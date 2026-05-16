#include "board/runtime/helpers.h"

#include "board/runtime/canvas.h"

namespace BoardSurface {

BoardCanvasHandle writable(BoardCanvas& canvas, BoardCanvasHandle& surface) {
  if (!canvas.active(surface)) {
    surface = canvas.acquireSurface();
  }
  canvas.bringToFront(surface);
  return surface;
}

void clear(BoardCanvas& canvas, BoardCanvasHandle surface) {
  if (canvas.active(surface)) canvas.clearSurface(surface);
}

void clearSquare(BoardCanvas& canvas, BoardCanvasHandle surface, int row, int col) {
  if (canvas.active(surface)) canvas.clearSurfaceSquare(surface, row, col);
}

void release(BoardCanvas& canvas, BoardCanvasHandle& surface) {
  canvas.releaseSurface(surface);
}

}  // namespace BoardSurface