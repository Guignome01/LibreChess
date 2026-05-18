#include "board/runtime/scheduler.h"

#include <string.h>

// ---------------------------------------------------------------------------
// BoardScheduler implementation
// ---------------------------------------------------------------------------

BoardScheduler::BoardScheduler() : nextGeneration_(1) { memset(slots_, 0, sizeof(slots_)); }

uint8_t BoardScheduler::findFreeSlot() const {
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    if (slots_[i].generation == 0) return i;
  }
  return SLOT_COUNT;
}

BoardScheduledHandle BoardScheduler::schedule(BoardCanvas& canvas, const BoardPainter& painter,
                                              uint32_t durationMs, bool loop, uint32_t nowMs) {
  return scheduleAt(canvas, painter, durationMs, loop, loop ? queuedStartMs(nowMs) : nowMs);
}

BoardScheduledHandle BoardScheduler::scheduleAt(BoardCanvas& canvas, const BoardPainter& painter,
                                                uint32_t durationMs, bool loop,
                                                uint32_t startMs) {
  if (painter.paint == nullptr || painter.contextSize > CONTEXT_BYTES) return BoardScheduledHandle{};

  const uint8_t i = findFreeSlot();
  if (i >= SLOT_COUNT) return BoardScheduledHandle{};

  BoardCanvasHandle surface = canvas.acquireSurface();
  if (!surface.valid()) return BoardScheduledHandle{};

  Slot& slot = slots_[i];
  slot.painter = painter;
  slot.surface = surface;
  slot.contextSize = painter.contextSize;
  if (slot.contextSize > 0 && painter.context != nullptr) {
    memcpy(slot.context, painter.context, slot.contextSize);
  }
  slot.painter.context = slot.context;
  slot.durationMs = durationMs;
  slot.loop = loop;
  slot.startMs = startMs;

  if (nextGeneration_ == 0) nextGeneration_ = 1;
  slot.generation = nextGeneration_++;
  return BoardScheduledHandle{i, slot.generation};
}

uint32_t BoardScheduler::queuedStartMs(uint32_t nowMs) const {
  uint32_t waitMs = 0;
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    const Slot& slot = slots_[i];
    if (slot.generation == 0 || slot.loop || slot.durationMs == 0) continue;

    const uint32_t elapsed = nowMs - slot.startMs;
    if (elapsed >= slot.durationMs) continue;

    const uint32_t remaining = slot.durationMs - elapsed;
    if (remaining > waitMs) waitMs = remaining;
  }
  return nowMs + waitMs;
}

void BoardScheduler::cancel(BoardScheduledHandle& handle, BoardCanvas& canvas) {
  if (!handle.valid()) return;
  if (handle.slot >= SLOT_COUNT) {
    handle = BoardScheduledHandle{};
    return;
  }

  Slot& slot = slots_[handle.slot];
  if (slot.generation != handle.generation || slot.generation == 0) {
    handle = BoardScheduledHandle{};
    return;
  }

  releaseSlot(handle.slot, canvas);
  handle = BoardScheduledHandle{};
}

bool BoardScheduler::active(BoardScheduledHandle handle) const {
  if (!handle.valid() || handle.slot >= SLOT_COUNT) return false;
  const Slot& slot = slots_[handle.slot];
  return slot.generation == handle.generation && slot.generation != 0;
}

bool BoardScheduler::any() const {
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    if (slots_[i].generation != 0) return true;
  }
  return false;
}

void BoardScheduler::releaseSlot(uint8_t i, BoardCanvas& canvas) {
  Slot& slot = slots_[i];
  if (slot.generation == 0) return;

  const BoardPainter painter = slot.painter;
  BoardCanvasHandle surface = slot.surface;
  if (painter.mode == BoardPaintMode::INCREMENTAL && painter.cleanup != nullptr) {
    painter.cleanup(slot.context, canvas, surface);
  }

  canvas.releaseSurface(slot.surface);

  slot.generation = 0;
  slot.contextSize = 0;
}

void BoardScheduler::clearAll(BoardCanvas& canvas) {
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    releaseSlot(i, canvas);
  }
}

void BoardScheduler::run(uint32_t nowMs, BoardCanvas& canvas) {
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    Slot& slot = slots_[i];
    if (slot.generation == 0) continue;
    if (static_cast<int32_t>(nowMs - slot.startMs) < 0) continue;

    const uint32_t elapsed = nowMs - slot.startMs;
    if (!slot.loop && slot.durationMs != 0 && elapsed >= slot.durationMs) {
      releaseSlot(i, canvas);
      continue;
    }

    if (slot.painter.mode == BoardPaintMode::FULL_SURFACE) {
      canvas.clearSurface(slot.surface);
    }
  }

  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    Slot& slot = slots_[i];
    if (slot.generation == 0 || slot.painter.paint == nullptr) continue;
    if (static_cast<int32_t>(nowMs - slot.startMs) < 0) continue;
    slot.painter.paint(slot.context, canvas, slot.surface, nowMs - slot.startMs);
  }
}
