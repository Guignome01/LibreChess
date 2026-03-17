#include "librechess_provider.h"

#include "game_mode/game_mode.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

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

LibreChessProvider::LibreChessProvider(int depth, uint32_t moveTimeMs,
                                       char playerColor, ILogger* logger)
    : EngineProvider(logger),
      depth_(depth),
      moveTimeMs_(moveTimeMs),
      playerColor_(playerColor) {}

bool LibreChessProvider::initialize(EngineInitResult& result) {
  logger_.info("LibreChessProvider: initializing on-board engine");
  logger_.infof("  depth=%d, moveTimeMs=%u", depth_, moveTimeMs_);
  result.playerColor = playerColor_;
  result.fen = "";  // Starting position
  result.mode = GameModeId::BOT;
  result.difficulty = static_cast<uint8_t>(depth_);
  result.canResume = true;
  return true;
}

void LibreChessProvider::requestMove(const std::string& fen) {
  auto* ctx = new TaskContext();
  ctx->fen = fen;
  ctx->depth = depth_;
  ctx->moveTimeMs = moveTimeMs_;
  // 16 KiB stack: search uses ~2 KiB for MAX_MOVES arrays per ply,
  // plus ~16 KiB for SearchState (killers + history).
  spawnTask(ctx, "lcTask", taskFunction, 16384);
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
  static constexpr size_t MAX_TT_BYTES = 128 * 1024;
  static constexpr size_t MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t ENTRY_SIZE = sizeof(LibreChess::search::TTEntry);

  size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t ttBytes = 0;
  if (freeHeap > MIN_FREE_HEAP) {
    ttBytes = (freeHeap - MIN_FREE_HEAP) / 4;
    if (ttBytes > MAX_TT_BYTES) ttBytes = MAX_TT_BYTES;
  }
  int ttEntries = static_cast<int>(ttBytes / ENTRY_SIZE);
  if (ttEntries < 64) ttEntries = 64;  // Minimum viable TT

  ctx->logger.infof("LibreChess: TT %d entries (%u bytes), free heap %u",
                     ttEntries, ttEntries * ENTRY_SIZE, freeHeap);

  // Create Engine and configure stop/time
  LibreChess::Engine engine(ttEntries);
  engine.setTimeFunc([]() -> uint32_t { return millis(); });
  engine.setExternalStop(&ctx->cancel);

  // Build search limits
  LibreChess::search::SearchLimits limits;
  if (ctx->depth > 0) limits.maxDepth = ctx->depth;
  if (ctx->moveTimeMs > 0) limits.maxTimeMs = ctx->moveTimeMs;
  if (ctx->depth <= 0 && ctx->moveTimeMs <= 0) limits.maxDepth = 6;

  // Run the search — returns structured result, no string parsing needed
  auto result = engine.calculateMove(ctx->fen, limits);

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
