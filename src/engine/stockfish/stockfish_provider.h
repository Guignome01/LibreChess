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

  explicit StockfishProvider(const StockfishSettings& settings, char playerColor = 'w',
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

  StockfishSettings settings_;
  char playerColor_;
  int currentEvaluation_ = 0;
};

#endif  // STOCKFISH_PROVIDER_H
