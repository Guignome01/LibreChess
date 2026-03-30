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
  /// Provider identity stored in GameMeta for game resume.
  static constexpr uint8_t ENGINE_ID = 1;

  /// 8 difficulty levels for the on-board engine (depths 1–8).
  static constexpr int LEVEL_COUNT = 8;
  static constexpr DifficultyLevel LEVELS[LEVEL_COUNT] = {
      {"Beginner", 1},  {"Easy", 2},         {"Intermediate", 3},
      {"Medium", 4},    {"Advanced", 5},      {"Hard", 6},
      {"Expert", 7},    {"Master", 8},
  };
  static constexpr int DEFAULT_LEVEL = 4;  // Medium

  /// Construct from a 1-based difficulty level (1–8). Clamped to valid range.
  explicit LibreChessProvider(int level = DEFAULT_LEVEL,
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
  };

  static void taskFunction(void* param);

  int level_;
  int depth_;
  char playerColor_;
  int currentEvaluation_ = 0;
};

#endif  // LIBRECHESS_PROVIDER_H
