#ifndef GAME_MODE_H
#define GAME_MODE_H

#include "game.h"
#include "logger.h"
#include "move.h"
#include "types.h"
#include <Arduino.h>
#include <cstring>

using namespace LibreChess;

// Forward declaration to avoid circular dependency
class Board;
class WiFiManagerESP32;

// ---------------------------------------------------------------------------
// Game metadata — firmware-owned overlay for GameHeader::meta[]
// ---------------------------------------------------------------------------

// Game mode identifiers — firmware interprets these; the library stores them
// as opaque bytes in GameHeader::meta[].
enum class GameModeId : uint8_t {
  NONE = 0,
  PLAYER = 1,    // Human vs human
  BOT = 2,       // Human vs engine (Stockfish / LibreChess)
  LICHESS = 3    // Online via Lichess
};

/// Firmware-specific metadata packed into GameHeader::meta[GAME_META_SIZE].
/// The lib stores these bytes without interpretation.
struct GameMeta {
  uint8_t mode;        // GameModeId enum value
  uint8_t engineId;    // Provider identity (opaque, set by each EngineProvider)
  uint8_t difficulty;  // Engine search depth (0 = unused)
};
static_assert(sizeof(GameMeta) == GAME_META_SIZE, "GameMeta must match GAME_META_SIZE");

// Helper — pack meta into the raw byte array expected by Game::startNewGame().
inline const uint8_t* metaBytes(const GameMeta& m) {
  return reinterpret_cast<const uint8_t*>(&m);
}

// Helper — read meta from the raw byte array returned by Game::getActiveGameInfo().
inline GameMeta readMeta(const uint8_t* raw) {
  GameMeta m;
  memcpy(&m, raw, sizeof(GameMeta));
  return m;
}

// Base class for chess game modes (shared state and common functionality).
// All chess-state mutations flow through `chess_` (Game orchestrator),
// which atomically updates the board, records moves, and notifies observers.
class GameMode {
 protected:
  Board* board_;
  WiFiManagerESP32* wifiManager_;
  Game* chess_;
  Log logger_;

  // --- Resign ---
  static constexpr unsigned long RESIGN_HOLD_MS = 3000;       // Duration king must stay off its square to initiate resign
  static constexpr unsigned long RESIGN_LIFT_WINDOW_MS = 1000; // Max time per quick lift during gesture
  bool resignPending_ = false;    // Set by web resign endpoint

  // Constructor
  GameMode(Board* board, WiFiManagerESP32* wm, Game* cg, ILogger* logger = nullptr);

  // Common initialization and game flow methods
  void waitForBoardSetup();
  MoveResult applyMove(int fromRow, int fromCol, int toRow, int toCol, char promotion = ' ', bool isRemoteMove = false);
  MoveResult applyMove(const std::string& move);
  bool tryPlayerMove(Color playerColor, int& fromRow, int& fromCol, int& toRow, int& toCol);

  /// Try to resume a live game from Game. Returns true if resumed.
  /// If no live game exists, returns false (caller should start a new game).
  bool tryResumeGame();

  // --- Resign ---
  /// Unified resign entry point. Call at the start of update() after readSensors().
  /// Returns true if the game loop should return early.
  bool processResign();
  /// Handle resign confirmation and game-end sequence.
  /// Uses virtual hooks so subclasses can customize behavior without
  /// duplicating the flow.
  bool handleResign(Color resignColor);

  // --- Resign hooks (override in subclasses) ---

  /// Board orientation for the confirm dialog (true = black at bottom).
  /// Default: white at bottom.
  virtual bool isFlipped() const { return false; }
  /// Called before the confirm dialog (e.g. stop thinking animation).
  virtual void onBeforeResignConfirm() {}
  /// Called when the user cancels the resign (e.g. restart thinking).
  virtual void onResignCancelled() {}
  /// Called after resign is confirmed, before endGame (e.g. API calls).
  virtual void onResignConfirmed(Color resignColor) {}

 private:
  /// Blocking loop for the 2 quick lifts after the initial king return.
  /// Called inline from tryPlayerMove(). Returns true if resign was confirmed.
  bool continueResignGesture(int row, int col, Color color);
  // Virtual hooks for remote move handling (overridden in subclasses)
  virtual void waitForRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol, bool isCapture, bool isEnPassant = false, int enPassantCapturedPawnRow = -1) {}

 public:
  virtual ~GameMode() {}

  virtual void begin() = 0;
  virtual void update() = 0;

  void setBoardStateFromFEN(const std::string& fen);
  bool isGameOver() const;
  void setResignPending(bool pending) { resignPending_ = pending; }
  virtual bool isNavigationAllowed() const { return true; }
};

#endif // GAME_MODE_H
