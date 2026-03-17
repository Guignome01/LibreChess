#ifndef LIBRECHESS_PROVIDER_H
#define LIBRECHESS_PROVIDER_H

#include "engine/engine_provider.h"
#include "engine.h"

// ---------------------------------------------------------------------------
// EngineProvider implementation for the built-in LibreChess engine.
//
// Runs the search on-board via a FreeRTOS background task.  Uses the
// Engine facade in-process — no network required.  The TT is sized to
// fit available heap (capped at 128 KiB).
// ---------------------------------------------------------------------------

class LibreChessProvider : public EngineProvider {
 public:
  // `depth`: max search depth (0 = use time control instead).
  // `moveTimeMs`: time per move in ms (0 = use depth instead).
  // `playerColor`: 'w' or 'b' — reported through EngineInitResult.
  // At least one of depth/moveTimeMs should be non-zero.
  explicit LibreChessProvider(int depth = 6, uint32_t moveTimeMs = 0,
                              char playerColor = 'w',
                              ILogger* logger = nullptr);

  bool initialize(EngineInitResult& result) override;
  void requestMove(const std::string& fen) override;
  bool checkResult(EngineResult& result) override;
  int getEvaluation() override;

 private:
  struct TaskContext : BaseTaskContext {
    std::string fen;
    int depth;
    uint32_t moveTimeMs;
  };

  static void taskFunction(void* param);

  int depth_;
  uint32_t moveTimeMs_;
  char playerColor_;
  int currentEvaluation_ = 0;
};

#endif  // LIBRECHESS_PROVIDER_H
