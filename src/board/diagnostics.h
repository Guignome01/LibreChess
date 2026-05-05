#ifndef BOARD_DIAGNOSTICS_H
#define BOARD_DIAGNOSTICS_H

#include "core/state.h"
#include "core/system.h"

class BoardLayering;

// Board-owned diagnostics mode for validating physical sensor coverage.
class BoardDiagnostics {
 public:
  explicit BoardDiagnostics(BoardSystem* system, BoardLayering* layering = nullptr);

  void begin();
  void update();
  bool isComplete() const { return complete_; }

 private:
  BoardSystem* system_;
  BoardLayering* layering_;
  bool visited_[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS];
  bool complete_;
  uint8_t visitedCount_;

  void clearVisited();
  void recordCurrentOccupancy();
  void recordChangedOccupancy();
  void recordVisitedSquare(int row, int col);
  void showVisitedSquares();
};

#endif  // BOARD_DIAGNOSTICS_H
