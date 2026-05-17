#ifndef BOARD_PROGRAMS_GAME_GAME_PROVIDER_H
#define BOARD_PROGRAMS_GAME_GAME_PROVIDER_H

#include "board/types.h"

// ---------------------------------------------------------------------------
// BoardGameProvider — board-facing game rules contract.
// ---------------------------------------------------------------------------
// The board game program consumes this interface for setup, lifted-piece
// lookup, and legal-target validation. Concrete rules implementations live
// outside src/board/ so the board package does not depend on LibreChess::Game
// or concrete engines.
// ---------------------------------------------------------------------------

class BoardGameProvider {
 public:
  virtual ~BoardGameProvider() = default;

  virtual BoardPiece pieceAt(int row, int col) const = 0;
  virtual void setupSnapshot(BoardSetupSnapshot& snapshot) const = 0;
  virtual void legalTargets(int fromRow, int fromCol, BoardMoveTargetList& targets) const = 0;
};

#endif  // BOARD_PROGRAMS_GAME_GAME_PROVIDER_H