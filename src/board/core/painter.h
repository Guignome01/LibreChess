#ifndef BOARD_CORE_PAINTER_H
#define BOARD_CORE_PAINTER_H

#include "board/core/canvas.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// BoardPainter — standalone logical-frame painter contract
// ---------------------------------------------------------------------------
// A painter converts caller-owned visual state into pixels on a BoardCanvas.
// It does not own timing, cancellation, hardware, or LED flushing. Schedulers
// and renderers can run painters without knowing what visual domain produced
// them (animation, menu feedback, setup prompt, diagnostics, ...).
// ---------------------------------------------------------------------------

enum class BoardPaintMode : uint8_t {
  INCREMENTAL,   ///< Painter updates/cleans only the pixels it owns.
  FULL_SURFACE,  ///< Scheduler clears the painter-owned surface before each frame.
};

using BoardPaintCallback = void (*)(const void* context, BoardCanvas& canvas,
                                    BoardCanvasHandle surface, uint32_t elapsedMs);
using BoardPaintCleanup = void (*)(const void* context, BoardCanvas& canvas,
                                   BoardCanvasHandle surface);

struct BoardPainter {
  const void* context = nullptr;
  uint8_t contextSize = 0;
  BoardPaintCallback paint = nullptr;
  BoardPaintCleanup cleanup = nullptr;
  BoardPaintMode mode = BoardPaintMode::INCREMENTAL;
};

#endif  // BOARD_CORE_PAINTER_H
