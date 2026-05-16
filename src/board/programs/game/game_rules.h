#ifndef BOARD_PROGRAMS_GAME_RULES_H
#define BOARD_PROGRAMS_GAME_RULES_H

#include "board/types.h"

// ---------------------------------------------------------------------------
// BoardGameRules — board-facing game rules contract.
// ---------------------------------------------------------------------------
// The board game program consumes this interface for setup, lifted-piece
// lookup, and legal-target validation. Concrete rules implementations live
// outside src/board/ so the board package does not depend on LibreChess::Game
// or concrete engines.
// ---------------------------------------------------------------------------

class BoardGameRules {
 public:
  virtual ~BoardGameRules() = default;

  virtual BoardPiece pieceAt(int row, int col) const = 0;
  virtual void setupSnapshot(BoardSetupSnapshot& snapshot) const = 0;
  virtual void legalTargets(int fromRow, int fromCol, BoardMoveTargetList& targets) const = 0;
};

#endif  // BOARD_PROGRAMS_GAME_RULES_H