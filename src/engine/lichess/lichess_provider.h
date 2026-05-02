#ifndef LICHESS_PROVIDER_H
#define LICHESS_PROVIDER_H

#include "engine/engine_provider.h"
#include "engine/lichess/lichess_api.h"
#include "engine/lichess/lichess_config.h"

// EngineProvider implementation for Lichess online play.
// initialize() blocks while waiting for a game (BotMode shows animation).
// requestMove() spawns a persistent NDJSON stream task for opponent moves.
class LichessProvider : public EngineProvider {
 public:
  explicit LichessProvider(const LichessConfig& config, ILogger* logger = nullptr);

  bool initialize(EngineInitResult& result) override;
  void requestMove(const std::string& fen) override;
  bool checkResult(EngineResult& result) override;
  bool onPlayerMoveApplied(const std::string& moveCoord) override;
  void onResignConfirmed() override;

 private:
  // Heap-allocated context shared between the caller and the FreeRTOS task.
  struct TaskContext : BaseTaskContext {
    LichessConfig config;
    String gameId;
    char playerColor;
    int lastKnownMoveCount;
    String lastSentMove;
  };

  static void taskFunction(void* param);

  LichessConfig config_;
  LichessAPI api_;
  String currentGameId_;
  char playerColor_ = 'w';
  int lastKnownMoveCount_ = 0;
  String lastSentMove_;
};

#endif  // LICHESS_PROVIDER_H
