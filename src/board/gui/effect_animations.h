#ifndef BOARD_EFFECT_ANIMATIONS_H
#define BOARD_EFFECT_ANIMATIONS_H

#include "board/core/canvas.h"
#include "board/core/colors.h"
#include "board/core/effects.h"
#include "board/gui/layers.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// Effect step functions (Phase 3 of the board refactor)
//
// These are pure, deterministic per-tick frame painters used by
// `BoardEffects::step`. Each `stepX(canvas, layer, elapsedMs, params)`
// clears its own layer (or square, for blink) then paints the frame
// corresponding to `elapsedMs`.
//
// They live in their own namespace `BoardEffectSteps`.
// ---------------------------------------------------------------------------

namespace BoardEffectSteps {

// Frame timings (ms).
constexpr uint32_t BLINK_HALF_MS = 200;
constexpr uint32_t FLASH_HALF_MS = 200;
constexpr uint32_t FIREWORK_FRAME_MS = 100;
constexpr uint32_t FIREWORK_FRAMES = 24;
constexpr uint32_t CAPTURE_FRAME_MS = 50;
constexpr uint32_t CAPTURE_FRAMES = 20;
constexpr uint32_t PROMOTION_FRAME_MS = 100;
constexpr uint32_t PROMOTION_FRAMES = 16;
constexpr uint32_t CONNECTING_FRAME_MS = 100;
constexpr uint32_t CONNECTING_FRAMES = 8;
constexpr uint32_t THINKING_FRAME_MS = 30;
constexpr uint32_t WAITING_FRAME_MS = 200;

void stepBlink(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
               const EffectParams::BlinkParams& p);
void stepFlash(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
               const EffectParams::FlashParams& p);
void stepFirework(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
                  const EffectParams::FireworkParams& p);
void stepCapture(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
                 const EffectParams::CaptureParams& p);
void stepPromotion(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
                   const EffectParams::PromotionParams& p);
void stepThinking(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs);
void stepWaiting(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs);
void stepConnecting(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs);

}  // namespace BoardEffectSteps

#endif  // BOARD_EFFECT_ANIMATIONS_H
