#include "board/core/effects.h"

#include "board/gui/effect_animations.h"

#include <string.h>

// ---------------------------------------------------------------------------
// BoardEffects implementation
// ---------------------------------------------------------------------------
// Slot lifecycle:
//   1. start() finds a free slot, increments nextGeneration_, stores spec +
//      startMs, and returns a handle bound to (slot, generation).
//   2. cancel() marks `cancelRequested` for the slot. The next step() call
//      clears the slot's layer pixels and frees the slot.
//   3. step() walks active slots, computes elapsedMs, and dispatches to
//      the per-kind step function. Slots whose duration has elapsed (and
//      that don't loop) are released.
//
// "Generation" exists so a stale handle from a recycled slot is rejected:
// a handle matches the slot only if `slot.generation == handle.generation`.
// ---------------------------------------------------------------------------

namespace {

uint32_t durationFor(const EffectSpec& spec) {
  // For looping effects, runtime never auto-recycles. For finite ones, this
  // matches the per-kind frame counts from the step functions.
  if (spec.loop) return 0;
  if (spec.durationMs != 0) return spec.durationMs;
  switch (spec.kind) {
    case EffectKind::BLINK:
      return 2 * BoardEffectSteps::BLINK_HALF_MS *
             static_cast<uint32_t>(spec.params.blink.times);
    case EffectKind::FLASH:
      return 2 * BoardEffectSteps::FLASH_HALF_MS *
             static_cast<uint32_t>(spec.params.flash.times);
    case EffectKind::FIREWORK:
      return BoardEffectSteps::FIREWORK_FRAME_MS * BoardEffectSteps::FIREWORK_FRAMES;
    case EffectKind::CAPTURE:
      return BoardEffectSteps::CAPTURE_FRAME_MS * BoardEffectSteps::CAPTURE_FRAMES;
    case EffectKind::PROMOTION:
      return BoardEffectSteps::PROMOTION_FRAME_MS * BoardEffectSteps::PROMOTION_FRAMES;
    case EffectKind::CONNECTING:
      return BoardEffectSteps::CONNECTING_FRAME_MS * BoardEffectSteps::CONNECTING_FRAMES;
    case EffectKind::THINKING:
    case EffectKind::WAITING:
      return 0;  // logically infinite; caller must cancel.
  }
  return 0;
}

bool paintsWholeLayer(EffectKind kind) {
  return kind != EffectKind::BLINK;
}

void paintEffectFrame(BoardCanvas& canvas, const EffectSpec& spec, uint32_t elapsedMs) {
  using namespace BoardEffectSteps;
  switch (spec.kind) {
    case EffectKind::BLINK:      stepBlink(canvas, spec.layer, elapsedMs, spec.params.blink); break;
    case EffectKind::FLASH:      stepFlash(canvas, spec.layer, elapsedMs, spec.params.flash); break;
    case EffectKind::FIREWORK:   stepFirework(canvas, spec.layer, elapsedMs, spec.params.firework); break;
    case EffectKind::CAPTURE:    stepCapture(canvas, spec.layer, elapsedMs, spec.params.capture); break;
    case EffectKind::PROMOTION:  stepPromotion(canvas, spec.layer, elapsedMs, spec.params.promotion); break;
    case EffectKind::THINKING:   stepThinking(canvas, spec.layer, elapsedMs); break;
    case EffectKind::WAITING:    stepWaiting(canvas, spec.layer, elapsedMs); break;
    case EffectKind::CONNECTING: stepConnecting(canvas, spec.layer, elapsedMs); break;
  }
}

void copyLayerPixels(const BoardCanvas& source, BoardCanvas& target, BoardLayer layer) {
  for (int row = 0; row < BoardCanvas::ROWS; ++row) {
    for (int col = 0; col < BoardCanvas::COLS; ++col) {
      if (source.layerHas(layer, row, col)) {
        target.setPixel(layer, row, col, source.resolve(row, col));
      }
    }
  }
}

}  // namespace

BoardEffects::BoardEffects() : nextGeneration_(1) {
  memset(slots_, 0, sizeof(slots_));
}

uint8_t BoardEffects::findFreeSlot() const {
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    if (slots_[i].generation == 0) return i;
  }
  return SLOT_COUNT;
}

BoardEffectHandle BoardEffects::start(const EffectSpec& spec, uint32_t nowMs) {
  const uint8_t i = findFreeSlot();
  if (i >= SLOT_COUNT) return BoardEffectHandle{};

  Slot& s = slots_[i];
  s.spec = spec;
  // Resolve the runtime duration up-front so step() can compare cheaply.
  s.spec.durationMs = durationFor(spec);
  s.startMs = nowMs;
  s.cancelRequested = false;
  // Generation must never be 0 (that means free). Wrap past 0 if needed.
  if (nextGeneration_ == 0) nextGeneration_ = 1;
  s.generation = nextGeneration_++;
  return BoardEffectHandle{i, s.generation};
}

void BoardEffects::cancel(BoardEffectHandle& handle) {
  if (!handle.valid()) return;
  if (handle.slot >= SLOT_COUNT) {
    handle = BoardEffectHandle{};
    return;
  }
  Slot& s = slots_[handle.slot];
  if (s.generation != handle.generation || s.generation == 0) {
    handle = BoardEffectHandle{};
    return;
  }
  // Defer the actual layer-clear to the next step() so the renderer sees
  // the pixels go away in one consistent flush rather than a partial state.
  s.cancelRequested = true;
  handle = BoardEffectHandle{};
}

bool BoardEffects::active(BoardEffectHandle handle) const {
  if (!handle.valid() || handle.slot >= SLOT_COUNT) return false;
  const Slot& s = slots_[handle.slot];
  return s.generation == handle.generation && s.generation != 0 && !s.cancelRequested;
}

bool BoardEffects::any() const {
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    if (slots_[i].generation != 0) return true;
  }
  return false;
}

void BoardEffects::releaseSlot(uint8_t i, BoardCanvas& canvas) {
  if (slots_[i].generation == 0) return;
  const EffectSpec spec = slots_[i].spec;
  slots_[i].generation = 0;
  slots_[i].cancelRequested = false;
  if (spec.kind == EffectKind::BLINK) {
    canvas.clearLayerSquare(spec.layer, spec.params.blink.row, spec.params.blink.col);
  } else if (!hasActiveWholeLayerEffect(spec.layer)) {
    canvas.clearLayer(spec.layer);
  }
}

bool BoardEffects::hasActiveWholeLayerEffect(BoardLayer layer) const {
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    const Slot& slot = slots_[i];
    if (slot.generation != 0 && !slot.cancelRequested && slot.spec.layer == layer &&
        paintsWholeLayer(slot.spec.kind)) {
      return true;
    }
  }
  return false;
}

void BoardEffects::clearAll(BoardCanvas& canvas) {
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    releaseSlot(i, canvas);
  }
}

// ---------------------------------------------------------------------------
// Render-task entry point
// ---------------------------------------------------------------------------

void BoardEffects::step(uint32_t nowMs, BoardCanvas& canvas) {
  bool compositeLayer[BOARD_LAYER_COUNT] = {false};

  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    Slot& s = slots_[i];
    if (s.generation == 0) continue;
    if (s.cancelRequested) {
      releaseSlot(i, canvas);
      continue;
    }
    const uint32_t elapsed = nowMs - s.startMs;
    // Auto-expire finite effects.
    if (!s.spec.loop && s.spec.durationMs != 0 && elapsed >= s.spec.durationMs) {
      releaseSlot(i, canvas);
      continue;
    }
    if (paintsWholeLayer(s.spec.kind)) {
      compositeLayer[layerIndex(s.spec.layer)] = true;
    }
  }

  for (uint8_t layer = 0; layer < BOARD_LAYER_COUNT; ++layer) {
    if (compositeLayer[layer]) {
      canvas.clearLayer(static_cast<BoardLayer>(layer));
    }
  }

  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    Slot& s = slots_[i];
    if (s.generation == 0) continue;
    const uint32_t elapsed = nowMs - s.startMs;
    if (paintsWholeLayer(s.spec.kind)) {
      scratch_.clearAll();
      paintEffectFrame(scratch_, s.spec, elapsed);
      copyLayerPixels(scratch_, canvas, s.spec.layer);
    } else {
      paintEffectFrame(canvas, s.spec, elapsed);
    }
  }
}

// ---------------------------------------------------------------------------
// Convenience builders
// ---------------------------------------------------------------------------

BoardEffectHandle BoardEffects::startBlink(int row, int col, LedRGB color, int times,
                                          uint32_t nowMs, BoardLayer layer) {
  EffectSpec spec{};
  spec.kind = EffectKind::BLINK;
  spec.layer = layer;
  spec.loop = false;
  spec.params.blink = {row, col, color, times};
  return start(spec, nowMs);
}

BoardEffectHandle BoardEffects::startFlash(LedRGB color, int times, uint32_t nowMs) {
  EffectSpec spec{};
  spec.kind = EffectKind::FLASH;
  spec.layer = BoardLayer::EFFECT;
  spec.loop = false;
  spec.params.flash = {color, times};
  return start(spec, nowMs);
}

BoardEffectHandle BoardEffects::startFirework(LedRGB color, uint32_t nowMs) {
  EffectSpec spec{};
  spec.kind = EffectKind::FIREWORK;
  spec.layer = BoardLayer::EFFECT;
  spec.loop = false;
  spec.params.firework = {color};
  return start(spec, nowMs);
}

BoardEffectHandle BoardEffects::startCapture(int row, int col, uint32_t nowMs) {
  EffectSpec spec{};
  spec.kind = EffectKind::CAPTURE;
  spec.layer = BoardLayer::EFFECT;
  spec.loop = false;
  spec.params.capture = {row, col};
  return start(spec, nowMs);
}

BoardEffectHandle BoardEffects::startPromotion(int col, uint32_t nowMs) {
  EffectSpec spec{};
  spec.kind = EffectKind::PROMOTION;
  spec.layer = BoardLayer::EFFECT;
  spec.loop = false;
  spec.params.promotion = {col};
  return start(spec, nowMs);
}

BoardEffectHandle BoardEffects::startThinking(uint32_t nowMs) {
  EffectSpec spec{};
  spec.kind = EffectKind::THINKING;
  spec.layer = BoardLayer::EFFECT;
  spec.loop = true;
  return start(spec, nowMs);
}

BoardEffectHandle BoardEffects::startWaiting(uint32_t nowMs) {
  EffectSpec spec{};
  spec.kind = EffectKind::WAITING;
  spec.layer = BoardLayer::EFFECT;
  spec.loop = true;
  return start(spec, nowMs);
}

BoardEffectHandle BoardEffects::startConnecting(uint32_t nowMs) {
  EffectSpec spec{};
  spec.kind = EffectKind::CONNECTING;
  spec.layer = BoardLayer::EFFECT;
  spec.loop = true;
  return start(spec, nowMs);
}
