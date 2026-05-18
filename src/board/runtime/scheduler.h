#ifndef BOARD_RUNTIME_SCHEDULER_H
#define BOARD_RUNTIME_SCHEDULER_H

#include "board/runtime/canvas.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// BoardPainter — scheduled canvas callback descriptor
// ---------------------------------------------------------------------------
// A producer describes how to paint one logical frame on a BoardCanvas. The
// scheduler owns timing, cancellation, context storage, and surface lifecycle;
// the callback owns only the pixels it writes into the supplied surface.
// ---------------------------------------------------------------------------

enum class BoardPaintMode : uint8_t {
  INCREMENTAL,   ///< Painter updates/cleans only the pixels it owns.
  FULL_SURFACE,  ///< Scheduler clears the painter-owned surface before each frame.
};

using BoardPaintCallback = void (*)(const void* context, BoardCanvas& canvas,
                                    BoardCanvasHandle surface, uint32_t elapsedMs);
using BoardPaintCleanup = void (*)(const void* context, BoardCanvas& canvas,
                                   BoardCanvasHandle surface);

struct BoardPainter {
  const void* context = nullptr;
  uint8_t contextSize = 0;
  BoardPaintCallback paint = nullptr;
  BoardPaintCleanup cleanup = nullptr;
  BoardPaintMode mode = BoardPaintMode::INCREMENTAL;
};

// ---------------------------------------------------------------------------
// BoardScheduler — fixed-slot timed painter runner
// ---------------------------------------------------------------------------
// Generic board presentation infrastructure. It owns slot allocation,
// generation-guarded handles, start time, duration, looping, cancellation, and
// surface ownership. It has no concept of animations or programs; it only
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

  /// Schedule a painter. Looping painters are queued behind currently active
  /// finite painters. Returns an invalid handle when all slots are busy or
  /// when the painter's context does not fit the scheduler's fixed storage.
  BoardScheduledHandle schedule(BoardCanvas& canvas, const BoardPainter& painter,
                                uint32_t durationMs, bool loop, uint32_t nowMs);

  /// Cancel a scheduled painter immediately and release its owned surface.
  void cancel(BoardScheduledHandle& handle, BoardCanvas& canvas);

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
    bool loop;
    uint32_t durationMs;
    uint32_t startMs;
    BoardPainter painter;
    BoardCanvasHandle surface;
    uint8_t contextSize;
    uint8_t context[CONTEXT_BYTES];
  };

  BoardScheduledHandle scheduleAt(BoardCanvas& canvas, const BoardPainter& painter,
                                  uint32_t durationMs, bool loop, uint32_t startMs);
  uint8_t findFreeSlot() const;
  uint32_t queuedStartMs(uint32_t nowMs) const;
  void releaseSlot(uint8_t i, BoardCanvas& canvas);

  Slot slots_[SLOT_COUNT];
  uint16_t nextGeneration_;
};

#endif  // BOARD_RUNTIME_SCHEDULER_H
