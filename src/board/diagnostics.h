#ifndef BOARD_DIAGNOSTICS_H
#define BOARD_DIAGNOSTICS_H

#include "board.h"

class BoardServices;

/// Board-owned diagnostics mode for validating physical sensor coverage.
class BoardDiagnostics {
 public:
  explicit BoardDiagnostics(Board& board);

  void begin();
  void update();
  bool isComplete() const { return complete_; }

 private:
  BoardServices& services_;
  bool visited_[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS];
  bool complete_;
  uint8_t visitedCount_;

  void clearVisited();
  void recordCurrentOccupancy();
  void recordVisitedSquare(int row, int col);
  void showVisitedSquares();
};

#endif  // BOARD_DIAGNOSTICS_H
