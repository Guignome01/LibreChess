#include "board/core/canvas.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// BoardCanvas implementation
// ---------------------------------------------------------------------------
// Surface presence is stored as a single `uint64_t` per surface (one bit per
// square, row-major). Surface order is explicit and monotonically increasing;
// composition chooses the newest active surface that has a pixel on a square.
//
// The canvas is stateless beyond fixed surface slots + a single dirty bit. No
// heap allocation. No driver access. No synchronization.
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

BoardCanvas::BoardCanvas() : nextGeneration_(1), nextOrder_(1), dirty_(true) {
  // Zero-initialize: no surface is active, no pixel is present, all colors black.
  memset(surfaces_, 0, sizeof(surfaces_));
}

// ---------------------------------------------------------------------------
// Surface lifecycle
// ---------------------------------------------------------------------------

BoardCanvas::Surface* BoardCanvas::surfaceFor(BoardCanvasHandle handle) {
  if (!handle.valid() || handle.slot >= SURFACE_COUNT) return nullptr;
  Surface& surface = surfaces_[handle.slot];
  if (!surface.active || surface.generation != handle.generation) return nullptr;
  return &surface;
}

const BoardCanvas::Surface* BoardCanvas::surfaceFor(BoardCanvasHandle handle) const {
  if (!handle.valid() || handle.slot >= SURFACE_COUNT) return nullptr;
  const Surface& surface = surfaces_[handle.slot];
  if (!surface.active || surface.generation != handle.generation) return nullptr;
  return &surface;
}

BoardCanvasHandle BoardCanvas::acquireSurface() {
  for (uint8_t i = 0; i < SURFACE_COUNT; ++i) {
    Surface& surface = surfaces_[i];
    if (surface.active) continue;

    if (nextGeneration_ == 0) nextGeneration_ = 1;
    surface.generation = nextGeneration_++;
    surface.presentMask = 0;
    surface.order = nextOrder_++;
    surface.active = true;
    memset(surface.pixels, 0, sizeof(surface.pixels));
    dirty_ = true;
    return BoardCanvasHandle{i, surface.generation};
  }
  return BoardCanvasHandle{};
}

void BoardCanvas::releaseSurface(BoardCanvasHandle& handle) {
  Surface* surface = surfaceFor(handle);
  if (surface == nullptr) {
    handle = BoardCanvasHandle{};
    return;
  }

  if (surface->presentMask != 0) dirty_ = true;
  memset(surface->pixels, 0, sizeof(surface->pixels));
  surface->presentMask = 0;
  surface->order = 0;
  surface->generation = 0;
  surface->active = false;

  handle = BoardCanvasHandle{};
}

bool BoardCanvas::active(BoardCanvasHandle handle) const { return surfaceFor(handle) != nullptr; }

void BoardCanvas::bringToFront(BoardCanvasHandle handle) {
  Surface* surface = surfaceFor(handle);
  if (surface == nullptr) return;
  surface->order = nextOrder_++;
  if (surface->presentMask != 0) dirty_ = true;
}

// ---------------------------------------------------------------------------
// Pixel writes
// ---------------------------------------------------------------------------

void BoardCanvas::setPixel(BoardCanvasHandle surfaceHandle, int row, int col, LedRGB color) {
  if (!inBounds(row, col)) return;
  Surface* surface = surfaceFor(surfaceHandle);
  if (surface == nullptr) return;
  surface->pixels[row][col] = color;
  surface->presentMask |= bitMask(row, col);
  dirty_ = true;
}

void BoardCanvas::clearSurfaceSquare(BoardCanvasHandle surfaceHandle, int row, int col) {
  if (!inBounds(row, col)) return;
  Surface* surface = surfaceFor(surfaceHandle);
  if (surface == nullptr) return;
  const uint64_t bit = bitMask(row, col);
  if (surface->presentMask & bit) {
    surface->presentMask &= ~bit;
    surface->pixels[row][col] = LedColors::Off;
    dirty_ = true;
  }
}

void BoardCanvas::clearSurface(BoardCanvasHandle surfaceHandle) {
  Surface* surface = surfaceFor(surfaceHandle);
  if (surface == nullptr || surface->presentMask == 0) return;
  surface->presentMask = 0;
  memset(surface->pixels, 0, sizeof(surface->pixels));
  dirty_ = true;
}

void BoardCanvas::clearAll() {
  // Force dirty so the renderer flushes a black frame at least once even if
  // every surface was already empty (caller intent is "blank everything now").
  for (uint8_t i = 0; i < SURFACE_COUNT; ++i) {
    surfaces_[i].presentMask = 0;
    memset(surfaces_[i].pixels, 0, sizeof(surfaces_[i].pixels));
  }
  dirty_ = true;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void BoardCanvas::drawRect(BoardCanvasHandle surface, int r0, int c0, int r1, int c1,
                           LedRGB color) {
  normalizeRange(r0, r1);
  normalizeRange(c0, c1);
  for (int c = c0; c <= c1; ++c) {
    setPixel(surface, r0, c, color);
    setPixel(surface, r1, c, color);
  }
  for (int r = r0; r <= r1; ++r) {
    setPixel(surface, r, c0, color);
    setPixel(surface, r, c1, color);
  }
}

void BoardCanvas::fillRect(BoardCanvasHandle surface, int r0, int c0, int r1, int c1,
                           LedRGB color) {
  normalizeRange(r0, r1);
  normalizeRange(c0, c1);
  for (int r = r0; r <= r1; ++r) {
    for (int c = c0; c <= c1; ++c) {
      setPixel(surface, r, c, color);
    }
  }
}

void BoardCanvas::fillAll(BoardCanvasHandle surface, LedRGB color) {
  fillRect(surface, 0, 0, ROWS - 1, COLS - 1, color);
}

void BoardCanvas::drawLine(BoardCanvasHandle surface, int r0, int c0, int r1, int c1,
                           LedRGB color) {
  // Bresenham's line algorithm. Works for any slope, including degenerate
  // (single-pixel and pure horizontal/vertical) cases.
  const int dr = abs(r1 - r0);
  const int dc = abs(c1 - c0);
  const int sr = (r0 < r1) ? 1 : -1;
  const int sc = (c0 < c1) ? 1 : -1;
  int err = dc - dr;
  while (true) {
    setPixel(surface, r0, c0, color);
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

void BoardCanvas::drawRing(BoardCanvasHandle surface, float centerRow, float centerCol,
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
        setPixel(surface, row, col, color);
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
  const Surface* newest = nullptr;
  for (uint8_t i = 0; i < SURFACE_COUNT; ++i) {
    const Surface& surface = surfaces_[i];
    if (!surface.active || !(surface.presentMask & bit)) continue;
    if (newest == nullptr || surface.order > newest->order) newest = &surface;
  }
  return newest != nullptr ? newest->pixels[row][col] : LedColors::Off;
}

bool BoardCanvas::hasPixel(int row, int col) const {
  if (!inBounds(row, col)) return false;
  const uint64_t bit = bitMask(row, col);
  for (uint8_t i = 0; i < SURFACE_COUNT; ++i) {
    const Surface& surface = surfaces_[i];
    if (surface.active && (surface.presentMask & bit)) return true;
  }
  return false;
}

bool BoardCanvas::surfaceHas(BoardCanvasHandle surfaceHandle, int row, int col) const {
  if (!inBounds(row, col)) return false;
  const Surface* surface = surfaceFor(surfaceHandle);
  return surface != nullptr && (surface->presentMask & bitMask(row, col)) != 0;
}
