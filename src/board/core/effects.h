#ifndef BOARD_EFFECTS_H
#define BOARD_EFFECTS_H

#include "board/core/canvas.h"
#include "board/core/colors.h"
#include "board/gui/layers.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// BoardEffects — retained, tick-stepped animated effects
// ---------------------------------------------------------------------------
// Animations are state, not threads. An effect occupies a fixed slot, knows
// its start time + duration + parameters, and is stepped once per render
// frame by `step(nowMs, canvas)`. Each step paints the current frame into
// the effect's assigned BoardLayer.
//
// A renderer task in BoardRuntime calls `step()` ~30 Hz. Workflows just
// call `start*()` and forget — the effect runs to completion (or loops
// indefinitely until cancelled) on its own.
//
// Slot allocation: BOARD_EFFECT_SLOTS fixed slots. `start()` returns an
// invalid handle when full so callers can degrade gracefully. Stale
// handles (slot recycled to a new effect) are silently rejected.
// ---------------------------------------------------------------------------

enum class EffectKind : uint8_t {
  BLINK,        ///< Single-square blink (on/off N times).
  FLASH,        ///< Full-board flash (clear/fill N times).
  FIREWORK,     ///< Contracting + expanding ring at center.
  CAPTURE,      ///< Multi-wave ring centered on a square.
  PROMOTION,    ///< Animated stripe along one column.
  THINKING,     ///< Looping corner breath (engine working).
  WAITING,      ///< Looping marquee around the border.
  CONNECTING,   ///< Looping progressive middle-rows fill.
};

struct EffectParams {
  // The union members are tagged by EffectSpec::kind. Unused slots stay zero.
  struct BlinkParams { int row; int col; LedRGB color; int times; };
  struct FlashParams { LedRGB color; int times; };
  struct FireworkParams { LedRGB color; };
  struct CaptureParams { int row; int col; };
  struct PromotionParams { int col; };

  union {
    BlinkParams blink;
    FlashParams flash;
    FireworkParams firework;
    CaptureParams capture;
    PromotionParams promotion;
  };

  EffectParams() { blink = {0, 0, LedColors::Off, 0}; }
};

struct EffectSpec {
  EffectKind kind;
  BoardLayer layer;
  uint32_t durationMs;  ///< Total run-time in ms; ignored when `loop` is true.
  bool loop;            ///< If true, effect runs until explicitly cancelled.
  EffectParams params;
};

/// Opaque handle returned by BoardEffects::start. Stale handles are
/// silently ignored by `cancel`/`active`.
struct BoardEffectHandle {
  static constexpr uint8_t INVALID_SLOT = 0xFF;
  uint8_t slot = INVALID_SLOT;
  uint16_t generation = 0;
  bool valid() const { return slot != INVALID_SLOT; }
};

class BoardEffects {
 public:
  static constexpr uint8_t SLOT_COUNT = 6;

  BoardEffects();

  BoardEffects(const BoardEffects&) = delete;
  BoardEffects& operator=(const BoardEffects&) = delete;

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------

  /// Start a new effect. Returns INVALID handle when no slot is free.
  /// `nowMs` is the start timestamp. Caller may discard the returned handle
  /// for fire-and-forget effects (finite duration ones will self-recycle).
  BoardEffectHandle start(const EffectSpec& spec, uint32_t nowMs);

  /// Request cancellation of an active effect. The slot's pixels are cleared
  /// from its layer on the next `step()` call. `handle` is invalidated.
  /// Stale or invalid handles are silently ignored.
  void cancel(BoardEffectHandle& handle);

  /// True iff `handle` still references a live effect.
  bool active(BoardEffectHandle handle) const;

  /// True iff at least one slot is currently active.
  bool any() const;

  /// Cancel every active effect immediately and clear their layers.
  void clearAll(BoardCanvas& canvas);

  // -------------------------------------------------------------------------
  // Render-task entry point
  // -------------------------------------------------------------------------

  /// Advance every active effect by computing the current frame against
  /// `nowMs - startMs` and painting it into its assigned layer. Effects
  /// whose duration has elapsed (and don't loop) are recycled.
  void step(uint32_t nowMs, BoardCanvas& canvas);

  // -------------------------------------------------------------------------
  // Convenience builders
  // -------------------------------------------------------------------------

  /// Blink one square `times` times (each cycle ~400 ms). Layer FEEDBACK.
  BoardEffectHandle startBlink(int row, int col, LedRGB color, int times,
                               uint32_t nowMs, BoardLayer layer = BoardLayer::FEEDBACK);

  /// Full-board flash, clear+fill alternating. Layer EFFECT.
  BoardEffectHandle startFlash(LedRGB color, int times, uint32_t nowMs);

  /// Full-board firework (contract + expand ring). Layer EFFECT.
  BoardEffectHandle startFirework(LedRGB color, uint32_t nowMs);

  /// Multi-wave capture ripple. Layer EFFECT.
  BoardEffectHandle startCapture(int row, int col, uint32_t nowMs);

  /// Vertical stripe on one column. Layer EFFECT.
  BoardEffectHandle startPromotion(int col, uint32_t nowMs);

  /// Looping corner breath until cancelled. Layer EFFECT.
  BoardEffectHandle startThinking(uint32_t nowMs);

  /// Looping marquee border until cancelled. Layer EFFECT.
  BoardEffectHandle startWaiting(uint32_t nowMs);

  /// Looping middle-rows fill. Layer EFFECT.
  BoardEffectHandle startConnecting(uint32_t nowMs);

 private:
  struct Slot {
    uint16_t generation;      ///< 0 = free; non-zero = active.
    bool cancelRequested;
    EffectSpec spec;
    uint32_t startMs;
  };

  /// Find the first free slot index, or SLOT_COUNT if none.
  uint8_t findFreeSlot() const;

  /// True iff another active full-layer effect still paints `layer`.
  bool hasActiveWholeLayerEffect(BoardLayer layer) const;

  /// Free a slot and clear its layer's pixels owned by that effect.
  void releaseSlot(uint8_t i, BoardCanvas& canvas);

  Slot slots_[SLOT_COUNT];
  BoardCanvas scratch_;
  uint16_t nextGeneration_;
};

#endif  // BOARD_EFFECTS_H
