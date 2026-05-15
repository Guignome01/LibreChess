#ifndef GAME_PROVIDER_H
#define GAME_PROVIDER_H

#include "types.h"

#include <stdint.h>
#include <string>

// ---------------------------------------------------------------------------
// Engine provider contract — pure game-layer data and interface.
// ---------------------------------------------------------------------------
// This header deliberately contains no Arduino, FreeRTOS, or firmware mode
// types. Firmware integrations may map the opaque mode byte into their own
// GameMeta overlay when starting or recording games.
// ---------------------------------------------------------------------------

/// Difficulty descriptor used by firmware engine integrations.
struct DifficultyLevel {
  const char* label;
  uint8_t depth;
};

/// Result from engine initialization, populated before a game starts.
struct EngineInitResult {
  char playerColor = 'w';
  std::string fen;
  uint8_t mode = 0;
  uint8_t engineId = 0;
  uint8_t difficulty = 0;
  bool canResume = true;
};

/// Result from an asynchronous engine computation or remote game event.
struct EngineResult {
  enum Type { NONE, MOVE, GAME_ENDED };

  Type type = NONE;
  std::string move;
  int evaluation = 0;
  LibreChess::GameResult gameResult = LibreChess::GameResult::IN_PROGRESS;
  char winnerColor = ' ';
};

/// Pure provider interface consumed by game modes.
class EngineProvider {
 public:
  virtual ~EngineProvider() = default;

  /// Initialize the provider before a game begins. May block.
  virtual bool initialize(EngineInitResult& result) = 0;

  /// Start an asynchronous move request from the current FEN.
  virtual void requestMove(const std::string& fen) = 0;

  /// Poll for completion of the active request.
  virtual bool checkResult(EngineResult& result) = 0;

  /// Cancel any in-flight request. Firmware helpers override this when needed.
  virtual void cancelRequest() {}

  /// Called after a local player move is applied.
  virtual bool onPlayerMoveApplied(const std::string& moveCoord) { return true; }

  /// Called after a resignation is confirmed.
  virtual void onResignConfirmed() {}

  /// Engine evaluation in centipawns for the web UI.
  virtual int getEvaluation() { return 0; }
};

#endif  // GAME_PROVIDER_H
