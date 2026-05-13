// Tests for BoardScheduler + BoardAnimations (retained timed painters).
//
// We verify slot lifecycle (allocation, recycling, generation safety), step
// dispatch (pixels appear on scheduled surfaces), and auto-expiration of
// finite animations.

#include <unity.h>

#include "board/core/canvas.h"
#include "board/core/scheduler.h"
#include "board/gui/animations.h"

namespace {

// ---------------------------------------------------------------------------
// Slot lifecycle
// ---------------------------------------------------------------------------

void test_start_returns_valid_handle() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  auto h = animations.startBlink(0, 0, LedColors::Red, 3, 0);
  TEST_ASSERT_TRUE(h.valid());
  TEST_ASSERT_TRUE(animations.active(h));
  TEST_ASSERT_TRUE(animations.any());
}

void test_slot_exhaustion_returns_invalid() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  // Fill all six slots.
  for (uint8_t i = 0; i < BoardAnimations::SLOT_COUNT; ++i) {
    auto h = animations.startBlink(0, i, LedColors::Red, 1, 0);
    TEST_ASSERT_TRUE(h.valid());
  }
  auto overflow = animations.startBlink(0, 0, LedColors::Red, 1, 0);
  TEST_ASSERT_FALSE(overflow.valid());
}

void test_cancel_releases_slot_on_next_step() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  auto h = animations.startThinking(0);
  TEST_ASSERT_TRUE(animations.active(h));
  animations.cancel(h);
  TEST_ASSERT_FALSE(h.valid());  // handle invalidated immediately
  // Slot still allocated until step runs.
  scheduler.run(0, canvas);
  // Now slot is free → starting new animations fills the same slot.
  TEST_ASSERT_FALSE(animations.any());
}

void test_stale_handle_rejected() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  auto h1 = animations.startBlink(0, 0, LedColors::Red, 1, 0);
  animations.cancel(h1);
  scheduler.run(0, canvas);
  // Recycle the slot.
  auto h2 = animations.startBlink(0, 1, LedColors::Green, 1, 0);
  TEST_ASSERT_TRUE(h2.valid());
  // The old (now-stale) handle should compare unequal — active() returns false.
  // Reconstruct it (since cancel zeroed the local).
  BoardAnimationHandle stale{h2.slot, static_cast<uint16_t>(h2.generation - 1)};
  TEST_ASSERT_FALSE(animations.active(stale));
}

// ---------------------------------------------------------------------------
// Step dispatch — pixels appear on scheduled surfaces
// ---------------------------------------------------------------------------

void test_blink_paints_target_square_in_on_phase() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  animations.startBlink(2, 3, LedColors::Red, 5, 0);
  scheduler.run(0, canvas);  // phase 0 → on
  TEST_ASSERT_TRUE(canvas.hasPixel(2, 3));
  // Other squares should not be touched by the blink surface.
  TEST_ASSERT_FALSE(canvas.hasPixel(4, 4));
}

void test_blink_dark_in_off_phase() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  animations.startBlink(2, 3, LedColors::Red, 5, 0);
  scheduler.run(BoardAnimationTiming::BLINK_HALF_MS + 10, canvas);  // off phase
  TEST_ASSERT_FALSE(canvas.hasPixel(2, 3));
}

void test_thinking_paints_corners() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  animations.startThinking(0);
  scheduler.run(0, canvas);
  // All 4 corners should be present on the animation surface.
  TEST_ASSERT_TRUE(canvas.hasPixel(0, 0));
  TEST_ASSERT_TRUE(canvas.hasPixel(0, 7));
  TEST_ASSERT_TRUE(canvas.hasPixel(7, 0));
  TEST_ASSERT_TRUE(canvas.hasPixel(7, 7));
  TEST_ASSERT_FALSE(canvas.hasPixel(3, 3));
}

void test_full_surface_animations_compose_without_erasing_siblings() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  auto thinking = animations.startThinking(0);
  auto capture = animations.startCapture(3, 3, 0);
  TEST_ASSERT_TRUE(thinking.valid());
  TEST_ASSERT_TRUE(capture.valid());

  scheduler.run(0, canvas);
  TEST_ASSERT_TRUE(canvas.hasPixel(0, 0));
  TEST_ASSERT_TRUE(canvas.hasPixel(7, 7));
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 3));

  animations.cancel(capture);
  scheduler.run(1, canvas);
  TEST_ASSERT_TRUE(canvas.hasPixel(0, 0));
  TEST_ASSERT_TRUE(canvas.hasPixel(7, 7));
  TEST_ASSERT_FALSE(canvas.hasPixel(3, 3));
}

void test_connecting_paints_progressive_columns() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  animations.startConnecting(0);
  // Frame 0: only col 0 lit.
  scheduler.run(0, canvas);
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 0));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 0));
  TEST_ASSERT_FALSE(canvas.hasPixel(3, 7));
  // Frame 7: all columns lit.
  scheduler.run(7 * BoardAnimationTiming::CONNECTING_FRAME_MS, canvas);
  for (int c = 0; c < 8; ++c) {
    TEST_ASSERT_TRUE(canvas.hasPixel(3, c));
    TEST_ASSERT_TRUE(canvas.hasPixel(4, c));
  }
  // Untouched rows.
  TEST_ASSERT_FALSE(canvas.hasPixel(0, 0));
  TEST_ASSERT_FALSE(canvas.hasPixel(7, 7));
}

// ---------------------------------------------------------------------------
// Auto-expiration of finite animations
// ---------------------------------------------------------------------------

void test_finite_animation_expires_after_duration() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  // Blink with times=2 → duration = 2 * 2 * 200ms = 800ms.
  auto h = animations.startBlink(0, 0, LedColors::Red, 2, 0);
  scheduler.run(0, canvas);
  TEST_ASSERT_TRUE(animations.active(h));

  // After the full duration, the next step should retire the slot.
  scheduler.run(900, canvas);
  TEST_ASSERT_FALSE(animations.active(h));
  TEST_ASSERT_FALSE(animations.any());
  // The animation surface should have been released by releaseSlot.
  TEST_ASSERT_FALSE(canvas.hasPixel(0, 0));
}

void test_looping_animation_does_not_expire() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  auto h = animations.startThinking(0);
  scheduler.run(60'000, canvas);  // one minute later
  TEST_ASSERT_TRUE(animations.active(h));
}

void test_connecting_animation_loops_until_cancelled() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  auto h = animations.startConnecting(0);
  scheduler.run(BoardAnimationTiming::CONNECTING_FRAME_MS *
                    BoardAnimationTiming::CONNECTING_FRAMES,
                canvas);
  TEST_ASSERT_TRUE(animations.active(h));
  TEST_ASSERT_TRUE(canvas.hasPixel(3, 0));
  TEST_ASSERT_TRUE(canvas.hasPixel(4, 0));
  TEST_ASSERT_FALSE(canvas.hasPixel(3, 7));
}

// ---------------------------------------------------------------------------
// clearAll
// ---------------------------------------------------------------------------

void test_clearAll_recycles_every_slot() {
  BoardScheduler scheduler;
  BoardCanvas canvas;
  BoardAnimations animations(scheduler, canvas);
  animations.startThinking(0);
  animations.startBlink(0, 0, LedColors::Red, 3, 0);
  TEST_ASSERT_TRUE(animations.any());
  animations.clearAll(canvas);
  TEST_ASSERT_FALSE(animations.any());
  // Surfaces used by those animations must be cleared.
  TEST_ASSERT_FALSE(canvas.hasPixel(0, 0));
}

}  // namespace

void register_animation_tests() {
  RUN_TEST(test_start_returns_valid_handle);
  RUN_TEST(test_slot_exhaustion_returns_invalid);
  RUN_TEST(test_cancel_releases_slot_on_next_step);
  RUN_TEST(test_stale_handle_rejected);
  RUN_TEST(test_blink_paints_target_square_in_on_phase);
  RUN_TEST(test_blink_dark_in_off_phase);
  RUN_TEST(test_thinking_paints_corners);
  RUN_TEST(test_full_surface_animations_compose_without_erasing_siblings);
  RUN_TEST(test_connecting_paints_progressive_columns);
  RUN_TEST(test_finite_animation_expires_after_duration);
  RUN_TEST(test_looping_animation_does_not_expire);
  RUN_TEST(test_connecting_animation_loops_until_cancelled);
  RUN_TEST(test_clearAll_recycles_every_slot);
}
