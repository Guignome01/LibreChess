#ifndef BOARD_CORE_SCHEDULER_H
#define BOARD_CORE_SCHEDULER_H

#include "board/core/painter.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// BoardScheduler — fixed-slot timed painter runner
// ---------------------------------------------------------------------------
// Generic board presentation infrastructure. It owns slot allocation,
// generation-guarded handles, start time, duration, looping, cancellation, and
// surface ownership. It has no concept of animations or workflows; it only
// runs BoardPainter callbacks against a logical BoardCanvas.
// ---------------------------------------------------------------------------

struct BoardScheduledHandle {
  static constexpr uint8_t INVALID_SLOT = 0xFF;
  uint8_t slot = INVALID_SLOT;
  uint16_t generation = 0;
  bool valid() const { return slot != INVALID_SLOT; }
};

class BoardScheduler {
 public:
  static constexpr uint8_t SLOT_COUNT = 6;
  static constexpr uint8_t CONTEXT_BYTES = 48;

  BoardScheduler();

  BoardScheduler(const BoardScheduler&) = delete;
  BoardScheduler& operator=(const BoardScheduler&) = delete;

  /// Schedule a painter. Returns an invalid handle when all slots are busy or
  /// when the painter's context does not fit the scheduler's fixed storage.
  BoardScheduledHandle schedule(BoardCanvas& canvas, const BoardPainter& painter,
                                uint32_t durationMs, bool loop, uint32_t nowMs);

  /// Request cancellation of a scheduled painter. Slot cleanup happens on the
  /// next run() call so presentation changes are flushed consistently.
  void cancel(BoardScheduledHandle& handle);

  /// Return true iff `handle` still references an active scheduled painter.
  bool active(BoardScheduledHandle handle) const;

  /// Return true iff at least one slot is currently active.
  bool any() const;

  /// Cancel every active slot immediately and release its owned surface.
  void clearAll(BoardCanvas& canvas);

  /// Run all active painters for the current time.
  void run(uint32_t nowMs, BoardCanvas& canvas);

 private:
  struct Slot {
    uint16_t generation;
    bool cancelRequested;
    bool loop;
    uint32_t durationMs;
    uint32_t startMs;
    BoardPainter painter;
    BoardCanvasHandle surface;
    uint8_t contextSize;
    uint8_t context[CONTEXT_BYTES];
  };

  uint8_t findFreeSlot() const;
  void releaseSlot(uint8_t i, BoardCanvas& canvas);

  Slot slots_[SLOT_COUNT];
  uint16_t nextGeneration_;
};

#endif  // BOARD_CORE_SCHEDULER_H
