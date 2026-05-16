#include "board/runtime/input.h"

#include <string.h>

// ---------------------------------------------------------------------------
// BoardInput implementation
// ---------------------------------------------------------------------------

BoardInput::BoardInput()
    : head_(0),
      count_(0),
      overflowed_(false),
      droppedEventCount_(0),
      maxQueueDepth_(0) {
  memset(current_, 0, sizeof(current_));
  memset(previous_, 0, sizeof(previous_));
  memset(events_, 0, sizeof(events_));
}

// ---------------------------------------------------------------------------
// Event queue helpers
// ---------------------------------------------------------------------------

void BoardInput::pushEvent(EventKind kind, int row, int col, uint32_t nowMs) {
  if (count_ >= EVENT_QUEUE_SIZE) {
    overflowed_ = true;
    ++droppedEventCount_;
    return;  // Drop new events on full queue (caller can detect overflow).
  }
  uint8_t writeIdx = (head_ + count_) % EVENT_QUEUE_SIZE;
  events_[writeIdx] = Event{kind, static_cast<int8_t>(row), static_cast<int8_t>(col), nowMs};
  ++count_;
  if (count_ > maxQueueDepth_) maxQueueDepth_ = count_;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void BoardInput::syncBaseline(const bool sensors[ROWS][COLS], uint32_t nowMs) {
  memcpy(current_, sensors, sizeof(current_));
  memcpy(previous_, sensors, sizeof(previous_));
  // Clear any prior events; the baseline is the new ground truth.
  head_ = 0;
  count_ = 0;
  clearOverflow();
  pushEvent(EventKind::BASELINE_SYNCED, 0, 0, nowMs);
}

void BoardInput::poll(const bool sensors[ROWS][COLS], uint32_t nowMs) {
  // Snapshot previous state so wasLifted/wasPlaced reflect the just-completed step.
  memcpy(previous_, current_, sizeof(previous_));
  for (int r = 0; r < ROWS; ++r) {
    for (int c = 0; c < COLS; ++c) {
      const bool prev = current_[r][c];
      const bool now = sensors[r][c];
      if (prev == now) continue;
      current_[r][c] = now;
      pushEvent(now ? EventKind::PLACED : EventKind::LIFTED, r, c, nowMs);
    }
  }
}

// ---------------------------------------------------------------------------
// Occupancy queries
// ---------------------------------------------------------------------------

bool BoardInput::occupied(int row, int col) const {
  if (!inBounds(row, col)) return false;
  return current_[row][col];
}

bool BoardInput::wasLifted(int row, int col) const {
  if (!inBounds(row, col)) return false;
  return previous_[row][col] && !current_[row][col];
}

bool BoardInput::wasPlaced(int row, int col) const {
  if (!inBounds(row, col)) return false;
  return !previous_[row][col] && current_[row][col];
}

// ---------------------------------------------------------------------------
// Event queue access
// ---------------------------------------------------------------------------

BoardInput::Event BoardInput::peek(uint8_t i) const {
  if (i >= count_) return Event{EventKind::BASELINE_SYNCED, 0, 0, 0};
  return events_[(head_ + i) % EVENT_QUEUE_SIZE];
}

void BoardInput::consume(uint8_t n) {
  if (n > count_) n = count_;
  head_ = (head_ + n) % EVENT_QUEUE_SIZE;
  count_ -= n;
}

void BoardInput::clearEvents() {
  head_ = 0;
  count_ = 0;
}

void BoardInput::clearOverflow() {
  overflowed_ = false;
  droppedEventCount_ = 0;
  maxQueueDepth_ = count_;
}
