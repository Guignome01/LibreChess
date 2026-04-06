#ifndef GAME_BOT_MODE_H
#define GAME_BOT_MODE_H

#include "game_mode.h"
#include "engine/engine_provider.h"
#include <atomic>

// State machine for the engine turn cycle.
enum class BotState { PLAYER_TURN, ENGINE_THINKING };

// Concrete game mode for human-vs-engine play.
// Composes an EngineProvider* (strategy) for all engine communication.
// The provider runs HTTP in a FreeRTOS task; update() stays non-blocking.
class BotMode : public GameMode {
 public:
  BotMode(BoardDriver* bd, WiFiManagerESP32* wm, Game* cg, EngineProvider* provider,
          ILogger* logger = nullptr);
  ~BotMode() override;

  void begin() override;
  void update() override;
  bool isNavigationAllowed() const override;

 private:
  EngineProvider* provider_;
  char playerColor_ = 'w';
  BotState botState_ = BotState::PLAYER_TURN;
  int engineRetryCount_ = 0;
  std::atomic<bool>* thinkingAnimation_ = nullptr;
  bool wasThinkingBeforeResign_ = false;

  /// Convert stored char playerColor to Color enum.
  Color playerColor() const { return playerColor_ == 'w' ? Color::WHITE : Color::BLACK; }

  // Apply a move string returned by the engine (e.g., "e2e4").
  // Returns false if the move could not be applied (corrupt data).
  bool applyEngineMove(const std::string& move);
  // Handle a remote game-end event reported by the provider.
  void handleRemoteGameEnd(const EngineResult& result);
  // Abort with a red flash and end the game.
  void abortWithError(const char* message);

  // --- Thinking animation helpers ---
  void startThinking();
  void stopThinking();

  // --- Resign hooks ---
  bool isFlipped() const override { return playerColor() == Color::BLACK; }
  void onBeforeResignConfirm() override;
  void onResignCancelled() override;
  void onResignConfirmed(Color resignColor) override;

  // Remote move guidance (LED + sensor blocking). Shared by all engines.
  void waitForRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol, bool isCapture, bool isEnPassant = false, int enPassantCapturedPawnRow = -1) override;
};

#endif  // GAME_BOT_MODE_H
