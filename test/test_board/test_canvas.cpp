// Tests for BoardCanvas (multi-layer 8x8 pixel surface).
//
// The canvas is pure logic: no Arduino, no driver, no threading. We verify
// layer composition, presence semantics, dirty tracking, and bounds safety.
//
// The .cpp is included directly so the native test environment doesn't need
// to compile src/ as a project source set.

#include <unity.h>

#include "board/core/canvas.h"

namespace {

bool sameColor(LedRGB a, LedRGB b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

LedRGB rgb(uint8_t r, uint8_t g, uint8_t b) { return LedRGB{r, g, b}; }

// ---------------------------------------------------------------------------
// Construction + initial state
// ---------------------------------------------------------------------------

void test_fresh_canvas_has_no_pixels() {
  BoardCanvas canvas;
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
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
// Single-layer writes
// ---------------------------------------------------------------------------

void test_setPixel_makes_layer_present() {
  BoardCanvas canvas;
  canvas.setPixel(BoardLayer::GAME, 2, 3, rgb(10, 20, 30));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::GAME, 2, 3));
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::FEEDBACK, 2, 3));
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(2, 3), rgb(10, 20, 30)));
}

void test_setPixel_last_write_wins_within_layer() {
  BoardCanvas canvas;
  canvas.setPixel(BoardLayer::GAME, 0, 0, LedColors::Red);
  canvas.setPixel(BoardLayer::GAME, 0, 0, LedColors::Green);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(0, 0), LedColors::Green));
}

// ---------------------------------------------------------------------------
// Layer composition (top wins)
// ---------------------------------------------------------------------------

void test_top_layer_wins_over_bottom() {
  BoardCanvas canvas;
  canvas.setPixel(BoardLayer::GAME, 4, 4, LedColors::Red);
  canvas.setPixel(BoardLayer::EFFECT, 4, 4, LedColors::Green);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(4, 4), LedColors::Green));
}

void test_bottom_visible_when_top_absent() {
  BoardCanvas canvas;
  canvas.setPixel(BoardLayer::GAME, 5, 5, LedColors::Red);
  // No write to EFFECT — GAME shows through.
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(5, 5), LedColors::Red));
}

void test_clearing_top_reveals_bottom() {
  BoardCanvas canvas;
  canvas.setPixel(BoardLayer::GAME, 1, 1, LedColors::Red);
  canvas.setPixel(BoardLayer::EFFECT, 1, 1, LedColors::Green);
  canvas.clearLayerSquare(BoardLayer::EFFECT, 1, 1);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(1, 1), LedColors::Red));
}

void test_clearLayer_does_not_affect_other_layers() {
  BoardCanvas canvas;
  canvas.setPixel(BoardLayer::GAME, 0, 0, LedColors::Red);
  canvas.setPixel(BoardLayer::FEEDBACK, 0, 0, LedColors::Yellow);
  canvas.clearLayer(BoardLayer::FEEDBACK);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(0, 0), LedColors::Red));
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::FEEDBACK, 0, 0));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::GAME, 0, 0));
}

// ---------------------------------------------------------------------------
// Dirty flag tracking
// ---------------------------------------------------------------------------

void test_compose_clears_dirty_flag() {
  BoardCanvas canvas;
  canvas.setPixel(BoardLayer::GAME, 0, 0, LedColors::Red);
  TEST_ASSERT_TRUE(canvas.dirty());
  LedRGB out[8][8];
  canvas.compose(out);
  TEST_ASSERT_FALSE(canvas.dirty());
}

void test_setPixel_marks_dirty() {
  BoardCanvas canvas;
  LedRGB out[8][8];
  canvas.compose(out);  // Drains initial dirty bit.
  TEST_ASSERT_FALSE(canvas.dirty());
  canvas.setPixel(BoardLayer::GAME, 0, 0, LedColors::Red);
  TEST_ASSERT_TRUE(canvas.dirty());
}

void test_clearLayer_no_op_does_not_dirty() {
  // Clearing an already-empty layer should not pin the dirty flag, otherwise
  // an idle canvas would force the renderer to flush every tick.
  BoardCanvas canvas;
  LedRGB out[8][8];
  canvas.compose(out);
  TEST_ASSERT_FALSE(canvas.dirty());
  canvas.clearLayer(BoardLayer::FEEDBACK);  // Already empty.
  TEST_ASSERT_FALSE(canvas.dirty());
}

// ---------------------------------------------------------------------------
// Compose output buffer
// ---------------------------------------------------------------------------

void test_compose_fills_buffer_with_top_layer_colors() {
  BoardCanvas canvas;
  canvas.setPixel(BoardLayer::GAME, 0, 0, LedColors::Red);
  canvas.setPixel(BoardLayer::EFFECT, 7, 7, LedColors::Green);
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
  canvas.setPixel(BoardLayer::GAME, -1, 0, LedColors::Red);
  canvas.setPixel(BoardLayer::GAME, 0, 8, LedColors::Red);
  canvas.setPixel(BoardLayer::GAME, 8, 8, LedColors::Red);
  // Nothing should have been recorded; canvas is still empty.
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      TEST_ASSERT_FALSE(canvas.hasPixel(r, c));
    }
  }
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void test_fillRect_paints_inclusive_range() {
  BoardCanvas canvas;
  canvas.fillRect(BoardLayer::GAME, 1, 2, 3, 4, LedColors::Blue);
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
  canvas.drawRect(BoardLayer::GAME, 2, 2, 4, 4, LedColors::Red);
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
  canvas.fillAll(BoardLayer::EFFECT, LedColors::Cyan);
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      TEST_ASSERT_TRUE(sameColor(canvas.resolve(r, c), LedColors::Cyan));
    }
  }
}

void test_drawLine_horizontal() {
  BoardCanvas canvas;
  canvas.drawLine(BoardLayer::GAME, 3, 1, 3, 6, LedColors::Red);
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
  canvas.drawLine(BoardLayer::GAME, 5, 2, 1, 2, LedColors::Green);
  for (int r = 1; r <= 5; ++r) {
    TEST_ASSERT_TRUE(sameColor(canvas.resolve(r, 2), LedColors::Green));
  }
}

void test_drawLine_diagonal_paints_endpoints() {
  BoardCanvas canvas;
  canvas.drawLine(BoardLayer::GAME, 0, 0, 7, 7, LedColors::Blue);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(0, 0), LedColors::Blue));
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(7, 7), LedColors::Blue));
  // Each step must paint along the main diagonal.
  for (int i = 0; i < 8; ++i) {
    TEST_ASSERT_TRUE(canvas.hasPixel(i, i));
  }
}

void test_drawLine_single_pixel() {
  BoardCanvas canvas;
  canvas.drawLine(BoardLayer::GAME, 4, 4, 4, 4, LedColors::Yellow);
  TEST_ASSERT_TRUE(sameColor(canvas.resolve(4, 4), LedColors::Yellow));
}

void test_drawRing_centered_radius_one() {
  // halfWidth 0.5 → only cells whose distance is ~1 from center (3.5, 3.5)
  // get painted. The four cells immediately around the board center each
  // sit at distance sqrt(0.5) ≈ 0.707, which is NOT within 0.5 of radius 1
  // (|0.707 - 1| = 0.293 < 0.5) — so all four should paint.
  BoardCanvas canvas;
  canvas.drawRing(BoardLayer::EFFECT, 3.5f, 3.5f, 1.0f, 0.5f, LedColors::Red);
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
  canvas.drawRing(BoardLayer::EFFECT, 3.5f, 3.5f, 0.0f, 0.8f, LedColors::Red);
  // sqrt(0.5) ≈ 0.707 < 0.8 → the four center cells paint.
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 3));
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 4));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 3));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 4));
  // (3,2) is at distance sqrt(0.5^2 + 1.5^2) ≈ 1.58 → not painted.
  TEST_ASSERT_FALSE(canvas.hasPixel(3, 2));
}

}  // namespace

void register_canvas_tests() {
  RUN_TEST(test_fresh_canvas_has_no_pixels);
  RUN_TEST(test_fresh_canvas_is_dirty);
  RUN_TEST(test_setPixel_makes_layer_present);
  RUN_TEST(test_setPixel_last_write_wins_within_layer);
  RUN_TEST(test_top_layer_wins_over_bottom);
  RUN_TEST(test_bottom_visible_when_top_absent);
  RUN_TEST(test_clearing_top_reveals_bottom);
  RUN_TEST(test_clearLayer_does_not_affect_other_layers);
  RUN_TEST(test_compose_clears_dirty_flag);
  RUN_TEST(test_setPixel_marks_dirty);
  RUN_TEST(test_clearLayer_no_op_does_not_dirty);
  RUN_TEST(test_compose_fills_buffer_with_top_layer_colors);
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
