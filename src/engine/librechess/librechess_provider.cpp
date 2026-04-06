#include "librechess_provider.h"

#include "game_mode/game_mode.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <memory>
#include <new>

#include "movegen.h"
#include "notation.h"

// ---------------------------------------------------------------------------
// LibreChessProvider — on-board chess engine using the core search library.
//
// The Engine (TT, pawn hash, eval hash) is created once in initialize() and
// persists for the game’s lifetime, avoiding heap fragmentation from per-move
// alloc/free cycles.  Each requestMove() spawns a FreeRTOS task that:
//   1. Wires the cancellation flag for this search.
//   2. Calls calculateMove(fen, limits) on the persistent Engine.
//   3. Converts the SearchResult to an EngineResult.
//   4. Sets the result and marks ready.
//
// Only SearchState (~31 KiB) is heap-allocated per search.  The TT persists
// across moves, enabling cross-move transposition reuse for stronger play.
//
// The task is cooperative-cancellable via ctx->cancel → SearchLimits.stop.
// ---------------------------------------------------------------------------

LibreChessProvider::LibreChessProvider(int level, char playerColor, ILogger* logger)
    : EngineProvider(logger), playerColor_(playerColor) {
  // Clamp to valid range and resolve depth from the level table.
  level_ = (level < 1) ? DEFAULT_LEVEL : (level > LEVEL_COUNT) ? DEFAULT_LEVEL : level;
  depth_ = LEVELS[level_ - 1].depth;
}

LibreChessProvider::~LibreChessProvider() {
  // Stop any running search task before destroying the engine it references.
  cancelRequest();
}

bool LibreChessProvider::initialize(EngineInitResult& result) {
  logger_.info("LibreChessProvider: initializing on-board engine");
  logger_.infof("  level=%d, depth=%d", level_, depth_);

  // --- Create persistent Engine with heap-sized TT ---
  //
  // The TT is sized once against available heap, capped at 128 KiB.
  // Hash tables (pawn 8 KiB + eval 8 KiB) are allocated inside the Engine
  // constructor.  All three persist across moves — no per-move fragmentation.
  // Reserve headroom for the per-search SearchState (~32 KiB) and a safety
  // margin for other tasks (32 KiB).
  static constexpr size_t MAX_TT_BYTES      = 128 * 1024;
  static constexpr size_t MIN_FREE_HEAP      = 32 * 1024;
  static constexpr size_t EVAL_HASH_OVERHEAD = 16 * 1024;   // pawn + eval hash
  static constexpr size_t SEARCH_OVERHEAD    = 40 * 1024;   // SearchState (~32 KiB + padding)
  static constexpr size_t TOTAL_OVERHEAD     = MIN_FREE_HEAP + EVAL_HASH_OVERHEAD + SEARCH_OVERHEAD;
  static constexpr size_t ENTRY_SIZE = sizeof(LibreChess::search::TTEntry);

  size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  logger_.infof("LibreChess: free heap %u, largest block %u", freeHeap, largestBlock);

  size_t usable = (largestBlock < freeHeap) ? largestBlock : freeHeap;
  if (usable < TOTAL_OVERHEAD) {
    logger_.errorf("LibreChess: insufficient heap (%u < %u)", usable, TOTAL_OVERHEAD);
    return false;
  }

  size_t ttBytes = (usable - TOTAL_OVERHEAD) / 4;
  if (ttBytes > MAX_TT_BYTES) ttBytes = MAX_TT_BYTES;
  int ttEntries = static_cast<int>(ttBytes / ENTRY_SIZE);
  if (ttEntries < 64) ttEntries = 64;  // Minimum viable TT

  logger_.infof("LibreChess: TT %d entries (%u bytes)",
                 ttEntries, static_cast<unsigned>(ttEntries * ENTRY_SIZE));

  auto* raw = new (std::nothrow) LibreChess::Engine(ttEntries);
  if (!raw) {
    logger_.error("LibreChess: failed to allocate Engine");
    return false;
  }
  engine_.reset(raw);
  engine_->setTimeFunc([]() -> uint32_t { return millis(); });

  result.playerColor = playerColor_;
  result.fen = "";  // Starting position
  result.mode = GameModeId::BOT;
  result.engineId = ENGINE_ID;
  result.difficulty = static_cast<uint8_t>(level_);
  result.canResume = true;
  return true;
}

void LibreChessProvider::requestMove(const std::string& fen) {
  auto* ctx = new TaskContext();
  ctx->fen = fen;
  ctx->depth = depth_;
  ctx->engine = engine_.get();
  // Stack budget for lcTask (64 KiB = 65536 bytes):
  //   findBestMove frame (MoveList + SearchResult) .... ~1,200 B
  //   Per negamax ply (MovePicker with int16_t arrays) . ~2,200 B × depth
  //   Per quiescence ply (MoveList + int16_t scores) .. ~1,200 B × QS depth
  //   Max depth 8 + extensions (~6) = 14 negamax + 16 QS ≈ 50 KiB.
  //   64 KiB provides comfortable headroom for all difficulty levels.
  spawnTask(ctx, "lcTask", taskFunction, 65536);
}

bool LibreChessProvider::checkResult(EngineResult& result) {
  if (!peekResult(result)) return false;
  if (result.type == EngineResult::MOVE)
    currentEvaluation_ = result.evaluation;
  finishTask();
  return true;
}

int LibreChessProvider::getEvaluation() {
  return currentEvaluation_;
}

// ---------------------------------------------------------------------------
// FreeRTOS task — runs the search in the background.
// ---------------------------------------------------------------------------

// Convert a core Move to a EngineResult::MOVE result.
// Returns false if the move is null (from==0 && to==0).
static bool moveToResult(const LibreChess::Move& move, EngineResult& out, int score = 0) {
  if (move.from == 0 && move.to == 0) return false;
  char promoChar = ' ';
  if (move.isPromotion()) {
    static const char promoChars[] = {'n', 'b', 'r', 'q'};
    promoChar = promoChars[move.promoIndex()];
  }
  std::string moveStr = LibreChess::notation::toCoordinate(
      move.from, move.to, promoChar);
  out.type = EngineResult::MOVE;
  out.move = moveStr;
  out.evaluation = score;
  return true;
}

// Generate legal moves and pick the first one as a fallback when the full
// search cannot run (e.g. insufficient heap for SearchState/TT).
// Uses only stack memory — no heap allocations required.
static bool fallbackMove(const std::string& fen, EngineResult& out, Log& logger) {
  LibreChess::Position pos;
  if (!pos.loadFEN(fen)) return false;

  LibreChess::MoveList moves;
  LibreChess::movegen::generateAllMoves(
      pos.bitboards(), pos.mailbox(), pos.sideToMove(), pos.positionState(), moves);

  if (moves.count == 0) return false;

  logger.info("LibreChess: using fallback move (search unavailable)");
  return moveToResult(moves.moves[0], out);
}

void LibreChessProvider::taskFunction(void* param) {
  auto* ctx = static_cast<TaskContext*>(param);

  // Wire the cancellation flag for this search.
  ctx->engine->setExternalStop(&ctx->cancel);

  // Verify the heap can still satisfy the per-search SearchState (~32 KiB).
  // The persistent Engine's TT and hash tables are already allocated, so
  // only SearchState + a free margin for other tasks need to be available.
  static constexpr size_t MIN_FREE_HEAP   = 32 * 1024;
  static constexpr size_t SEARCH_OVERHEAD = 40 * 1024;  // SearchState (~32 KiB + padding)

  size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  ctx->logger.infof("LibreChess: free heap %u, largest block %u", freeHeap, largestBlock);

  size_t usable = (largestBlock < freeHeap) ? largestBlock : freeHeap;
  if (usable < MIN_FREE_HEAP + SEARCH_OVERHEAD) {
    ctx->logger.errorf("LibreChess: insufficient heap for search (%u)", usable);
    fallbackMove(ctx->fen, ctx->result, ctx->logger);
    ctx->ready.store(true);
    vTaskDelete(nullptr);
    return;
  }

  // Search — SearchState (~31 KiB) is heap-allocated internally per search.
  LibreChess::search::SearchLimits limits;
  limits.maxDepth = (ctx->depth > 0) ? ctx->depth : 6;

  auto result = ctx->engine->calculateMove(ctx->fen, limits);

  // Convert SearchResult → EngineResult
  if (!moveToResult(result.bestMove, ctx->result, result.score)) {
    ctx->logger.error("LibreChess: search returned no move, trying fallback");
    fallbackMove(ctx->fen, ctx->result, ctx->logger);
  }

  ctx->ready.store(true);
  vTaskDelete(nullptr);
}
