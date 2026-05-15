#ifndef BOARD_WORKFLOWS_GAMEPLAY_H
#define BOARD_WORKFLOWS_GAMEPLAY_H

#include "board/core/visual/assistance.h"
#include "board/core/visual/animations.h"
#include "board/core/visual/feedback.h"
#include "board/assistance_provider.h"
#include "board/game_provider.h"
#include "board/types.h"

#include <stdint.h>

class BoardAnimations;
class BoardMenuRunner;
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
  BoardPieceColor resignColor = BoardPieceColor::WHITE;
};

class BoardGameplay {
 public:
  BoardGameplay(BoardRuntime& runtime, BoardAnimations& animations, BoardMenuRunner& menuRunner);

  /// Configure the active board-owned assistance provider.
  void setAssistanceProvider(BoardAssistanceProvider* provider);
  BoardAssistanceLevel assistanceLevel() const { return assistance_.level(); }

  /// Service the active assistance provider. No-op for none/legal providers.
  void serviceAssistance();

  /// Cancel any provider work and clear assistance visuals.
  void cancelAssistance();

  /// Wait until the physical board matches the in-memory game position.
  void waitForSetup(const BoardGameProvider& gameProvider);

  /// Detect a legal player move or resign gesture from current input state.
  BoardGameplayResult tryPlayerMove(const BoardGameProvider& gameProvider,
                                    BoardPieceColor playerColor,
                                    BoardGameplayMove& selection);

  /// Guide physical completion of a move (castling steps, remote-engine
  /// move) and show outcome visuals.
  void completeAppliedMove(const BoardMoveCompletion& completion,
                           const BoardMoveFeedbackData& feedback, int fromRow, int fromCol,
                           int toRow, int toCol);

  /// Run resign confirmation prompt (blocking; renderer keeps prompt alive).
  bool confirmResign(BoardPieceColor resignColor, bool flipped);

  /// Show the winner feedback for a confirmed resignation.
  void showResignWinner(BoardPieceColor resignColor);

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
  BoardMenuRunner& menuRunner_;
  BoardFeedback feedback_;
  BoardAssistance assistance_;
  BoardAssistanceProvider* assistanceProvider_ = nullptr;

  BoardGameplayResult handleSourceRestore(int row, int col, BoardPieceColor color,
                                          bool resignTransitioned,
                                          unsigned long resignFlagTimestamp,
                                          BoardGameplayMove& selection);
  bool continueResignGesture(int row, int col, BoardPieceColor color);
};

#endif  // BOARD_WORKFLOWS_GAMEPLAY_H
