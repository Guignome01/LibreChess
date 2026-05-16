#include "board/services/visual/animations.h"

#include <math.h>

// ---------------------------------------------------------------------------
// Board animation implementation
// ---------------------------------------------------------------------------
// Animation frame painters are pure functions of (elapsedMs, spec). They paint
// logical pixels only; BoardScheduler owns timed lifecycle and BoardRenderer
// later translates the composed canvas to hardware through BoardDriver.
// ---------------------------------------------------------------------------

namespace {

#ifndef PI_F
#define PI_F 3.14159265358979323846f
#endif

using namespace BoardAnimationTiming;

static_assert(sizeof(BoardAnimationSpec) <= BoardScheduler::CONTEXT_BYTES,
              "BoardAnimationSpec must fit scheduler slot context storage");

inline uint8_t scaleByte(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 1.0f) return 255;
  return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

uint32_t durationFor(const BoardAnimationSpec& spec) {
  if (spec.loop) return 0;
  if (spec.durationMs != 0) return spec.durationMs;
  switch (spec.kind) {
    case BoardAnimationKind::BLINK:
      return 2 * BLINK_HALF_MS * static_cast<uint32_t>(spec.params.blink.times);
    case BoardAnimationKind::FLASH:
      return 2 * FLASH_HALF_MS * static_cast<uint32_t>(spec.params.flash.times);
    case BoardAnimationKind::FIREWORK:
      return FIREWORK_FRAME_MS * FIREWORK_FRAMES;
    case BoardAnimationKind::CAPTURE:
      return CAPTURE_FRAME_MS * CAPTURE_FRAMES;
    case BoardAnimationKind::PROMOTION:
      return PROMOTION_FRAME_MS * PROMOTION_FRAMES;
    case BoardAnimationKind::CONNECTING:
      return CONNECTING_FRAME_MS * CONNECTING_FRAMES;
    case BoardAnimationKind::THINKING:
    case BoardAnimationKind::WAITING:
      return 0;
  }
  return 0;
}

BoardPaintMode paintModeFor(BoardAnimationKind kind) {
  return kind == BoardAnimationKind::BLINK ? BoardPaintMode::INCREMENTAL
                                           : BoardPaintMode::FULL_SURFACE;
}

void paintBlink(BoardCanvas& canvas, BoardCanvasHandle surface, uint32_t elapsedMs,
                const BoardAnimationParams::BlinkParams& params) {
  const uint32_t cycle = 2 * BLINK_HALF_MS;
  const uint32_t phase = elapsedMs % cycle;
  canvas.clearSurfaceSquare(surface, params.row, params.col);
  if (phase < BLINK_HALF_MS) {
    canvas.setPixel(surface, params.row, params.col, params.color);
  }
}

void paintFlash(BoardCanvas& canvas, BoardCanvasHandle surface, uint32_t elapsedMs,
                const BoardAnimationParams::FlashParams& params) {
  const uint32_t cycle = 2 * FLASH_HALF_MS;
  const uint32_t phase = elapsedMs % cycle;
  if (phase >= FLASH_HALF_MS) {
    canvas.fillAll(surface, params.color);
  }
}

void paintFirework(BoardCanvas& canvas, BoardCanvasHandle surface, uint32_t elapsedMs,
                   const BoardAnimationParams::FireworkParams& params) {
  const uint32_t frame = elapsedMs / FIREWORK_FRAME_MS;
  if (frame >= FIREWORK_FRAMES) return;

  const float radius = (frame < FIREWORK_FRAMES / 2)
                           ? 6.0f - 0.5f * static_cast<float>(frame)
                           : 0.5f * static_cast<float>(frame - FIREWORK_FRAMES / 2);
  canvas.drawRing(surface, 3.5f, 3.5f, radius, 0.5f, params.color);
}

void paintCapture(BoardCanvas& canvas, BoardCanvasHandle surface, uint32_t elapsedMs,
                  const BoardAnimationParams::CaptureParams& params) {
  const uint32_t frame = elapsedMs / CAPTURE_FRAME_MS;
  if (frame >= CAPTURE_FRAMES) return;

  const float cx = static_cast<float>(params.col) + 0.5f;
  const float cy = static_cast<float>(params.row) + 0.5f;
  const int numWaves = 3;
  const float waveSpeed = 0.4f;
  const float waveWidth = 1.2f;

  for (int row = 0; row < BoardCanvas::ROWS; ++row) {
    for (int col = 0; col < BoardCanvas::COLS; ++col) {
      const float dx = static_cast<float>(col) - cx;
      const float dy = static_cast<float>(row) - cy;
      const float dist = sqrtf(dx * dx + dy * dy);
      uint8_t finalR = 0, finalG = 0, finalB = 0;
      for (int wave = 0; wave < numWaves; ++wave) {
        const float waveRadius = (static_cast<float>(frame) - wave * 4.0f) * waveSpeed;
        if (waveRadius < 0) continue;
        const float distToWave = fabsf(dist - waveRadius);
        if (distToWave >= waveWidth) continue;
        float intensity = 1.0f - (distToWave / waveWidth);
        intensity *= intensity;
        float fadeOut = 1.0f - (waveRadius / 6.0f);
        if (fadeOut < 0) fadeOut = 0;
        intensity *= fadeOut;
        const LedRGB tint = (wave % 2 == 0) ? LedColors::Red : LedColors::Yellow;
        const uint8_t r = static_cast<uint8_t>(tint.r * intensity);
        const uint8_t g = static_cast<uint8_t>(tint.g * intensity);
        const uint8_t b = static_cast<uint8_t>(tint.b * intensity);
        if (r > finalR) finalR = r;
        if (g > finalG) finalG = g;
        if (b > finalB) finalB = b;
      }
      if (finalR || finalG || finalB) {
        canvas.setPixel(surface, row, col, LedRGB{finalR, finalG, finalB});
      }
    }
  }
  canvas.setPixel(surface, params.row, params.col, LedColors::Red);
}

void paintPromotion(BoardCanvas& canvas, BoardCanvasHandle surface, uint32_t elapsedMs,
                    const BoardAnimationParams::PromotionParams& params) {
  const uint32_t frame = elapsedMs / PROMOTION_FRAME_MS;
  if (frame >= PROMOTION_FRAMES) return;
  for (int row = 0; row < BoardCanvas::ROWS; ++row) {
    if ((static_cast<int>(frame) + row) % BoardCanvas::ROWS < BoardCanvas::ROWS / 2) {
      canvas.setPixel(surface, row, params.col, LedColors::Yellow);
    }
  }
}

void paintThinking(BoardCanvas& canvas, BoardCanvasHandle surface, uint32_t elapsedMs) {
  static constexpr int corners[4][2] = {{0, 0}, {0, 7}, {7, 0}, {7, 7}};
  static constexpr float HUE_CENTER = 240.0f;
  static constexpr float HUE_RANGE = 10.0f;
  static constexpr float BRIGHTNESS_MIN = 0.08f;
  static constexpr float BRIGHTNESS_MAX = 1.0f;

  const float phase = (static_cast<float>(elapsedMs) / THINKING_FRAME_MS) * 0.04f;
  const float breathe = (sinf(phase) + 1.0f) * 0.5f;
  const float brightness = BRIGHTNESS_MIN + breathe * (BRIGHTNESS_MAX - BRIGHTNESS_MIN);
  const float hue = HUE_CENTER + HUE_RANGE * (1.0f - breathe);

  const float h = hue / 60.0f;
  const int hi = static_cast<int>(h);
  const float f = h - hi;
  const float v = brightness;
  const float q = v * (1.0f - f);
  const float t = v * f;
  uint8_t r = 0, g = 0, b = 0;
  switch (hi) {
    case 0: r = scaleByte(v); g = scaleByte(t); b = 0; break;
    case 1: r = scaleByte(q); g = scaleByte(v); b = 0; break;
    case 2: r = 0; g = scaleByte(v); b = scaleByte(t); break;
    case 3: r = 0; g = scaleByte(q); b = scaleByte(v); break;
    case 4: r = scaleByte(t); g = 0; b = scaleByte(v); break;
    default: r = scaleByte(v); g = 0; b = scaleByte(q); break;
  }
  const LedRGB color{r, g, b};
  for (auto& corner : corners) {
    canvas.setPixel(surface, corner[0], corner[1], color);
  }
}

void paintWaiting(BoardCanvas& canvas, BoardCanvasHandle surface, uint32_t elapsedMs) {
  static constexpr int positions[28][2] = {
      {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7},
      {1, 7}, {2, 7}, {3, 7}, {4, 7}, {5, 7}, {6, 7}, {7, 7}, {7, 6},
      {7, 5}, {7, 4}, {7, 3}, {7, 2}, {7, 1}, {7, 0}, {6, 0}, {5, 0},
      {4, 0}, {3, 0}, {2, 0}, {1, 0},
  };
  constexpr int numPositions = 28;
  const uint32_t frame = elapsedMs / WAITING_FRAME_MS;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 2; ++j) {
      const int idx = (static_cast<int>(frame) + i + j * 14) % numPositions;
      canvas.setPixel(surface, positions[idx][0], positions[idx][1], LedColors::White);
    }
  }
}

void paintConnecting(BoardCanvas& canvas, BoardCanvasHandle surface, uint32_t elapsedMs) {
  const uint32_t frame = (elapsedMs / CONNECTING_FRAME_MS) % CONNECTING_FRAMES;
  const int upTo = static_cast<int>(frame);
  for (int col = 0; col <= upTo && col < BoardCanvas::COLS; ++col) {
    canvas.setPixel(surface, 3, col, LedColors::Blue);
    canvas.setPixel(surface, 4, col, LedColors::Blue);
  }
}

void paintAnimation(const void* context, BoardCanvas& canvas, BoardCanvasHandle surface,
                    uint32_t elapsedMs) {
  const auto& spec = *static_cast<const BoardAnimationSpec*>(context);
  switch (spec.kind) {
    case BoardAnimationKind::BLINK:      paintBlink(canvas, surface, elapsedMs, spec.params.blink); break;
    case BoardAnimationKind::FLASH:      paintFlash(canvas, surface, elapsedMs, spec.params.flash); break;
    case BoardAnimationKind::FIREWORK:   paintFirework(canvas, surface, elapsedMs, spec.params.firework); break;
    case BoardAnimationKind::CAPTURE:    paintCapture(canvas, surface, elapsedMs, spec.params.capture); break;
    case BoardAnimationKind::PROMOTION:  paintPromotion(canvas, surface, elapsedMs, spec.params.promotion); break;
    case BoardAnimationKind::THINKING:   paintThinking(canvas, surface, elapsedMs); break;
    case BoardAnimationKind::WAITING:    paintWaiting(canvas, surface, elapsedMs); break;
    case BoardAnimationKind::CONNECTING: paintConnecting(canvas, surface, elapsedMs); break;
  }
}

void cleanupAnimation(const void* context, BoardCanvas& canvas, BoardCanvasHandle surface) {
  const auto& spec = *static_cast<const BoardAnimationSpec*>(context);
  if (spec.kind == BoardAnimationKind::BLINK) {
    canvas.clearSurfaceSquare(surface, spec.params.blink.row, spec.params.blink.col);
  }
}

}  // namespace

BoardAnimations::BoardAnimations(BoardScheduler& scheduler, BoardCanvas& canvas)
  : scheduler_(scheduler), canvas_(canvas) {}

BoardAnimationHandle BoardAnimations::start(const BoardAnimationSpec& spec, uint32_t nowMs) {
  BoardAnimationSpec runtimeSpec = spec;
  runtimeSpec.durationMs = durationFor(spec);

  BoardPainter painter;
  painter.context = &runtimeSpec;
  painter.contextSize = sizeof(runtimeSpec);
  painter.paint = paintAnimation;
  painter.cleanup = cleanupAnimation;
  painter.mode = paintModeFor(runtimeSpec.kind);

  return scheduler_.schedule(canvas_, painter, runtimeSpec.durationMs, runtimeSpec.loop, nowMs);
}

void BoardAnimations::cancel(BoardAnimationHandle& handle) { scheduler_.cancel(handle); }

bool BoardAnimations::active(BoardAnimationHandle handle) const { return scheduler_.active(handle); }

bool BoardAnimations::any() const { return scheduler_.any(); }

void BoardAnimations::clearAll() { scheduler_.clearAll(canvas_); }

BoardAnimationHandle BoardAnimations::startBlink(int row, int col, LedRGB color, int times,
                                                 uint32_t nowMs) {
  BoardAnimationSpec spec{};
  spec.kind = BoardAnimationKind::BLINK;
  spec.loop = false;
  spec.params.blink = {row, col, color, times};
  return start(spec, nowMs);
}

BoardAnimationHandle BoardAnimations::startFlash(LedRGB color, int times, uint32_t nowMs) {
  BoardAnimationSpec spec{};
  spec.kind = BoardAnimationKind::FLASH;
  spec.loop = false;
  spec.params.flash = {color, times};
  return start(spec, nowMs);
}

BoardAnimationHandle BoardAnimations::startFirework(LedRGB color, uint32_t nowMs) {
  BoardAnimationSpec spec{};
  spec.kind = BoardAnimationKind::FIREWORK;
  spec.loop = false;
  spec.params.firework = {color};
  return start(spec, nowMs);
}

BoardAnimationHandle BoardAnimations::startCapture(int row, int col, uint32_t nowMs) {
  BoardAnimationSpec spec{};
  spec.kind = BoardAnimationKind::CAPTURE;
  spec.loop = false;
  spec.params.capture = {row, col};
  return start(spec, nowMs);
}

BoardAnimationHandle BoardAnimations::startPromotion(int col, uint32_t nowMs) {
  BoardAnimationSpec spec{};
  spec.kind = BoardAnimationKind::PROMOTION;
  spec.loop = false;
  spec.params.promotion = {col};
  return start(spec, nowMs);
}

BoardAnimationHandle BoardAnimations::startThinking(uint32_t nowMs) {
  BoardAnimationSpec spec{};
  spec.kind = BoardAnimationKind::THINKING;
  spec.loop = true;
  return start(spec, nowMs);
}

BoardAnimationHandle BoardAnimations::startWaiting(uint32_t nowMs) {
  BoardAnimationSpec spec{};
  spec.kind = BoardAnimationKind::WAITING;
  spec.loop = true;
  return start(spec, nowMs);
}

BoardAnimationHandle BoardAnimations::startConnecting(uint32_t nowMs) {
  BoardAnimationSpec spec{};
  spec.kind = BoardAnimationKind::CONNECTING;
  spec.loop = true;
  return start(spec, nowMs);
}
