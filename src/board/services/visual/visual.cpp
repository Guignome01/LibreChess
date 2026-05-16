#include "board/services/visual/visual.h"

#include "board/runtime/helpers.h"
#include "board/runtime/runtime.h"

BoardVisual::BoardVisual(BoardRuntime& runtime) : runtime_(runtime), surface_() {}

BoardCanvasHandle BoardVisual::writableSurface(BoardCanvas& canvas) {
  return BoardSurface::writable(canvas, surface_);
}

void BoardVisual::clearSurface(BoardCanvas& canvas) {
  BoardSurface::clear(canvas, surface_);
}

void BoardVisual::clearSurface() {
  auto guard = runtime_.lockCanvas();
  clearSurface(guard.canvas);
}

void BoardVisual::clearSquare(BoardCanvas& canvas, int row, int col) {
  BoardSurface::clearSquare(canvas, surface_, row, col);
}

void BoardVisual::clearSquare(int row, int col) {
  auto guard = runtime_.lockCanvas();
  clearSquare(guard.canvas, row, col);
}

void BoardVisual::releaseSurface(BoardCanvas& canvas) {
  BoardSurface::release(canvas, surface_);
}

void BoardVisual::releaseSurface() {
  auto guard = runtime_.lockCanvas();
  releaseSurface(guard.canvas);
}