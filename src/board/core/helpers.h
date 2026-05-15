#ifndef BOARD_CORE_HELPERS_H
#define BOARD_CORE_HELPERS_H

class BoardCanvas;
struct BoardCanvasHandle;

// ---------------------------------------------------------------------------
// BoardHelpers — shared logical board dimensions and coordinate helpers.
// ---------------------------------------------------------------------------
// The physical board, LED canvas, input queue, menus, and board-owned DTOs all
// describe the same 8x8 display grid. Keeping those dimensions here avoids
// parallel magic constants across the board subsystem.
// ---------------------------------------------------------------------------

namespace BoardHelpers {
constexpr int ROWS = 8;
constexpr int COLS = 8;
constexpr int SQUARES = ROWS * COLS;
constexpr int LAST_ROW = ROWS - 1;
constexpr int LAST_COL = COLS - 1;

inline constexpr bool inBounds(int row, int col) {
  return row >= 0 && row < ROWS && col >= 0 && col < COLS;
}

inline constexpr int bitOf(int row, int col) { return row * COLS + col; }
}  // namespace BoardHelpers

// ---------------------------------------------------------------------------
// BoardSurface — helpers for visual owners with one retained surface.
// ---------------------------------------------------------------------------
// Several visual/workflow classes store a BoardCanvasHandle and use the same
// lifecycle: acquire lazily, bring the surface to the front before painting,
// and clear/release it only when the handle still refers to an active surface.
// Keeping that boilerplate here makes those visual classes read as workflows.
// ---------------------------------------------------------------------------

namespace BoardSurface {

BoardCanvasHandle writable(BoardCanvas& canvas, BoardCanvasHandle& surface);
void clear(BoardCanvas& canvas, BoardCanvasHandle surface);
void clearSquare(BoardCanvas& canvas, BoardCanvasHandle surface, int row, int col);
void release(BoardCanvas& canvas, BoardCanvasHandle& surface);

}  // namespace BoardSurface

#endif  // BOARD_CORE_HELPERS_H