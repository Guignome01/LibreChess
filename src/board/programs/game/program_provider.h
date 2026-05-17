#ifndef BOARD_PROGRAMS_GAME_PROGRAM_PROVIDER_H
#define BOARD_PROGRAMS_GAME_PROGRAM_PROVIDER_H

#include "board/assistance_provider.h"
#include "board/programs/game/game_provider.h"
#include "board/services/program/program.h"
#include "board/services/visual/animations.h"
#include "board/types.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// IBoardGame — board-facing physical chess interaction contract.
// ---------------------------------------------------------------------------
// GameMode consumes this interface instead of the concrete gameplay program.
// Concrete implementations live under src/board/programs/game/ and are created
// by the board program factory.
// ---------------------------------------------------------------------------

/// Result of polling the physical board for a player interaction.
enum class BoardGameResult : uint8_t {
  NONE,
  MOVE,
  RESIGN_REQUESTED,
};

/// Display-coordinate move or resign request detected on the physical board.
struct BoardGameMove {
  int fromRow = -1;
  int fromCol = -1;
  int toRow = -1;
  int toCol = -1;
  BoardPieceColor resignColor = BoardPieceColor::WHITE;
};

class IBoardGame : public BoardProgram {
 public:
  ~IBoardGame() override = default;

  // -------------------------------------------------------------------------
  // BoardProgram contract
  // -------------------------------------------------------------------------
  // The game program is a permanent-style program: it never completes on its
  // own and is detached only by an explicit `Board::stopProgram()`. Game-mode
  // drives gameplay manually through the IBoardGame methods below; the
  // polled `update()` slot is therefore a no-op.
  // -------------------------------------------------------------------------

  void begin() override { reset(); }
  void update() override {}
  void cancel() override { reset(); }
  bool isComplete() const override { return false; }

  /// Reset internal state for a fresh game. Called by `begin()`.
  virtual void reset() = 0;

  virtual void setAssistanceProvider(BoardAssistanceProvider* provider) = 0;
  virtual BoardAssistanceLevel assistanceLevel() const = 0;
  virtual void serviceAssistance() = 0;
  virtual void cancelAssistance() = 0;
  virtual void waitForSetup(const BoardGameProvider& gameRules) = 0;
  virtual BoardGameResult tryPlayerMove(const BoardGameProvider& gameRules,
                                            BoardPieceColor playerColor,
                                            BoardGameMove& selection) = 0;
  virtual void completeAppliedMove(const BoardMoveCompletion& completion,
                                   const BoardMoveFeedbackData& feedback, int fromRow,
                                   int fromCol, int toRow, int toCol) = 0;
  virtual bool confirmResign(BoardPieceColor resignColor, bool flipped) = 0;
  virtual void showResignWinner(BoardPieceColor resignColor) = 0;
  /// Start a looping status animation. The returned token cancels it on destruction.
  virtual BoardAnimationToken startThinkingStatus() = 0;
  virtual BoardAnimationToken startWaitingStatus() = 0;
  virtual void showRemoteGameEnd(char winnerColor) = 0;
  virtual void showErrorFeedback() = 0;
};

#endif  // BOARD_PROGRAMS_GAME_PROGRAM_PROVIDER_H