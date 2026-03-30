#include "librechess_provider.h"

#include "game_mode/game_mode.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <memory>

#include "notation.h"
#include "square.h"

// ---------------------------------------------------------------------------
// LibreChessProvider — on-board chess engine using the core search library.
//
// Each requestMove() spawns a FreeRTOS task that:
//   1. Creates an Engine with an appropriately-sized TT.
//   2. Calls calculateMove(fen, limits) directly — no string serialization.
//   3. Converts the SearchResult to an EngineResult.
//   4. Sets the result and marks ready.
//
// The task is cooperative-cancellable via ctx->cancel → SearchLimits.stop.
// ---------------------------------------------------------------------------

LibreChessProvider::LibreChessProvider(int level, char playerColor, ILogger* logger)
    : EngineProvider(logger), playerColor_(playerColor) {
  // Clamp to valid range and resolve depth from the level table.
  level_ = (level < 1) ? DEFAULT_LEVEL : (level > LEVEL_COUNT) ? DEFAULT_LEVEL : level;
  depth_ = LEVELS[level_ - 1].depth;
}

bool LibreChessProvider::initialize(EngineInitResult& result) {
  logger_.info("LibreChessProvider: initializing on-board engine");
  logger_.infof("  level=%d, depth=%d", level_, depth_);
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

void LibreChessProvider::taskFunction(void* param) {
  auto* ctx = static_cast<TaskContext*>(param);

  // Size the TT based on available heap, capped at 128 KiB.
  // Each TTEntry is ≤16 bytes.  Leave at least 32 KiB free for other tasks.
  // The Engine also allocates a pawn hash table (8 KiB) and eval hash table
  // (8 KiB), so reserve an additional 16 KiB for those.
  static constexpr size_t MAX_TT_BYTES = 128 * 1024;
  static constexpr size_t MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t EVAL_HASH_OVERHEAD = 16 * 1024;  // pawn + eval hash
  static constexpr size_t ENTRY_SIZE = sizeof(LibreChess::search::TTEntry);

  size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t ttBytes = 0;
  if (freeHeap > MIN_FREE_HEAP + EVAL_HASH_OVERHEAD) {
    ttBytes = (freeHeap - MIN_FREE_HEAP - EVAL_HASH_OVERHEAD) / 4;
    if (ttBytes > MAX_TT_BYTES) ttBytes = MAX_TT_BYTES;
  }
  int ttEntries = static_cast<int>(ttBytes / ENTRY_SIZE);
  if (ttEntries < 64) ttEntries = 64;  // Minimum viable TT

  ctx->logger.infof("LibreChess: TT %d entries (%u bytes), free heap %u",
                     ttEntries, ttEntries * ENTRY_SIZE, freeHeap);

  // Heap-allocate the Engine to keep the task stack small and ensure
  // cleanup via unique_ptr — vTaskDelete() does not unwind the C++ stack,
  // so stack-local objects would leak their heap allocations (TT, pawn
  // hash, eval hash).
  auto engine = std::make_unique<LibreChess::Engine>(ttEntries);
  engine->setTimeFunc([]() -> uint32_t { return millis(); });
  engine->setExternalStop(&ctx->cancel);

  // Build search limits (depth-based only; time control removed)
  LibreChess::search::SearchLimits limits;
  limits.maxDepth = (ctx->depth > 0) ? ctx->depth : 6;

  // Run the search — returns structured result, no string parsing needed
  auto result = engine->calculateMove(ctx->fen, limits);

  // Release engine resources before task termination
  engine.reset();

  // Convert SearchResult → EngineResult
  if (result.bestMove.from != 0 || result.bestMove.to != 0) {
    std::string moveStr = LibreChess::notation::toCoordinate(
        LibreChess::rowOf(result.bestMove.from),
        LibreChess::colOf(result.bestMove.from),
        LibreChess::rowOf(result.bestMove.to),
        LibreChess::colOf(result.bestMove.to));
    if (result.bestMove.isPromotion()) {
      static const char promoChars[] = {'n', 'b', 'r', 'q'};
      moveStr += promoChars[result.bestMove.promoIndex()];
    }
    ctx->result.type = EngineResult::MOVE;
    ctx->result.move = moveStr;
    ctx->result.evaluation = result.score;
  } else {
    ctx->logger.error("LibreChess: no legal move found");
  }

  ctx->ready.store(true);
  vTaskDelete(nullptr);
}
