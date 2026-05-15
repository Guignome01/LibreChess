#ifndef BOARD_GAME_PROVIDER_H
#define BOARD_GAME_PROVIDER_H

#include "board/types.h"

// ---------------------------------------------------------------------------
// BoardGameProvider — board-facing gameplay contract.
// ---------------------------------------------------------------------------
// The board owns this interface because BoardGameplay consumes it for setup
// and physical move validation. Concrete adapters live outside src/board/ so
// the board package does not depend on LibreChess::Game or concrete engines.
// ---------------------------------------------------------------------------

class BoardGameProvider {
 public:
  virtual ~BoardGameProvider() = default;

  virtual BoardPiece pieceAt(int row, int col) const = 0;
  virtual void setupSnapshot(BoardSetupSnapshot& snapshot) const = 0;
  virtual void legalTargets(int fromRow, int fromCol, BoardMoveTargetList& targets) const = 0;
};

#endif  // BOARD_GAME_PROVIDER_H