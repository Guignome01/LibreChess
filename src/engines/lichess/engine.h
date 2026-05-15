#ifndef ENGINES_LICHESS_ENGINE_H
#define ENGINES_LICHESS_ENGINE_H

#include "engines/async_provider.h"
#include "engines/lichess/api.h"
#include "engines/lichess/config.h"

// EngineProvider implementation for Lichess online play.
// initialize() blocks while waiting for a game (BotMode shows animation).
// requestMove() spawns a persistent NDJSON stream task for opponent moves.
class LichessEngine : public AsyncEngineProvider {
 public:
  explicit LichessEngine(const LichessConfig& config, LibreChess::ILogger* logger = nullptr);

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

#endif  // ENGINES_LICHESS_ENGINE_H
