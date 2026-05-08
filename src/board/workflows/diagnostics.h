#ifndef BOARD_WORKFLOWS_DIAGNOSTICS_H
#define BOARD_WORKFLOWS_DIAGNOSTICS_H

#include <stdint.h>

class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardDiagnostics — physical sensor coverage workflow.
// ---------------------------------------------------------------------------
// Lights every square on BoardLayer::GAME as the user places pieces. When
// all 64 squares have been visited, the GAME layer is cleared and a
// firework effect plays.
// ---------------------------------------------------------------------------

class BoardDiagnostics {
 public:
  static constexpr int ROWS = 8;
  static constexpr int COLS = 8;
  static constexpr int SQUARES = ROWS * COLS;

  explicit BoardDiagnostics(BoardRuntime& runtime);

  void begin();
  void update();
  bool isComplete() const { return complete_; }

 private:
  BoardRuntime& runtime_;
  bool visited_[ROWS][COLS];
  bool complete_;
  uint8_t visitedCount_;

  void clearVisited();
  void recordCurrentOccupancy();
  void recordVisitedSquare(int row, int col);
  void paintVisited();
};

#endif  // BOARD_WORKFLOWS_DIAGNOSTICS_H
