#include "librechess_provider.h"

#include "game_mode/game_mode.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <new>

#include "notation.h"
#include "movegen.h"

// ---------------------------------------------------------------------------
// LibreChessProvider — on-board chess engine using Game::calculateMove().
//
// Game::initSearch() is called once in initialize(), allocating TT, hash
// tables, and SearchState that persist for the game's lifetime.  Each
// requestMove() spawns a FreeRTOS task that:
//   1. Wires the cancellation flag.
//   2. Calls game->calculateMove(limits) on a Game-owned snapshot.
//   3. Converts the SearchResult to an EngineResult.
//   4. Sets the result and marks ready.
//
// The task is cooperative-cancellable via ctx->cancel → SearchLimits.stop.
// ---------------------------------------------------------------------------

// Shared heap-sizing constants — used in both initialize() and taskFunction().
static constexpr size_t MIN_FREE_HEAP      = 32 * 1024;
static constexpr size_t EVAL_HASH_OVERHEAD = 12 * 1024;   // pawn (6 KiB) + eval (4 KiB) hash
static constexpr size_t SEARCH_OVERHEAD    = 16 * 1024;   // SearchState (~10 KiB + headroom)

LibreChessProvider::LibreChessProvider(LibreChess::Game* game, int level,
                                       char playerColor, ILogger* logger)
    : EngineProvider(logger), game_(game), playerColor_(playerColor) {
  // Clamp to valid range and resolve depth from the level table.
  level_ = (level < 1) ? DEFAULT_LEVEL : (level > LEVEL_COUNT) ? DEFAULT_LEVEL : level;
  depth_ = LEVELS[level_ - 1].depth;
}

LibreChessProvider::~LibreChessProvider() {
  // Stop any running search task before teardown.
  cancelRequest();
}

bool LibreChessProvider::initialize(EngineInitResult& result) {
  logger_.info("LibreChessProvider: initializing on-board engine");
  logger_.infof("  level=%d, depth=%d", level_, depth_);

  // --- Initialize search resources on the Game object ---
  //
  // The TT is sized once against available heap, capped at 64 KiB.
  // Hash tables (pawn 6 KiB + eval 4 KiB) are allocated inside
  // Game::initSearch().  All persist across moves.
  static constexpr size_t MAX_TT_BYTES  = 64 * 1024;
  static constexpr size_t TOTAL_OVERHEAD = MIN_FREE_HEAP + EVAL_HASH_OVERHEAD + SEARCH_OVERHEAD;
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

  game_->initSearch(ttEntries);
  if (!game_->searchInitialized()) {
    logger_.error("LibreChess: search engine allocation failed; fallback moves will be used");
  } else if (game_->searchHashTableAllocationFailed()) {
    logger_.error("LibreChess: search hash table allocation failed; continuing with reduced caching");
  } else if (!game_->searchHashTablesReady()) {
    logger_.info("LibreChess: search initialized without all hash tables");
  }
  game_->setTimeFunc([]() -> uint32_t { return millis(); });

  result.playerColor = playerColor_;
  result.fen = "";  // Starting position
  result.mode = GameModeId::BOT;
  result.engineId = ENGINE_ID;
  result.difficulty = static_cast<uint8_t>(level_);
  result.canResume = true;
  return true;
}

void LibreChessProvider::requestMove(const std::string& fen) {
  auto* ctx = new (std::nothrow) TaskContext();
  if (!ctx) {
    logger_.error("LibreChessProvider: failed to allocate task context");
    setImmediateResult(EngineResult{});
    return;
  }
  ctx->fen = fen;
  ctx->depth = depth_;
  ctx->game = game_;
  // Stack budget for lcTask (64 KiB = 65536 bytes):
  //   Position snapshot + findBestMove frame .......... ~3,500 B
  //   Per negamax ply (MovePicker with int16_t arrays) . ~2,200 B × depth
  //   Per quiescence ply (MoveList + int16_t scores) .. ~1,200 B × QS depth
  //   Max depth 8 + extensions (~6) = 14 negamax + 16 QS ≈ 52 KiB.
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
  LibreChess::movegen::generateMoves(
      pos.bitboards(), pos.mailbox(), pos.sideToMove(), pos.positionState(),
      moves, LibreChess::movegen::FilterMode::ALL);

  if (moves.count == 0) return false;

  logger.info("LibreChess: using fallback move (search unavailable)");
  return moveToResult(moves.moves[0], out);
}

void LibreChessProvider::taskFunction(void* param) {
  auto* ctx = static_cast<TaskContext*>(param);

  // Wire the cancellation flag for this search.
  ctx->game->setExternalStop(&ctx->cancel);

  // No need to load position — Game already has the current board state.
  // BotMode always calls requestMove(chess_->getFen()) after making moves
  // through Game, so the position is already correct.  Calling loadFEN()
  // from a FreeRTOS task would be unsafe (clears history, notifies observer).

  // Search — TT, hash tables, and SearchState are already allocated
  // inside Game by initSearch().
  LibreChess::search::SearchLimits limits;
  limits.maxDepth = (ctx->depth > 0) ? ctx->depth : 6;

  auto result = ctx->game->calculateMove(limits);

  // Convert SearchResult → EngineResult
  if (!moveToResult(result.bestMove, ctx->result, result.score)) {
    ctx->logger.error("LibreChess: search returned no move, trying fallback");
    fallbackMove(ctx->fen, ctx->result, ctx->logger);
  }

  ctx->ready.store(true);
  vTaskDelete(nullptr);
}
