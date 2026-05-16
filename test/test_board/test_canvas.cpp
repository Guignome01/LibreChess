// Tests for BoardCanvas (ordered 8x8 pixel surfaces).
//
// The canvas is pure logic: no Arduino, no driver, no threading. We verify
// insertion-order composition, presence semantics, dirty tracking, and bounds safety.
//
// The .cpp is included directly so the native test environment doesn't need
// to compile src/ as a project source set.

#include <unity.h>

#include "board/runtime/canvas.h"
#include "board/runtime/helpers.h"

namespace {

bool sameColor(LedRGB a, LedRGB b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

LedRGB rgb(uint8_t r, uint8_t g, uint8_t b) { return LedRGB{r, g, b}; }

BoardCanvasHandle acquireTestSurface(BoardCanvas& canvas) {
  BoardCanvasHandle surface = canvas.acquireSurface();
  TEST_ASSERT_TRUE(surface.valid());
  return surface;
}

// ---------------------------------------------------------------------------
// Construction + initial state
// ---------------------------------------------------------------------------

void test_fresh_canvas_has_no_pixels() {
  BoardCanvas canvas;
  for (int r = 0; r < BoardCanvas::ROWS; ++r) {
    for (int c = 0; c < BoardCanvas::COLS; ++c) {
      TEST_ASSERT_FALSE(canvas.hasPixel(r, c));
      TEST_ASSERT_TRUE(sameColor(canvas.resolve(r, c), LedColors::Off));
    }
  }
}

void test_fresh_canvas_is_dirty() {
  // Initial state forces one flush so the renderer paints a known frame.
  BoardCanvas canvas;
  TEST_ASSERT_TRUE(canvas.dirty());
}

// ---------------------------------------------------------------------------
// Single-surface writes
// ---------------------------------------------------------------------------

void test_setPixel_makes_surface_present() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.setPixel(surface, 2, 3, rgb(10, 20, 30));
  TEST_ASSERT_TRUE(canvas.surfaceHas(surface, 2, 3));
  TEST_ASSERT_TRUE(canvas.hasPixel(2, 3));
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(2, 3), rgb(10, 20, 30)));
}

void test_setPixel_last_write_wins_within_surface() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.setPixel(surface, 0, 0, LedColors::Red);
  canvas.setPixel(surface, 0, 0, LedColors::Green);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(0, 0), LedColors::Green));
}

// ---------------------------------------------------------------------------
// Surface composition (newest wins)
// ---------------------------------------------------------------------------

void test_newer_surface_write_wins_over_earlier_write() {
  BoardCanvas canvas;
  BoardCanvasHandle earlier = acquireTestSurface(canvas);
  BoardCanvasHandle newer = acquireTestSurface(canvas);
  canvas.setPixel(earlier, 4, 4, LedColors::Red);
  canvas.setPixel(newer, 4, 4, LedColors::Green);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(4, 4), LedColors::Green));
}

void test_earlier_surface_visible_when_later_absent() {
  BoardCanvas canvas;
  BoardCanvasHandle earlier = acquireTestSurface(canvas);
  (void)acquireTestSurface(canvas);
  canvas.setPixel(earlier, 5, 5, LedColors::Red);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(5, 5), LedColors::Red));
}

void test_clearing_newer_surface_reveals_older_surface() {
  BoardCanvas canvas;
  BoardCanvasHandle earlier = acquireTestSurface(canvas);
  BoardCanvasHandle newer = acquireTestSurface(canvas);
  canvas.setPixel(earlier, 1, 1, LedColors::Red);
  canvas.setPixel(newer, 1, 1, LedColors::Green);
  canvas.clearSurfaceSquare(newer, 1, 1);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(1, 1), LedColors::Red));
}

void test_surface_write_order_follows_acquisition_order() {
  BoardCanvas canvas;
  BoardCanvasHandle first = acquireTestSurface(canvas);
  BoardCanvasHandle second = acquireTestSurface(canvas);
  canvas.setPixel(second, 2, 2, LedColors::Green);
  canvas.setPixel(first, 2, 2, LedColors::Red);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(2, 2), LedColors::Green));
}

void test_bringToFront_updates_surface_order() {
  BoardCanvas canvas;
  BoardCanvasHandle first = acquireTestSurface(canvas);
  BoardCanvasHandle second = acquireTestSurface(canvas);
  canvas.setPixel(second, 3, 3, LedColors::Green);
  canvas.setPixel(first, 3, 3, LedColors::Red);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(3, 3), LedColors::Green));
  canvas.bringToFront(first);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(3, 3), LedColors::Red));
}

void test_clearSurface_does_not_affect_other_surfaces() {
  BoardCanvas canvas;
  BoardCanvasHandle first = acquireTestSurface(canvas);
  BoardCanvasHandle second = acquireTestSurface(canvas);
  canvas.setPixel(first, 0, 0, LedColors::Red);
  canvas.setPixel(second, 0, 0, LedColors::Yellow);
  canvas.clearSurface(second);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(0, 0), LedColors::Red));
  TEST_ASSERT_FALSE(canvas.surfaceHas(second, 0, 0));
  TEST_ASSERT_TRUE(canvas.surfaceHas(first, 0, 0));
}

// ---------------------------------------------------------------------------
// Dirty flag tracking
// ---------------------------------------------------------------------------

void test_compose_clears_dirty_flag() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.setPixel(surface, 0, 0, LedColors::Red);
  TEST_ASSERT_TRUE(canvas.dirty());
  LedRGB out[BoardCanvas::ROWS][BoardCanvas::COLS];
  canvas.compose(out);
  TEST_ASSERT_FALSE(canvas.dirty());
}

void test_setPixel_marks_dirty() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  LedRGB out[BoardCanvas::ROWS][BoardCanvas::COLS];
  canvas.compose(out);  // Drains initial/acquire dirty bits.
  TEST_ASSERT_FALSE(canvas.dirty());
  canvas.setPixel(surface, 0, 0, LedColors::Red);
  TEST_ASSERT_TRUE(canvas.dirty());
}

void test_clearSurface_no_op_does_not_dirty() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  LedRGB out[BoardCanvas::ROWS][BoardCanvas::COLS];
  canvas.compose(out);
  TEST_ASSERT_FALSE(canvas.dirty());
  canvas.clearSurface(surface);  // Already empty.
  TEST_ASSERT_FALSE(canvas.dirty());
}

void test_surface_helper_acquires_and_reuses_surface() {
  BoardCanvas canvas;
  BoardCanvasHandle surface;
  BoardCanvasHandle first = BoardSurface::writable(canvas, surface);
  TEST_ASSERT_TRUE(first.valid());
  TEST_ASSERT_TRUE(surface.valid());

  BoardCanvasHandle second = BoardSurface::writable(canvas, surface);
  TEST_ASSERT_EQUAL_UINT8(first.slot, second.slot);
  TEST_ASSERT_EQUAL_UINT16(first.generation, second.generation);
}

void test_surface_helper_clear_and_release_are_safe() {
  BoardCanvas canvas;
  BoardCanvasHandle surface;
  BoardSurface::writable(canvas, surface);
  canvas.setPixel(surface, 0, 0, LedColors::Red);

  BoardSurface::clearSquare(canvas, surface, 0, 0);
  TEST_ASSERT_FALSE(canvas.hasPixel(0, 0));

  canvas.setPixel(surface, 1, 1, LedColors::Green);
  BoardSurface::clear(canvas, surface);
  TEST_ASSERT_FALSE(canvas.hasPixel(1, 1));

  BoardSurface::release(canvas, surface);
  TEST_ASSERT_FALSE(surface.valid());
  BoardSurface::clear(canvas, surface);
}

// ---------------------------------------------------------------------------
// Compose output buffer
// ---------------------------------------------------------------------------

void test_compose_fills_buffer_with_resolved_colors() {
  BoardCanvas canvas;
  BoardCanvasHandle first = acquireTestSurface(canvas);
  BoardCanvasHandle second = acquireTestSurface(canvas);
  canvas.setPixel(first, 0, 0, LedColors::Red);
  canvas.setPixel(second, 7, 7, LedColors::Green);
  LedRGB out[8][8];
  canvas.compose(out);
  TEST_ASSERT_TRUE(sameColor(out[0][0], LedColors::Red));
  TEST_ASSERT_TRUE(sameColor(out[7][7], LedColors::Green));
  TEST_ASSERT_TRUE(sameColor(out[3][3], LedColors::Off));
}

// ---------------------------------------------------------------------------
// Bounds safety
// ---------------------------------------------------------------------------

void test_out_of_bounds_writes_silently_ignored() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.setPixel(surface, -1, 0, LedColors::Red);
  canvas.setPixel(surface, 0, 8, LedColors::Red);
  canvas.setPixel(surface, 8, 8, LedColors::Red);
  // Nothing should have been recorded; canvas is still empty.
  for (int r = 0; r < BoardCanvas::ROWS; ++r) {
    for (int c = 0; c < BoardCanvas::COLS; ++c) {
      TEST_ASSERT_FALSE(canvas.hasPixel(r, c));
    }
  }
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void test_fillRect_paints_inclusive_range() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.fillRect(surface, 1, 2, 3, 4, LedColors::Blue);
  for (int r = 1; r <= 3; ++r) {
    for (int c = 2; c <= 4; ++c) {
      TEST_ASSERT_TRUE(sameColor(canvas.resolve(r, c), LedColors::Blue));
    }
  }
  // Outside the rect: untouched.
  TEST_ASSERT_FALSE(canvas.hasPixel(0, 2));
  TEST_ASSERT_FALSE(canvas.hasPixel(4, 4));
  TEST_ASSERT_FALSE(canvas.hasPixel(2, 5));
}

void test_drawRect_paints_outline_only() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.drawRect(surface, 2, 2, 4, 4, LedColors::Red);
  // Corners + edges painted.
  TEST_ASSERT_TRUE(canvas.hasPixel(2, 2));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 4));
  TEST_ASSERT_TRUE(canvas.hasPixel(2, 3));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 3));
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 2));
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 4));
  // Interior: empty.
  TEST_ASSERT_FALSE(canvas.hasPixel(3, 3));
}

void test_fillAll_paints_every_square() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.fillAll(surface, LedColors::Cyan);
  for (int r = 0; r < BoardCanvas::ROWS; ++r) {
    for (int c = 0; c < BoardCanvas::COLS; ++c) {
      TEST_ASSERT_TRUE(sameColor(canvas.resolve(r, c), LedColors::Cyan));
    }
  }
}

void test_drawLine_horizontal() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.drawLine(surface, 3, 1, 3, 6, LedColors::Red);
  for (int c = 1; c <= 6; ++c) {
    TEST_ASSERT_TRUE(sameColor(canvas.resolve(3, c), LedColors::Red));
  }
  TEST_ASSERT_FALSE(canvas.hasPixel(3, 0));
  TEST_ASSERT_FALSE(canvas.hasPixel(3, 7));
  TEST_ASSERT_FALSE(canvas.hasPixel(2, 3));
}

void test_drawLine_vertical_reversed_endpoints() {
  // Bresenham must work regardless of endpoint order.
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.drawLine(surface, 5, 2, 1, 2, LedColors::Green);
  for (int r = 1; r <= 5; ++r) {
    TEST_ASSERT_TRUE(sameColor(canvas.resolve(r, 2), LedColors::Green));
  }
}

void test_drawLine_diagonal_paints_endpoints() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.drawLine(surface, 0, 0, 7, 7, LedColors::Blue);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(0, 0), LedColors::Blue));
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(7, 7), LedColors::Blue));
  // Each step must paint along the main diagonal.
  for (int i = 0; i < BoardCanvas::ROWS; ++i) {
    TEST_ASSERT_TRUE(canvas.hasPixel(i, i));
  }
}

void test_drawLine_single_pixel() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.drawLine(surface, 4, 4, 4, 4, LedColors::Yellow);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(4, 4), LedColors::Yellow));
}

void test_drawRing_centered_radius_one() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.drawRing(surface, 3.5f, 3.5f, 1.0f, 0.5f, LedColors::Red);
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 3));
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 4));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 3));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 4));
  // Corner squares are way outside.
  TEST_ASSERT_FALSE(canvas.hasPixel(0, 0));
  TEST_ASSERT_FALSE(canvas.hasPixel(7, 7));
}

void test_drawRing_zero_radius_paints_only_nearest_cells() {
  BoardCanvas canvas;
  BoardCanvasHandle surface = acquireTestSurface(canvas);
  canvas.drawRing(surface, 3.5f, 3.5f, 0.0f, 0.8f, LedColors::Red);
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 3));
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 4));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 3));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 4));
  // (3,2) is too far from the board center to be painted.
  TEST_ASSERT_FALSE(canvas.hasPixel(3, 2));
}

}  // namespace

void register_canvas_tests() {
  RUN_TEST(test_fresh_canvas_has_no_pixels);
  RUN_TEST(test_fresh_canvas_is_dirty);
  RUN_TEST(test_setPixel_makes_surface_present);
  RUN_TEST(test_setPixel_last_write_wins_within_surface);
  RUN_TEST(test_newer_surface_write_wins_over_earlier_write);
  RUN_TEST(test_earlier_surface_visible_when_later_absent);
  RUN_TEST(test_clearing_newer_surface_reveals_older_surface);
  RUN_TEST(test_surface_write_order_follows_acquisition_order);
  RUN_TEST(test_bringToFront_updates_surface_order);
  RUN_TEST(test_clearSurface_does_not_affect_other_surfaces);
  RUN_TEST(test_compose_clears_dirty_flag);
  RUN_TEST(test_setPixel_marks_dirty);
  RUN_TEST(test_clearSurface_no_op_does_not_dirty);
  RUN_TEST(test_surface_helper_acquires_and_reuses_surface);
  RUN_TEST(test_surface_helper_clear_and_release_are_safe);
  RUN_TEST(test_compose_fills_buffer_with_resolved_colors);
  RUN_TEST(test_out_of_bounds_writes_silently_ignored);
  RUN_TEST(test_fillRect_paints_inclusive_range);
  RUN_TEST(test_drawRect_paints_outline_only);
  RUN_TEST(test_fillAll_paints_every_square);
  RUN_TEST(test_drawLine_horizontal);
  RUN_TEST(test_drawLine_vertical_reversed_endpoints);
  RUN_TEST(test_drawLine_diagonal_paints_endpoints);
  RUN_TEST(test_drawLine_single_pixel);
  RUN_TEST(test_drawRing_centered_radius_one);
  RUN_TEST(test_drawRing_zero_radius_paints_only_nearest_cells);
}
