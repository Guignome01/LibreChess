// Tests for BoardInput (debounced occupancy + event ring buffer).
//
// No Arduino dependency: callers feed an 8x8 sensor matrix directly.
// The .cpp is included so the native test build is single-TU per suite.

#include <unity.h>

#include <string.h>

#include "board/core/input.h"

namespace {

using Event = BoardInput::Event;
using EventKind = BoardInput::EventKind;

// Helper: zero-fill an 8x8 sensor matrix.
void clear(bool s[BoardInput::ROWS][BoardInput::COLS]) {
  memset(s, 0, sizeof(bool) * BoardHelpers::SQUARES);
}

// Helper: set one square occupied.
void place(bool s[BoardInput::ROWS][BoardInput::COLS], int r, int c) { s[r][c] = true; }

// ---------------------------------------------------------------------------
// Baseline
// ---------------------------------------------------------------------------

void test_syncBaseline_emits_one_event_only() {
  BoardInput input;
  bool sensors[BoardInput::ROWS][BoardInput::COLS];
  clear(sensors);
  place(sensors, 0, 0);
  place(sensors, 7, 7);
  input.syncBaseline(sensors, 100);
  TEST_ASSERT_EQUAL_UINT8(1, input.eventCount());
  TEST_ASSERT_EQUAL(static_cast<int>(EventKind::BASELINE_SYNCED),
                    static_cast<int>(input.peek(0).kind));
  TEST_ASSERT_TRUE(input.occupied(0, 0));
  TEST_ASSERT_TRUE(input.occupied(7, 7));
  TEST_ASSERT_FALSE(input.occupied(3, 3));
}

void test_syncBaseline_clears_prior_events() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  input.syncBaseline(s, 0);
  // Generate some transitions.
  place(s, 1, 1);
  input.poll(s, 1);
  TEST_ASSERT_EQUAL_UINT8(2, input.eventCount());  // baseline + place
  // Re-baseline drops everything.
  input.syncBaseline(s, 2);
  TEST_ASSERT_EQUAL_UINT8(1, input.eventCount());
  TEST_ASSERT_EQUAL(static_cast<int>(EventKind::BASELINE_SYNCED),
                    static_cast<int>(input.peek(0).kind));
}

// ---------------------------------------------------------------------------
// Place / lift transitions
// ---------------------------------------------------------------------------

void test_place_emits_PLACED_event() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  input.syncBaseline(s, 0);
  input.consume(1);  // drop baseline event

  place(s, 4, 4);
  input.poll(s, 50);
  TEST_ASSERT_EQUAL_UINT8(1, input.eventCount());
  Event e = input.peek(0);
  TEST_ASSERT_EQUAL(static_cast<int>(EventKind::PLACED), static_cast<int>(e.kind));
  TEST_ASSERT_EQUAL_INT(4, e.row);
  TEST_ASSERT_EQUAL_INT(4, e.col);
  TEST_ASSERT_EQUAL_UINT32(50, e.timestampMs);
  TEST_ASSERT_TRUE(input.wasPlaced(4, 4));
  TEST_ASSERT_FALSE(input.wasLifted(4, 4));
}

void test_lift_emits_LIFTED_event() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  place(s, 2, 3);
  input.syncBaseline(s, 0);
  input.consume(1);

  // Lift it.
  s[2][3] = false;
  input.poll(s, 10);
  TEST_ASSERT_EQUAL_UINT8(1, input.eventCount());
  Event e = input.peek(0);
  TEST_ASSERT_EQUAL(static_cast<int>(EventKind::LIFTED), static_cast<int>(e.kind));
  TEST_ASSERT_EQUAL_INT(2, e.row);
  TEST_ASSERT_EQUAL_INT(3, e.col);
  TEST_ASSERT_TRUE(input.wasLifted(2, 3));
  TEST_ASSERT_FALSE(input.occupied(2, 3));
}

void test_stable_frame_emits_no_events() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  place(s, 1, 1);
  input.syncBaseline(s, 0);
  input.consume(1);

  input.poll(s, 5);
  input.poll(s, 10);
  input.poll(s, 15);
  TEST_ASSERT_EQUAL_UINT8(0, input.eventCount());
  TEST_ASSERT_TRUE(input.occupied(1, 1));
  TEST_ASSERT_FALSE(input.wasPlaced(1, 1));
  TEST_ASSERT_FALSE(input.wasLifted(1, 1));
}

// ---------------------------------------------------------------------------
// Multiple changes in one poll
// ---------------------------------------------------------------------------

void test_multiple_changes_in_one_poll() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  input.syncBaseline(s, 0);
  input.consume(1);

  place(s, 0, 0);
  place(s, 7, 7);
  place(s, 3, 4);
  input.poll(s, 100);
  TEST_ASSERT_EQUAL_UINT8(3, input.eventCount());
  // All three events should be PLACED at the right time.
  for (uint8_t i = 0; i < 3; ++i) {
    Event e = input.peek(i);
    TEST_ASSERT_EQUAL(static_cast<int>(EventKind::PLACED), static_cast<int>(e.kind));
    TEST_ASSERT_EQUAL_UINT32(100, e.timestampMs);
  }
}

// ---------------------------------------------------------------------------
// Event consumption
// ---------------------------------------------------------------------------

void test_consume_advances_head() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  input.syncBaseline(s, 0);
  // Generate three PLACED events.
  place(s, 0, 0);
  input.poll(s, 1);
  place(s, 0, 1);
  input.poll(s, 2);
  place(s, 0, 2);
  input.poll(s, 3);
  TEST_ASSERT_EQUAL_UINT8(4, input.eventCount());  // baseline + 3 placed

  input.consume(2);
  TEST_ASSERT_EQUAL_UINT8(2, input.eventCount());
  // First remaining event should be the second PLACED at col=1.
  Event e = input.peek(0);
  TEST_ASSERT_EQUAL_INT(0, e.row);
  TEST_ASSERT_EQUAL_INT(1, e.col);
}

void test_consume_clamps_to_count() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  input.syncBaseline(s, 0);
  input.consume(99);  // way more than available
  TEST_ASSERT_EQUAL_UINT8(0, input.eventCount());
}

// ---------------------------------------------------------------------------
// Overflow
// ---------------------------------------------------------------------------

void test_queue_overflow_drops_new_and_sets_flag() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  input.syncBaseline(s, 0);
  // Fill the queue to its max (16). Baseline already counts as 1.
  for (int i = 0; i < 16 + 5; ++i) {
    int r = i % BoardInput::ROWS;
    int c = (i / BoardInput::ROWS) % BoardInput::COLS;
    s[r][c] = !s[r][c];  // toggle
    input.poll(s, static_cast<uint32_t>(i));
  }
  TEST_ASSERT_EQUAL_UINT8(BoardInput::EVENT_QUEUE_SIZE, input.eventCount());
  TEST_ASSERT_TRUE(input.overflowed());
  TEST_ASSERT_TRUE(input.droppedEventCount() > 0);
  TEST_ASSERT_EQUAL_UINT8(BoardInput::EVENT_QUEUE_SIZE, input.maxQueueDepth());
  input.clearOverflow();
  TEST_ASSERT_FALSE(input.overflowed());
  TEST_ASSERT_EQUAL_UINT32(0, input.droppedEventCount());
}

void test_syncBaseline_resets_overflow_metrics() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  input.syncBaseline(s, 0);
  for (int i = 0; i < 20; ++i) {
    const int r = i % BoardInput::ROWS;
    const int c = (i / BoardInput::ROWS) % BoardInput::COLS;
    s[r][c] = !s[r][c];
    input.poll(s, static_cast<uint32_t>(i + 1));
  }
  TEST_ASSERT_TRUE(input.overflowed());

  clear(s);
  input.syncBaseline(s, 200);
  TEST_ASSERT_FALSE(input.overflowed());
  TEST_ASSERT_EQUAL_UINT32(0, input.droppedEventCount());
  TEST_ASSERT_EQUAL_UINT8(1, input.maxQueueDepth());
}

// ---------------------------------------------------------------------------
// Ring buffer wrap-around
// ---------------------------------------------------------------------------

void test_ring_wraps_correctly() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  input.syncBaseline(s, 0);
  input.consume(1);

  // Push 10 events, consume 8, push 8 more — head should have wrapped.
  for (int i = 0; i < 10; ++i) {
    int r = i / BoardInput::COLS;
    int c = i % BoardInput::COLS;
    s[r][c] = true;
    input.poll(s, static_cast<uint32_t>(i + 1));
  }
  TEST_ASSERT_EQUAL_UINT8(10, input.eventCount());
  input.consume(8);
  TEST_ASSERT_EQUAL_UINT8(2, input.eventCount());
  // Push 8 more transitions.
  for (int i = 0; i < BoardInput::COLS; ++i) {
    int r = i / BoardInput::COLS;
    int c = i % BoardInput::COLS;
    s[r][c] = false;
    input.poll(s, static_cast<uint32_t>(100 + i));
  }
  TEST_ASSERT_EQUAL_UINT8(10, input.eventCount());
  // Verify the first remaining event is from the second batch (timestamp 9 or 10).
  Event first = input.peek(0);
  TEST_ASSERT_TRUE(first.timestampMs == 9 || first.timestampMs == 10);
  // Last event should be one of the lift events.
  Event last = input.peek(input.eventCount() - 1);
  TEST_ASSERT_EQUAL(static_cast<int>(EventKind::LIFTED), static_cast<int>(last.kind));
}

// ---------------------------------------------------------------------------
// Bounds safety on queries
// ---------------------------------------------------------------------------

void test_oob_queries_return_false() {
  BoardInput input;
  bool s[BoardInput::ROWS][BoardInput::COLS];
  clear(s);
  input.syncBaseline(s, 0);
  TEST_ASSERT_FALSE(input.occupied(-1, 0));
  TEST_ASSERT_FALSE(input.occupied(0, 8));
  TEST_ASSERT_FALSE(input.wasPlaced(8, 0));
  TEST_ASSERT_FALSE(input.wasLifted(0, -1));
}

}  // namespace

void register_input_tests() {
  RUN_TEST(test_syncBaseline_emits_one_event_only);
  RUN_TEST(test_syncBaseline_clears_prior_events);
  RUN_TEST(test_place_emits_PLACED_event);
  RUN_TEST(test_lift_emits_LIFTED_event);
  RUN_TEST(test_stable_frame_emits_no_events);
  RUN_TEST(test_multiple_changes_in_one_poll);
  RUN_TEST(test_consume_advances_head);
  RUN_TEST(test_consume_clamps_to_count);
  RUN_TEST(test_queue_overflow_drops_new_and_sets_flag);
  RUN_TEST(test_syncBaseline_resets_overflow_metrics);
  RUN_TEST(test_ring_wraps_correctly);
  RUN_TEST(test_oob_queries_return_false);
}
