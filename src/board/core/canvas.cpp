#include "board/core/canvas.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// BoardCanvas implementation
// ---------------------------------------------------------------------------
// Layer presence is stored as a single `uint64_t` per layer (one bit per
// square, row-major). This makes whole-layer queries (`clearLayer`,
// "is anything painted") O(1) and removes the per-square scan loop the
// previous bool-array implementation needed.
//
// The canvas is stateless beyond pixel arrays + presence masks + a single
// dirty bit. No heap allocation. No driver access. No synchronization.
// ---------------------------------------------------------------------------

namespace {

inline void normalizeRange(int& a, int& b) {
  if (a > b) {
    int t = a;
    a = b;
    b = t;
  }
}

}  // namespace

BoardCanvas::BoardCanvas() : dirty_(true) {
  // Zero-initialize: no pixel is present, all colors black.
  memset(pixels_, 0, sizeof(pixels_));
  memset(presentMask_, 0, sizeof(presentMask_));
}

// ---------------------------------------------------------------------------
// Pixel writes
// ---------------------------------------------------------------------------

void BoardCanvas::setPixel(BoardLayer layer, int row, int col, LedRGB color) {
  if (!inBounds(row, col)) return;
  const uint8_t li = layerIndex(layer);
  pixels_[li][row][col] = color;
  presentMask_[li] |= bitMask(row, col);
  dirty_ = true;
}

void BoardCanvas::clearLayerSquare(BoardLayer layer, int row, int col) {
  if (!inBounds(row, col)) return;
  const uint8_t li = layerIndex(layer);
  const uint64_t bit = bitMask(row, col);
  if (presentMask_[li] & bit) {
    presentMask_[li] &= ~bit;
    pixels_[li][row][col] = LedColors::Off;
    dirty_ = true;
  }
}

void BoardCanvas::clearLayer(BoardLayer layer) {
  const uint8_t li = layerIndex(layer);
  // The mask is the "any pixel present" check — no scan needed.
  if (!presentMask_[li]) return;
  presentMask_[li] = 0;
  memset(pixels_[li], 0, sizeof(pixels_[li]));
  dirty_ = true;
}

void BoardCanvas::clearAll() {
  // Force dirty so the renderer flushes a black frame at least once even if
  // every layer was already empty (caller intent is "blank everything now").
  memset(pixels_, 0, sizeof(pixels_));
  memset(presentMask_, 0, sizeof(presentMask_));
  dirty_ = true;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void BoardCanvas::drawRect(BoardLayer layer, int r0, int c0, int r1, int c1, LedRGB color) {
  normalizeRange(r0, r1);
  normalizeRange(c0, c1);
  for (int c = c0; c <= c1; ++c) {
    setPixel(layer, r0, c, color);
    setPixel(layer, r1, c, color);
  }
  for (int r = r0; r <= r1; ++r) {
    setPixel(layer, r, c0, color);
    setPixel(layer, r, c1, color);
  }
}

void BoardCanvas::fillRect(BoardLayer layer, int r0, int c0, int r1, int c1, LedRGB color) {
  normalizeRange(r0, r1);
  normalizeRange(c0, c1);
  for (int r = r0; r <= r1; ++r) {
    for (int c = c0; c <= c1; ++c) {
      setPixel(layer, r, c, color);
    }
  }
}

void BoardCanvas::fillAll(BoardLayer layer, LedRGB color) {
  fillRect(layer, 0, 0, ROWS - 1, COLS - 1, color);
}

void BoardCanvas::drawLine(BoardLayer layer, int r0, int c0, int r1, int c1, LedRGB color) {
  // Bresenham's line algorithm. Works for any slope, including degenerate
  // (single-pixel and pure horizontal/vertical) cases.
  const int dr = abs(r1 - r0);
  const int dc = abs(c1 - c0);
  const int sr = (r0 < r1) ? 1 : -1;
  const int sc = (c0 < c1) ? 1 : -1;
  int err = dc - dr;
  while (true) {
    setPixel(layer, r0, c0, color);
    if (r0 == r1 && c0 == c1) break;
    const int e2 = 2 * err;
    if (e2 > -dr) {
      err -= dr;
      c0 += sc;
    }
    if (e2 < dc) {
      err += dc;
      r0 += sr;
    }
  }
}

void BoardCanvas::drawRing(BoardLayer layer, float centerRow, float centerCol,
                           float radius, float halfWidth, LedRGB color) {
  // Brute-force the 64-cell grid: for an 8x8 board this is faster and
  // simpler than an integer midpoint algorithm. Compare squared distances
  // so render-time rings avoid a sqrtf per cell.
  const float inner = radius - halfWidth;
  const float outer = radius + halfWidth;
  const float innerSq = inner * inner;
  const float outerSq = outer * outer;
  for (int row = 0; row < ROWS; ++row) {
    for (int col = 0; col < COLS; ++col) {
      const float dr = static_cast<float>(row) - centerRow;
      const float dc = static_cast<float>(col) - centerCol;
      const float distSq = dr * dr + dc * dc;
      if (distSq < outerSq && (inner <= 0.0f || distSq > innerSq)) {
        setPixel(layer, row, col, color);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Composition + queries
// ---------------------------------------------------------------------------

void BoardCanvas::compose(LedRGB out[ROWS][COLS]) {
  for (int r = 0; r < ROWS; ++r) {
    for (int c = 0; c < COLS; ++c) {
      out[r][c] = resolve(r, c);
    }
  }
  dirty_ = false;
}

LedRGB BoardCanvas::resolve(int row, int col) const {
  if (!inBounds(row, col)) return LedColors::Off;
  const uint64_t bit = bitMask(row, col);
  // Walk layers top-down so the first present pixel wins.
  for (int li = BOARD_LAYER_COUNT - 1; li >= 0; --li) {
    if (presentMask_[li] & bit) return pixels_[li][row][col];
  }
  return LedColors::Off;
}

bool BoardCanvas::hasPixel(int row, int col) const {
  if (!inBounds(row, col)) return false;
  const uint64_t bit = bitMask(row, col);
  for (int li = 0; li < BOARD_LAYER_COUNT; ++li) {
    if (presentMask_[li] & bit) return true;
  }
  return false;
}

bool BoardCanvas::layerHas(BoardLayer layer, int row, int col) const {
  if (!inBounds(row, col)) return false;
  return (presentMask_[layerIndex(layer)] & bitMask(row, col)) != 0;
}
