#ifndef ENGINE_PROVIDER_H
#define ENGINE_PROVIDER_H

#include "logger.h"
#include "types.h"
#include <Arduino.h>
#include <atomic>
#include <string>

using namespace LibreChess;

enum class GameModeId : uint8_t;  // Defined in game_mode.h.

// ---------------------------------------------------------------------------
// Difficulty level descriptor — each engine provider defines a static array
// of these (up to 8 entries, one per board-row square) mapping a 1-based
// level index to a human-readable label and a search depth.
// ---------------------------------------------------------------------------
struct DifficultyLevel {
  const char* label;   // Human-readable name (e.g. "Beginner")
  uint8_t depth;       // Engine search depth for this level
};

// Result from engine initialization (populated by the provider during begin()).
struct EngineInitResult {
  char playerColor = 'w';       // Discovered at runtime (Lichess) or passed in (Stockfish)
  std::string fen;              // Empty = starting position; non-empty = load this FEN
  GameModeId mode;                // BOT, LICHESS, etc. (firmware-owned enum)
  uint8_t engineId = 0;         // Provider-specific identifier (packed into GameMeta::mode high nibble)
  uint8_t difficulty = 0;       // Engine search depth for recording metadata; 0 = unused
  bool canResume = true;        // false = skip tryResumeGame (Lichess: state comes from server)
};

// Result from an async engine computation (move or game-end event).
struct EngineResult {
  enum Type { NONE, MOVE, GAME_ENDED };
  Type type = NONE;
  std::string move;             // UCI coordinate when type == MOVE (e.g., "e2e4")
  int evaluation = 0;          // Centipawns (positive = White advantage); 0 = use material count
  GameResult gameResult = GameResult::IN_PROGRESS;  // When type == GAME_ENDED
  char winnerColor = ' ';       // When type == GAME_ENDED
};

// Base context shared between the main loop and a FreeRTOS task.
// Derived providers add request-specific fields.
struct BaseTaskContext {
  virtual ~BaseTaskContext() = default;
  std::atomic<bool> cancel{false};
  std::atomic<bool> ready{false};
  EngineResult result;
  Log logger;
};

// Base class for chess engine providers.
// Manages a single FreeRTOS background task for async move computation.
// Providers handle computation/communication and return data only. They never
// touch board workflows, BoardDriver, or any hardware.
class EngineProvider {
 public:
  explicit EngineProvider(ILogger* logger = nullptr) : logger_(logger) {}
  virtual ~EngineProvider() { cancelRequest(); }

  // Lifecycle — called once during BotMode::begin(). May block (HTTP calls).
  // BotMode handles waiting animation before/after.
  // Returns false if initialization failed (BotMode will abort the game).
  virtual bool initialize(EngineInitResult& result) = 0;

  // Async move computation — non-blocking.
  // requestMove() spawns a background task; checkResult() polls for completion.
  virtual void requestMove(const std::string& fen) = 0;
  virtual bool checkResult(EngineResult& result) = 0;

  virtual void cancelRequest() {
    if (activeTask_) {
      activeTask_->cancel.store(true);
      unsigned long start = millis();
      while (!activeTask_->ready.load() && (millis() - start < 2000))
        delay(10);
      delete activeTask_;
      activeTask_ = nullptr;
    }
  }

  // Called after a local player move is applied. Lichess uses this to send the
  // move to the server. Returns false if the operation failed (BotMode aborts).
  virtual bool onPlayerMoveApplied(const std::string& moveCoord) { return true; }

  // Called after resign is confirmed. Lichess uses this to resign on the server.
  virtual void onResignConfirmed() {}

  // Engine evaluation for the web UI (centipawns). Returns 0 by default
  // (BotMode falls back to material count).
  virtual int getEvaluation() { return 0; }

 protected:
  Log logger_;
  BaseTaskContext* activeTask_ = nullptr;

  // Spawn a FreeRTOS task with the given context. Cancels any running task first.
  void spawnTask(BaseTaskContext* ctx, const char* name,
                 TaskFunction_t taskFn, uint32_t stackSize = 8192) {
    if (activeTask_) cancelRequest();
    ctx->logger = logger_;
    activeTask_ = ctx;
    if (xTaskCreate(taskFn, name, stackSize, ctx, 1, nullptr) != pdPASS) {
      logger_.error("EngineProvider: xTaskCreate failed (OOM?)");
      delete activeTask_;
      activeTask_ = nullptr;
      setImmediateResult(EngineResult{});
    } else {
      hasImmediateResult_ = false;
    }
  }

  // Poll for a completed result. Returns true and fills `result` when ready.
  // Deletes the task context — caller must do any post-processing before this.
  bool pollResult(EngineResult& result) {
    if (hasImmediateResult_) {
      result = immediateResult_;
      hasImmediateResult_ = false;
      return true;
    }
    if (!activeTask_ || !activeTask_->ready.load()) return false;
    result = activeTask_->result;
    delete activeTask_;
    activeTask_ = nullptr;
    return true;
  }

  // Like pollResult but does NOT delete the context, allowing the caller to
  // read provider-specific fields from the derived context first.
  // Caller MUST call finishTask() after reading extra fields.
  bool peekResult(EngineResult& result) {
    if (hasImmediateResult_) {
      result = immediateResult_;
      hasImmediateResult_ = false;
      return true;
    }
    if (!activeTask_ || !activeTask_->ready.load()) return false;
    result = activeTask_->result;
    return true;
  }

  // Delete the active task context after peekResult(). Must be called if
  // peekResult() returned true.
  void finishTask() {
    delete activeTask_;
    activeTask_ = nullptr;
  }

  // Publish a synchronous result when allocation or task creation fails
  // before a FreeRTOS task can own a context.
  void setImmediateResult(const EngineResult& result) {
    immediateResult_ = result;
    hasImmediateResult_ = true;
  }

 private:
  EngineResult immediateResult_;
  bool hasImmediateResult_ = false;
};

#endif  // ENGINE_PROVIDER_H
