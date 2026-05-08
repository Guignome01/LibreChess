// Tests for BoardEffects (retained tick-stepped effects).
//
// We verify slot lifecycle (allocation, recycling, generation safety), step
// dispatch (a pixel appears on the right layer), and auto-expiration of
// finite effects.

#include <unity.h>

#include "board/core/canvas.h"
#include "board/core/effects.h"
#include "board/gui/effect_animations.h"

namespace {

// ---------------------------------------------------------------------------
// Slot lifecycle
// ---------------------------------------------------------------------------

void test_start_returns_valid_handle() {
  BoardEffects effects;
  BoardCanvas canvas;
  auto h = effects.startBlink(0, 0, LedColors::Red, 3, 0);
  TEST_ASSERT_TRUE(h.valid());
  TEST_ASSERT_TRUE(effects.active(h));
  TEST_ASSERT_TRUE(effects.any());
}

void test_slot_exhaustion_returns_invalid() {
  BoardEffects effects;
  BoardCanvas canvas;
  // Fill all six slots.
  for (uint8_t i = 0; i < BoardEffects::SLOT_COUNT; ++i) {
    auto h = effects.startBlink(0, i, LedColors::Red, 1, 0);
    TEST_ASSERT_TRUE(h.valid());
  }
  auto overflow = effects.startBlink(0, 0, LedColors::Red, 1, 0);
  TEST_ASSERT_FALSE(overflow.valid());
}

void test_cancel_releases_slot_on_next_step() {
  BoardEffects effects;
  BoardCanvas canvas;
  auto h = effects.startThinking(0);
  TEST_ASSERT_TRUE(effects.active(h));
  effects.cancel(h);
  TEST_ASSERT_FALSE(h.valid());  // handle invalidated immediately
  // Slot still allocated until step runs.
  effects.step(0, canvas);
  // Now slot is free → starting new effects fills the same slot.
  TEST_ASSERT_FALSE(effects.any());
}

void test_stale_handle_rejected() {
  BoardEffects effects;
  BoardCanvas canvas;
  auto h1 = effects.startBlink(0, 0, LedColors::Red, 1, 0);
  effects.cancel(h1);
  effects.step(0, canvas);
  // Recycle the slot.
  auto h2 = effects.startBlink(0, 1, LedColors::Green, 1, 0);
  TEST_ASSERT_TRUE(h2.valid());
  // The old (now-stale) handle should compare unequal — active() returns false.
  // Reconstruct it (since cancel zeroed the local).
  BoardEffectHandle stale{h2.slot, static_cast<uint16_t>(h2.generation - 1)};
  TEST_ASSERT_FALSE(effects.active(stale));
}

// ---------------------------------------------------------------------------
// Step dispatch — pixels appear on the right layer
// ---------------------------------------------------------------------------

void test_blink_paints_target_square_in_on_phase() {
  BoardEffects effects;
  BoardCanvas canvas;
  effects.startBlink(2, 3, LedColors::Red, 5, 0);
  effects.step(0, canvas);  // phase 0 → on
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::FEEDBACK, 2, 3));
  // Other squares should not be touched on the FEEDBACK layer.
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::FEEDBACK, 4, 4));
}

void test_blink_dark_in_off_phase() {
  BoardEffects effects;
  BoardCanvas canvas;
  effects.startBlink(2, 3, LedColors::Red, 5, 0);
  effects.step(BoardEffectSteps::BLINK_HALF_MS + 10, canvas);  // off phase
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::FEEDBACK, 2, 3));
}

void test_thinking_paints_corners() {
  BoardEffects effects;
  BoardCanvas canvas;
  effects.startThinking(0);
  effects.step(0, canvas);
  // All 4 corners should be present on the EFFECT layer.
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 0, 0));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 0, 7));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 7, 0));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 7, 7));
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::EFFECT, 3, 3));
}

void test_full_layer_effects_compose_without_erasing_siblings() {
  BoardEffects effects;
  BoardCanvas canvas;
  auto thinking = effects.startThinking(0);
  auto capture = effects.startCapture(3, 3, 0);
  TEST_ASSERT_TRUE(thinking.valid());
  TEST_ASSERT_TRUE(capture.valid());

  effects.step(0, canvas);
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 0, 0));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 7, 7));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 3, 3));

  effects.cancel(capture);
  effects.step(1, canvas);
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 0, 0));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 7, 7));
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::EFFECT, 3, 3));
}

void test_connecting_paints_progressive_columns() {
  BoardEffects effects;
  BoardCanvas canvas;
  effects.startConnecting(0);
  // Frame 0: only col 0 lit.
  effects.step(0, canvas);
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 3, 0));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 4, 0));
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::EFFECT, 3, 7));
  // Frame 7: all columns lit.
  effects.step(7 * BoardEffectSteps::CONNECTING_FRAME_MS, canvas);
  for (int c = 0; c < 8; ++c) {
    TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 3, c));
    TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 4, c));
  }
  // Untouched rows.
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::EFFECT, 0, 0));
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::EFFECT, 7, 7));
}

// ---------------------------------------------------------------------------
// Auto-expiration of finite effects
// ---------------------------------------------------------------------------

void test_finite_effect_expires_after_duration() {
  BoardEffects effects;
  BoardCanvas canvas;
  // Blink with times=2 → duration = 2 * 2 * 200ms = 800ms.
  auto h = effects.startBlink(0, 0, LedColors::Red, 2, 0);
  effects.step(0, canvas);
  TEST_ASSERT_TRUE(effects.active(h));

  // After the full duration, the next step should retire the slot.
  effects.step(900, canvas);
  TEST_ASSERT_FALSE(effects.active(h));
  TEST_ASSERT_FALSE(effects.any());
  // Layer should have been cleared by releaseSlot.
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::FEEDBACK, 0, 0));
}

void test_looping_effect_does_not_expire() {
  BoardEffects effects;
  BoardCanvas canvas;
  auto h = effects.startThinking(0);
  effects.step(60'000, canvas);  // one minute later
  TEST_ASSERT_TRUE(effects.active(h));
}

void test_connecting_effect_loops_until_cancelled() {
  BoardEffects effects;
  BoardCanvas canvas;
  auto h = effects.startConnecting(0);
  effects.step(BoardEffectSteps::CONNECTING_FRAME_MS * BoardEffectSteps::CONNECTING_FRAMES,
               canvas);
  TEST_ASSERT_TRUE(effects.active(h));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 3, 0));
  TEST_ASSERT_TRUE(canvas.layerHas(BoardLayer::EFFECT, 4, 0));
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::EFFECT, 3, 7));
}

// ---------------------------------------------------------------------------
// clearAll
// ---------------------------------------------------------------------------

void test_clearAll_recycles_every_slot() {
  BoardEffects effects;
  BoardCanvas canvas;
  effects.startThinking(0);
  effects.startBlink(0, 0, LedColors::Red, 3, 0);
  TEST_ASSERT_TRUE(effects.any());
  effects.clearAll(canvas);
  TEST_ASSERT_FALSE(effects.any());
  // Layers used by those effects must be cleared.
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::EFFECT, 0, 0));
  TEST_ASSERT_FALSE(canvas.layerHas(BoardLayer::FEEDBACK, 0, 0));
}

}  // namespace

void register_effects_tests() {
  RUN_TEST(test_start_returns_valid_handle);
  RUN_TEST(test_slot_exhaustion_returns_invalid);
  RUN_TEST(test_cancel_releases_slot_on_next_step);
  RUN_TEST(test_stale_handle_rejected);
  RUN_TEST(test_blink_paints_target_square_in_on_phase);
  RUN_TEST(test_blink_dark_in_off_phase);
  RUN_TEST(test_thinking_paints_corners);
  RUN_TEST(test_full_layer_effects_compose_without_erasing_siblings);
  RUN_TEST(test_connecting_paints_progressive_columns);
  RUN_TEST(test_finite_effect_expires_after_duration);
  RUN_TEST(test_looping_effect_does_not_expire);
  RUN_TEST(test_connecting_effect_loops_until_cancelled);
  RUN_TEST(test_clearAll_recycles_every_slot);
}
