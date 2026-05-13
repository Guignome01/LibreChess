#ifndef BOARD_CANVAS_H
#define BOARD_CANVAS_H

#include "board/core/colors.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// BoardCanvas — ordered 8x8 pixel surfaces
// ---------------------------------------------------------------------------
// The canvas is the single source of truth for what should be displayed on
// the LED matrix. Visual subsystems write into small fixed-size surfaces;
// the canvas composes all active surfaces into a single 8x8 frame on demand.
//
// Composition rule: surfaces are drawn oldest-to-newest and the newest
// surface with a presence bit wins for that square. Within a single surface,
// the last write wins.
//
// Storage: each surface owns an 8x8 `LedRGB` array plus one `uint64_t`
// presence mask (one bit per square, row-major). Capacity is fixed so the
// renderer never allocates heap memory while still allowing persistent board
// visuals plus all scheduled animation slots to coexist.
//
// The canvas itself is NOT thread-safe. Callers (workflows, the renderer
// task) must serialize access through the LED mutex owned by BoardRuntime
// (acquired via `BoardRuntime::lockCanvas()`).
// ---------------------------------------------------------------------------

struct BoardCanvasHandle {
  static constexpr uint8_t INVALID_SLOT = 0xFF;
  uint8_t slot = INVALID_SLOT;
  uint16_t generation = 0;
  bool valid() const { return slot != INVALID_SLOT; }
};

class BoardCanvas {
 public:
  static constexpr int ROWS = 8;
  static constexpr int COLS = 8;
  static constexpr uint8_t SURFACE_COUNT = 16;

  BoardCanvas();

  BoardCanvas(const BoardCanvas&) = delete;
  BoardCanvas& operator=(const BoardCanvas&) = delete;

  // -------------------------------------------------------------------------
  // Surface lifecycle
  // -------------------------------------------------------------------------

  /// Acquire a new empty surface appended above older active surfaces.
  /// Returns an invalid handle when the fixed surface pool is full.
  BoardCanvasHandle acquireSurface();

  /// Release a previously acquired surface and invalidate the caller handle.
  void releaseSurface(BoardCanvasHandle& handle);

  /// Return true iff the handle references a live surface.
  bool active(BoardCanvasHandle handle) const;

  /// Move a live surface above all older active surfaces.
  void bringToFront(BoardCanvasHandle handle);

  // -------------------------------------------------------------------------
  // Pixel writes
  // -------------------------------------------------------------------------

  /// Set one pixel on a surface. Out-of-bounds coordinates and stale handles
  /// are ignored. Marks the canvas dirty for the renderer's next flush.
  void setPixel(BoardCanvasHandle surface, int row, int col, LedRGB color);

  /// Clear a single square on a surface.
  void clearSurfaceSquare(BoardCanvasHandle surface, int row, int col);

  /// Clear all pixels owned by one surface without releasing its handle.
  void clearSurface(BoardCanvasHandle surface);

  /// Clear every active surface while preserving acquired handles.
  void clearAll();

  // -------------------------------------------------------------------------
  // Drawing helpers (all paint into a single chosen surface)
  // -------------------------------------------------------------------------

  /// Paint a rectangle outline on a surface.
  void drawRect(BoardCanvasHandle surface, int r0, int c0, int r1, int c1,
                LedRGB color);

  /// Paint a filled rectangle on a surface.
  void fillRect(BoardCanvasHandle surface, int r0, int c0, int r1, int c1,
                LedRGB color);

  /// Paint every square on a surface with one color.
  void fillAll(BoardCanvasHandle surface, LedRGB color);

  /// Paint a straight line between two squares using Bresenham's algorithm.
  /// Endpoints are integer cell coordinates and are inclusive.
  void drawLine(BoardCanvasHandle surface, int r0, int c0, int r1, int c1,
                LedRGB color);

  /// Paint a ring centered at fractional cell coordinates. A square is
  /// painted when its distance to the center is within `halfWidth` of
  /// `radius`. Use halfWidth ≈ 0.5 for a one-pixel-thick ring; larger
  /// values give thicker bands. Fractional centers (e.g. 3.5, 3.5) place
  /// the ring between cells, which is the natural choice for board-center
  /// animations like fireworks.
  void drawRing(BoardCanvasHandle surface, float centerRow, float centerCol,
                float radius, float halfWidth, LedRGB color);

  // -------------------------------------------------------------------------
  // Composition + queries
  // -------------------------------------------------------------------------

  /// Returns true if any surface changed since the last `compose()` call.
  bool dirty() const { return dirty_; }

  /// Resolve the per-square newest-present color into `out`. Squares with no
  /// presence on any surface become LedColors::Off. Clears the dirty flag.
  void compose(LedRGB out[ROWS][COLS]);

  /// Query the resolved color of one square without composing the full board.
  /// Useful for tests and lightweight inspection. Does NOT clear the dirty flag.
  LedRGB resolve(int row, int col) const;

  /// Return true if any surface claims presence on the given square.
  bool hasPixel(int row, int col) const;

  /// Return true if a specific surface has presence at a square.
  bool surfaceHas(BoardCanvasHandle surface, int row, int col) const;

 private:
  static constexpr bool inBounds(int row, int col) {
    return row >= 0 && row < ROWS && col >= 0 && col < COLS;
  }
  static constexpr int bitOf(int row, int col) { return row * COLS + col; }
  static constexpr uint64_t bitMask(int row, int col) {
    return uint64_t{1} << bitOf(row, col);
  }

  struct Surface {
    LedRGB pixels[ROWS][COLS];
    uint64_t presentMask;
    uint32_t order;
    uint16_t generation;
    bool active;
  };

  Surface* surfaceFor(BoardCanvasHandle handle);
  const Surface* surfaceFor(BoardCanvasHandle handle) const;

  Surface surfaces_[SURFACE_COUNT];
  uint16_t nextGeneration_;
  uint32_t nextOrder_;
  bool dirty_;
};

#endif  // BOARD_CANVAS_H
