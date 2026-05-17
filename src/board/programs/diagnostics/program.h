#ifndef BOARD_PROGRAMS_DIAGNOSTICS_PROGRAM_H
#define BOARD_PROGRAMS_DIAGNOSTICS_PROGRAM_H

#include "board/runtime/helpers.h"
#include "board/services/program/program.h"
#include "board/services/visual/visual.h"

#include <stdint.h>

class BoardRuntime;
class BoardAnimations;

// ---------------------------------------------------------------------------
// BoardDiagnostics — physical sensor coverage program.
// ---------------------------------------------------------------------------
// Lights every square on an owned surface as the user places pieces.
// When all 64 squares have been visited, the surface is cleared and a
// firework animation plays.
// ---------------------------------------------------------------------------

class BoardDiagnostics final : public BoardProgram, private BoardVisual {
 public:
  static constexpr int ROWS = BoardHelpers::ROWS;
  static constexpr int COLS = BoardHelpers::COLS;
  static constexpr int SQUARES = BoardHelpers::SQUARES;

  BoardDiagnostics(BoardRuntime& runtime, BoardAnimations& animations);

  void begin() override;
  void update() override;
  void cancel() override;
  bool isComplete() const override { return complete_; }

 private:
  BoardAnimations& animations_;
  bool visited_[ROWS][COLS];
  bool complete_;
  uint8_t visitedCount_;

  void clearVisited();
  void recordCurrentOccupancy();
  void recordVisitedSquare(int row, int col);
  void paintVisited();
};

#endif  // BOARD_PROGRAMS_DIAGNOSTICS_PROGRAM_H
