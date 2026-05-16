#ifndef BOARD_PROGRAMS_GAME_GAMEPLAY_H
#define BOARD_PROGRAMS_GAME_GAMEPLAY_H

#include "board/programs/game/game_program.h"
#include "board/programs/game/visuals/assistance.h"
#include "board/programs/game/visuals/feedback.h"

#include <stdint.h>

class BoardAnimations;
class BoardMenuRunner;
class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardGameplay — physical chess interaction component for the game program.
// ---------------------------------------------------------------------------
// Owns its own BoardFeedback + BoardAssistance instances (programs are no
// longer composed by the controller). Drains synchronized input-event batches
// from BoardRuntime and paints feedback/assistance through the canvas guard.
//
// `tryPlayerMove` walks recently changed squares (debounced by BoardInput)
// to detect player intent: piece pickup, target placement, capture,
// resignation gesture (king held off-board for 3 s).
// ---------------------------------------------------------------------------

class BoardGameplay final : public BoardGameProgram {
 public:
  BoardGameplay(BoardRuntime& runtime, BoardAnimations& animations, BoardMenuRunner& menuRunner);

  /// Reset internal state for a fresh game.
  void reset() override;

  /// Configure the active board-owned assistance provider.
  void setAssistanceProvider(BoardAssistanceProvider* provider) override;
  BoardAssistanceLevel assistanceLevel() const override { return assistance_.level(); }

  /// Service the active assistance provider. No-op for none/legal providers.
  void serviceAssistance() override;

  /// Cancel any provider work and clear assistance visuals.
  void cancelAssistance() override;

  /// Wait until the physical board matches the in-memory game position.
  void waitForSetup(const BoardGameRules& gameRules) override;

  /// Detect a legal player move or resign gesture from current input state.
  BoardGameplayResult tryPlayerMove(const BoardGameRules& gameRules,
                                    BoardPieceColor playerColor,
                                    BoardGameplayMove& selection) override;

  /// Guide physical completion of a move (castling steps, remote-engine
  /// move) and show outcome visuals.
  void completeAppliedMove(const BoardMoveCompletion& completion,
                           const BoardMoveFeedbackData& feedback, int fromRow, int fromCol,
                           int toRow, int toCol) override;

  /// Run resign confirmation prompt (blocking; renderer keeps prompt alive).
  bool confirmResign(BoardPieceColor resignColor, bool flipped) override;

  /// Show the winner feedback for a confirmed resignation.
  void showResignWinner(BoardPieceColor resignColor) override;

  // -------------------------------------------------------------------------
  // Status animation tokens — auto-cancel on destruction or reassignment.
  // -------------------------------------------------------------------------

  /// Start the looping THINKING animation.
  BoardAnimationToken startThinkingStatus() override;

  /// Start the looping WAITING animation.
  BoardAnimationToken startWaitingStatus() override;

  /// Show a remote game-end (winner) firework on the board.
  void showRemoteGameEnd(char winnerColor) override;

  /// Three red flashes for an unrecoverable error.
  void showErrorFeedback() override;

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

#endif  // BOARD_PROGRAMS_GAME_GAMEPLAY_H
