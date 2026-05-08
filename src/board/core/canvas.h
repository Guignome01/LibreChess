#ifndef BOARD_CANVAS_H
#define BOARD_CANVAS_H

#include "board/core/colors.h"
#include "board/gui/layers.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// BoardCanvas — multi-layer 8x8 pixel surface
// ---------------------------------------------------------------------------
// The canvas is the single source of truth for what should be displayed on
// the LED matrix. Every visual subsystem (workflows, effects, menus,
// feedback) writes into a specific BoardLayer; the canvas composes all
// layers into a single 8x8 frame on demand.
//
// Composition rule: for each square, the TOP-MOST layer whose presence bit
// is set wins. Within a single layer, the LAST write wins.
//
// Storage: per-layer per-pixel `LedRGB` plus one `uint64_t` presence mask
// per layer (one bit per square, row-major). The bitmask makes
// `clearLayer`, `hasPixel`, and `layerHas` O(1) and removes the need for a
// per-square scan to detect "is anything painted on this layer". Total
// state is ~1.4 KiB per canvas — comfortably small for an ESP32.
//
// The canvas itself is NOT thread-safe. Callers (workflows, the renderer
// task) must serialize access through the LED mutex owned by BoardRuntime
// (acquired via `BoardRuntime::lockCanvas()`).
// ---------------------------------------------------------------------------

class BoardCanvas {
 public:
  static constexpr int ROWS = 8;
  static constexpr int COLS = 8;

  BoardCanvas();

  BoardCanvas(const BoardCanvas&) = delete;
  BoardCanvas& operator=(const BoardCanvas&) = delete;

  // -------------------------------------------------------------------------
  // Pixel writes
  // -------------------------------------------------------------------------

  /// Set one pixel on the given layer. Out-of-bounds coordinates are silently
  /// ignored. Marks the canvas dirty for the renderer's next flush.
  void setPixel(BoardLayer layer, int row, int col, LedRGB color);

  /// Clear a single square on the given layer (removes its presence bit).
  void clearLayerSquare(BoardLayer layer, int row, int col);

  /// Clear an entire layer (no pixels present).
  void clearLayer(BoardLayer layer);

  /// Clear every layer.
  void clearAll();

  // -------------------------------------------------------------------------
  // Drawing helpers (all paint into a single chosen layer)
  // -------------------------------------------------------------------------

  /// Paint a rectangle outline.
  void drawRect(BoardLayer layer, int r0, int c0, int r1, int c1, LedRGB color);

  /// Paint a filled rectangle.
  void fillRect(BoardLayer layer, int r0, int c0, int r1, int c1, LedRGB color);

  /// Paint every square on the given layer with one color.
  void fillAll(BoardLayer layer, LedRGB color);

  /// Paint a straight line between two squares using Bresenham's algorithm.
  /// Endpoints are integer cell coordinates and are inclusive.
  void drawLine(BoardLayer layer, int r0, int c0, int r1, int c1, LedRGB color);

  /// Paint a ring centered at fractional cell coordinates. A square is
  /// painted when its distance to the center is within `halfWidth` of
  /// `radius`. Use halfWidth ≈ 0.5 for a one-pixel-thick ring; larger
  /// values give thicker bands. Fractional centers (e.g. 3.5, 3.5) place
  /// the ring between cells, which is the natural choice for board-center
  /// effects like fireworks.
  void drawRing(BoardLayer layer, float centerRow, float centerCol,
                float radius, float halfWidth, LedRGB color);

  // -------------------------------------------------------------------------
  // Composition + queries
  // -------------------------------------------------------------------------

  /// Returns true if any layer changed since the last `compose()` call.
  bool dirty() const { return dirty_; }

  /// Resolve the per-square top-most-present color into `out`. Squares with
  /// no presence on any layer become LedColors::Off. Clears the dirty flag.
  void compose(LedRGB out[ROWS][COLS]);

  /// Query the resolved color of one square without composing the full board.
  /// Useful for tests and lightweight inspection. Does NOT clear the dirty flag.
  LedRGB resolve(int row, int col) const;

  /// Return true if any layer claims presence on the given square.
  bool hasPixel(int row, int col) const;

  /// Return true if a specific layer has presence at a square.
  bool layerHas(BoardLayer layer, int row, int col) const;

 private:
  static constexpr bool inBounds(int row, int col) {
    return row >= 0 && row < ROWS && col >= 0 && col < COLS;
  }
  static constexpr int bitOf(int row, int col) { return row * COLS + col; }
  static constexpr uint64_t bitMask(int row, int col) {
    return uint64_t{1} << bitOf(row, col);
  }

  LedRGB pixels_[BOARD_LAYER_COUNT][ROWS][COLS];
  uint64_t presentMask_[BOARD_LAYER_COUNT];
  bool dirty_;
};

#endif  // BOARD_CANVAS_H
