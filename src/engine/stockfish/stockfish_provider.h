#ifndef STOCKFISH_PROVIDER_H
#define STOCKFISH_PROVIDER_H

#include "engine/engine_provider.h"
#include "engine/stockfish/stockfish_settings.h"

// EngineProvider implementation for the Stockfish online API.
// Spawns a FreeRTOS task per move request for non-blocking HTTP.
class StockfishProvider : public EngineProvider {
 public:
  /// Provider identity stored in GameMeta for game resume.
  static constexpr uint8_t ENGINE_ID = 0;

  /// 8 difficulty levels mapping to the Stockfish API's valid depth range (6–16).
  static constexpr int LEVEL_COUNT = 8;
  static constexpr DifficultyLevel LEVELS[LEVEL_COUNT] = {
      {"Beginner", 6},  {"Easy", 7},         {"Intermediate", 8},
      {"Medium", 9},    {"Advanced", 10},     {"Hard", 12},
      {"Expert", 14},   {"Master", 16},
  };
  static constexpr int DEFAULT_LEVEL = 4;  // Medium

  /// Construct from a 1-based difficulty level (1–8). Clamped to valid range.
  explicit StockfishProvider(int level = DEFAULT_LEVEL, char playerColor = 'w',
                             ILogger* logger = nullptr);

  bool initialize(EngineInitResult& result) override;
  void requestMove(const std::string& fen) override;
  bool checkResult(EngineResult& result) override;
  int getEvaluation() override;

 private:
  // Heap-allocated context shared between the caller and the FreeRTOS task.
  struct TaskContext : BaseTaskContext {
    std::string fen;
    int depth;
    int timeoutMs;
    int maxRetries;
  };

  static void taskFunction(void* param);

  int level_;
  StockfishSettings settings_;
  char playerColor_;
  int currentEvaluation_ = 0;
};

#endif  // STOCKFISH_PROVIDER_H
