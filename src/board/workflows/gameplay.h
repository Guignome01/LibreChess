#ifndef BOARD_WORKFLOWS_GAMEPLAY_H
#define BOARD_WORKFLOWS_GAMEPLAY_H

#include "board/gui/assistance.h"
#include "board/gui/animations.h"
#include "board/gui/feedback.h"
#include "game.h"
#include "logger.h"

#include <stdint.h>

class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardGameplay — physical chess interaction workflow.
// ---------------------------------------------------------------------------
// Owns its own BoardFeedback + BoardAssistance instances (workflows are no
// longer composed by the controller). Drains synchronized input-event batches
// from BoardRuntime and paints feedback/assistance through the canvas guard.
//
// `tryPlayerMove` walks recently changed squares (debounced by BoardInput)
// to detect player intent: piece pickup, target placement, capture,
// resignation gesture (king held off-board for 3 s).
// ---------------------------------------------------------------------------

/// Result of polling the physical board for a player interaction.
enum class BoardGameplayResult : uint8_t {
  NONE,
  MOVE,
  RESIGN_REQUESTED,
};

/// Display-coordinate move or resign request detected on the physical board.
struct BoardGameplayMove {
  int fromRow = -1;
  int fromCol = -1;
  int toRow = -1;
  int toCol = -1;
  LibreChess::Color resignColor = LibreChess::Color::WHITE;
};

class BoardGameplay {
 public:
  BoardGameplay(BoardRuntime& runtime, BoardAnimations& animations);

  /// Wait until the physical board matches the in-memory game position.
  void waitForSetup(const LibreChess::Game& game, LibreChess::Log& logger);

  /// Detect a legal player move or resign gesture from current input state.
  BoardGameplayResult tryPlayerMove(const LibreChess::Game& game, LibreChess::Color playerColor,
                                    LibreChess::Log& logger, BoardGameplayMove& selection);

  /// Guide physical completion of a move (castling steps, remote-engine
  /// move) and show outcome visuals.
  void completeAppliedMove(const LibreChess::Game& game, const LibreChess::MoveResult& result,
                           const LibreChess::CastlingInfo& castling, int fromRow, int fromCol,
                           int toRow, int toCol, bool isRemoteMove, LibreChess::Log& logger);

  /// Run resign confirmation prompt (blocking; renderer keeps prompt alive).
  bool confirmResign(LibreChess::Color resignColor, bool flipped, LibreChess::Log& logger);

  /// Show the winner feedback for a confirmed resignation.
  void showResignWinner(LibreChess::Color resignColor);

  // -------------------------------------------------------------------------
  // Status animation handles (BoardAnimationHandle replaces the old
  // std::atomic<bool>* signal).
  // -------------------------------------------------------------------------

  /// Start the looping THINKING animation.
  BoardAnimationHandle startThinkingStatus();

  /// Start the looping WAITING animation.
  BoardAnimationHandle startWaitingStatus();

  /// Cancel a status animation. Invalidates `handle`.
  void stopStatusAnimation(BoardAnimationHandle& handle);

  /// Show a remote game-end (winner) firework on the board.
  void showRemoteGameEnd(char winnerColor);

  /// Three red flashes for an unrecoverable error.
  void showErrorFeedback();

 private:
  static constexpr unsigned long RESIGN_HOLD_MS = 3000;
  static constexpr unsigned long RESIGN_LIFT_WINDOW_MS = 1000;

  BoardRuntime& runtime_;
  BoardAnimations& animations_;
  BoardFeedback feedback_;
  BoardAssistance assistance_;

  bool continueResignGesture(int row, int col, LibreChess::Color color, LibreChess::Log& logger);
};

#endif  // BOARD_WORKFLOWS_GAMEPLAY_H
