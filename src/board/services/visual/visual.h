#ifndef BOARD_SERVICES_VISUAL_VISUAL_H
#define BOARD_SERVICES_VISUAL_VISUAL_H

#include "board/runtime/canvas.h"

class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardVisual — retained-surface base for board visuals.
// ---------------------------------------------------------------------------
// Visual owners share one canvas-surface lifecycle: acquire lazily, bring the
// surface forward before painting, clear squares safely, and release on cancel
// when appropriate. Drawing policy stays in concrete visuals.
// ---------------------------------------------------------------------------

class BoardVisual {
 public:
  BoardVisual(const BoardVisual&) = delete;
  BoardVisual& operator=(const BoardVisual&) = delete;
  virtual ~BoardVisual() = default;

 protected:
  explicit BoardVisual(BoardRuntime& runtime);

  BoardCanvasHandle writableSurface(BoardCanvas& canvas);
  void clearSurface(BoardCanvas& canvas);
  void clearSurface();
  void clearSquare(BoardCanvas& canvas, int row, int col);
  void clearSquare(int row, int col);
  void releaseSurface(BoardCanvas& canvas);
  void releaseSurface();

  BoardRuntime& runtime_;

 private:
  BoardCanvasHandle surface_;
};

#endif  // BOARD_SERVICES_VISUAL_VISUAL_H