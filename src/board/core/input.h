#ifndef BOARD_INPUT_H
#define BOARD_INPUT_H

#include "board/core/helpers.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// BoardInput — debounced occupancy + event ring buffer
// ---------------------------------------------------------------------------
// Owns the 8x8 occupancy snapshot (current + previous) and a small ring
// buffer of transition events (LIFTED/PLACED). Workflows consume events
// from the queue or query the latest occupancy.
//
// BoardInput is hardware-agnostic: callers provide the latest debounced
// 8x8 sensor matrix to `poll()`. The actual driver read happens inside
// BoardRuntime which bridges driver -> input. This decoupling lets the
// native test suite drive the input with synthesized sensor states.
//
// Thread-safety: BoardInput itself is NOT internally synchronized. The
// runtime serializes producer (input poll timer) and consumers (workflows)
// through a shared mutex. Within that mutex, reads/writes are O(1).
// ---------------------------------------------------------------------------

class BoardInput {
 public:
  static constexpr int ROWS = BoardHelpers::ROWS;
  static constexpr int COLS = BoardHelpers::COLS;
  static constexpr uint8_t EVENT_QUEUE_SIZE = 16;

  enum class EventKind : uint8_t {
    LIFTED,           ///< Square transitioned occupied -> empty.
    PLACED,           ///< Square transitioned empty -> occupied.
    BASELINE_SYNCED,  ///< Initial baseline set; observers may want to redraw.
  };

  struct Event {
    EventKind kind;
    int8_t row;
    int8_t col;
    uint32_t timestampMs;
  };

  BoardInput();

  BoardInput(const BoardInput&) = delete;
  BoardInput& operator=(const BoardInput&) = delete;

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------

  /// Establish the initial occupancy snapshot from the given sensor matrix.
  /// Emits a single BASELINE_SYNCED event. No LIFTED/PLACED events for the
  /// initial state. Use this after calibration finishes.
  void syncBaseline(const bool sensors[ROWS][COLS], uint32_t nowMs);

  /// Update the snapshot from a fresh debounced sensor read. Emits one
  /// LIFTED or PLACED event per square that changed since the previous poll.
  /// On overflow, drops the new event and sets `overflowed_`.
  void poll(const bool sensors[ROWS][COLS], uint32_t nowMs);

  // -------------------------------------------------------------------------
  // Occupancy queries (latest debounced state)
  // -------------------------------------------------------------------------

  /// True iff the square is currently occupied (most recent poll).
  bool occupied(int row, int col) const;

  /// True iff the most recent poll observed an occupied -> empty transition.
  /// Cleared on the following poll when the state stabilizes.
  bool wasLifted(int row, int col) const;

  /// True iff the most recent poll observed an empty -> occupied transition.
  /// Cleared on the following poll when the state stabilizes.
  bool wasPlaced(int row, int col) const;

  // -------------------------------------------------------------------------
  // Event queue
  // -------------------------------------------------------------------------

  /// Number of unconsumed events.
  uint8_t eventCount() const { return count_; }

  /// Returns true if the event ring buffer dropped any event since last
  /// `clearOverflow()` because the queue was full.
  bool overflowed() const { return overflowed_; }

  /// Number of events dropped since the last overflow clear.
  uint32_t droppedEventCount() const { return droppedEventCount_; }

  /// Highest queue depth observed since the last overflow clear/baseline.
  uint8_t maxQueueDepth() const { return maxQueueDepth_; }

  /// Clear the overflow flag and all overflow diagnostic counters.
  void clearOverflow();

  /// Read the event at logical offset `i` from the head (0 = oldest).
  /// `i` must be < `eventCount()`. Out-of-range returns a zero-initialized event.
  Event peek(uint8_t i) const;

  /// Discard the `n` oldest events from the queue. n is clamped to count_.
  void consume(uint8_t n);

  /// Drop every queued event. Does NOT alter occupancy state.
  void clearEvents();

 private:
  static bool inBounds(int row, int col) {
    return BoardHelpers::inBounds(row, col);
  }

  void pushEvent(EventKind kind, int row, int col, uint32_t nowMs);

  bool current_[ROWS][COLS];
  bool previous_[ROWS][COLS];
  Event events_[EVENT_QUEUE_SIZE];
  uint8_t head_;     ///< Index of the next event to peek (0 in linear order).
  uint8_t count_;    ///< Number of valid events in the ring.
  bool overflowed_;
  uint32_t droppedEventCount_;
  uint8_t maxQueueDepth_;
};

#endif  // BOARD_INPUT_H
