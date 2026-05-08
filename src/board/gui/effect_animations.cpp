#include "board/gui/effect_animations.h"

#include <math.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Effect step function implementations.
//
// Each step function is a pure function of (elapsedMs, params). The render
// task calls these once per frame; no thread state, no driver access.
//
// Coordinate system: row 0 = rank 8 (top), col 0 = file a (left).
// ---------------------------------------------------------------------------

namespace BoardEffectSteps {

namespace {

#ifndef PI_F
#define PI_F 3.14159265358979323846f
#endif

inline uint8_t scaleByte(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 1.0f) return 255;
  return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

}  // namespace

// Blink — single square on/off N times. Touches only its own pixel so
// sibling effects on the same layer are preserved.
void stepBlink(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
               const EffectParams::BlinkParams& p) {
  const uint32_t cycle = 2 * BLINK_HALF_MS;
  const uint32_t phase = elapsedMs % cycle;
  canvas.clearLayerSquare(layer, p.row, p.col);
  if (phase < BLINK_HALF_MS) {
    canvas.setPixel(layer, p.row, p.col, p.color);
  }
}

// Flash — full board blank/fill alternating.
void stepFlash(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
               const EffectParams::FlashParams& p) {
  canvas.clearLayer(layer);
  const uint32_t cycle = 2 * FLASH_HALF_MS;
  const uint32_t phase = elapsedMs % cycle;
  if (phase >= FLASH_HALF_MS) {
    canvas.fillAll(layer, p.color);
  }
}

// Firework — contracting then expanding ring at board center.
void stepFirework(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
                  const EffectParams::FireworkParams& p) {
  canvas.clearLayer(layer);
  const uint32_t frame = elapsedMs / FIREWORK_FRAME_MS;
  if (frame >= FIREWORK_FRAMES) return;

  const float radius = (frame < FIREWORK_FRAMES / 2)
                           ? 6.0f - 0.5f * static_cast<float>(frame)
                           : 0.5f * static_cast<float>(frame - FIREWORK_FRAMES / 2);
  // Center between the four middle squares so the ring is symmetric.
  canvas.drawRing(layer, 3.5f, 3.5f, radius, 0.5f, p.color);
}

// Capture — multi-wave ripple centered on a square.
void stepCapture(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
                 const EffectParams::CaptureParams& p) {
  canvas.clearLayer(layer);
  const uint32_t frame = elapsedMs / CAPTURE_FRAME_MS;
  if (frame >= CAPTURE_FRAMES) return;

  const float cx = static_cast<float>(p.col) + 0.5f;
  const float cy = static_cast<float>(p.row) + 0.5f;
  const int numWaves = 3;
  const float waveSpeed = 0.4f;
  const float waveWidth = 1.2f;

  for (int row = 0; row < 8; ++row) {
    for (int col = 0; col < 8; ++col) {
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
        canvas.setPixel(layer, row, col, LedRGB{finalR, finalG, finalB});
      }
    }
  }
  // Center square always burns red so the captured square reads clearly.
  canvas.setPixel(layer, p.row, p.col, LedColors::Red);
}

// Promotion — animated yellow stripe along a single column.
void stepPromotion(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs,
                   const EffectParams::PromotionParams& p) {
  canvas.clearLayer(layer);
  const uint32_t frame = elapsedMs / PROMOTION_FRAME_MS;
  if (frame >= PROMOTION_FRAMES) return;
  for (int row = 0; row < 8; ++row) {
    if ((static_cast<int>(frame) + row) % 8 < 4) {
      canvas.setPixel(layer, row, p.col, LedColors::Yellow);
    }
  }
}

// Thinking — looping corner breath. Hue cycles between 230° and 250°.
void stepThinking(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs) {
  canvas.clearLayer(layer);
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
  for (auto& c : corners) {
    canvas.setPixel(layer, c[0], c[1], color);
  }
}

// Waiting — looping marquee around the perimeter.
void stepWaiting(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs) {
  canvas.clearLayer(layer);
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
      canvas.setPixel(layer, positions[idx][0], positions[idx][1], LedColors::White);
    }
  }
}

// Connecting — looping progressive blue fill of middle rows.
void stepConnecting(BoardCanvas& canvas, BoardLayer layer, uint32_t elapsedMs) {
  canvas.clearLayer(layer);
  const uint32_t frame = (elapsedMs / CONNECTING_FRAME_MS) % CONNECTING_FRAMES;
  const int upTo = static_cast<int>(frame);
  for (int col = 0; col <= upTo && col < 8; ++col) {
    canvas.setPixel(layer, 3, col, LedColors::Blue);
    canvas.setPixel(layer, 4, col, LedColors::Blue);
  }
}

}  // namespace BoardEffectSteps
