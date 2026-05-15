#ifndef BOARD_WORKFLOWS_DIAGNOSTICS_H
#define BOARD_WORKFLOWS_DIAGNOSTICS_H

#include "board/core/canvas.h"
#include "board/core/helpers.h"

#include <stdint.h>

class BoardRuntime;
class BoardAnimations;

// ---------------------------------------------------------------------------
// BoardDiagnostics — physical sensor coverage workflow.
// ---------------------------------------------------------------------------
// Lights every square on an owned surface as the user places pieces.
// When all 64 squares have been visited, the surface is cleared and a
// firework animation plays.
// ---------------------------------------------------------------------------

class BoardDiagnostics {
 public:
  static constexpr int ROWS = BoardHelpers::ROWS;
  static constexpr int COLS = BoardHelpers::COLS;
  static constexpr int SQUARES = BoardHelpers::SQUARES;

  BoardDiagnostics(BoardRuntime& runtime, BoardAnimations& animations);

  void begin();
  void update();
  bool isComplete() const { return complete_; }

 private:
  BoardRuntime& runtime_;
  BoardAnimations& animations_;
  BoardCanvasHandle surface_;
  bool visited_[ROWS][COLS];
  bool complete_;
  uint8_t visitedCount_;

  void clearVisited();
  void recordCurrentOccupancy();
  void recordVisitedSquare(int row, int col);
  void paintVisited();
};

#endif  // BOARD_WORKFLOWS_DIAGNOSTICS_H
