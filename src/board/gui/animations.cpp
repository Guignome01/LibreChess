#include "animations.h"

#include "board/core/controller.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

namespace {

void runBlink(BoardLEDBatch& leds, int row, int col, LedRGB color, int times, bool clearAfter, bool clearBefore) {
  if (clearBefore)
    leds.clearAllLEDs(false);
  for (int i = 0; i < times; i++) {
    leds.setSquareLED(row, col, color);
    leds.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
    leds.setSquareLED(row, col, LedColors::Off);
    leds.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (!clearAfter) {
    leds.setSquareLED(row, col, color);
    leds.showLEDs();
  }
}

void runFirework(BoardLEDBatch& leds, LedRGB color) {
  leds.clearAllLEDs(false);
  float centerX = 3.5;
  float centerY = 3.5;

  // Contraction phase.
  for (float radius = 6; radius > 0; radius -= 0.5) {
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++) {
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);
        if (fabs(dist - radius) < 0.5)
          leds.setSquareLED(row, col, color);
        else
          leds.setSquareLED(row, col, LedColors::Off);
      }
    leds.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Expansion phase.
  for (float radius = 0; radius < 6; radius += 0.5) {
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++) {
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);
        if (fabs(dist - radius) < 0.5)
          leds.setSquareLED(row, col, color);
        else
          leds.setSquareLED(row, col, LedColors::Off);
      }
    leds.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  leds.clearAllLEDs();
}

void runCapture(BoardLEDBatch& leds, int centerRow, int centerCol) {
  float centerX = centerCol + 0.5f;
  float centerY = centerRow + 0.5f;

  // Wave animation with multiple expanding rings in 2 colors.
  const int numWaves = 3;
  const int totalFrames = 20;
  const float waveSpeed = 0.4f;
  const float waveWidth = 1.2f;

  leds.clearAllLEDs(false);
  for (int frame = 0; frame < totalFrames; frame++) {
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);

        uint8_t finalR = 0, finalG = 0, finalB = 0;

        // Check each wave ring.
        for (int wave = 0; wave < numWaves; wave++) {
          float waveRadius = (frame - wave * 4) * waveSpeed;
          if (waveRadius < 0) continue;

          float distToWave = fabs(dist - waveRadius);

          if (distToWave < waveWidth) {
            float intensity = 1.0f - (distToWave / waveWidth);
            intensity = intensity * intensity;

            float fadeOut = 1.0f - (waveRadius / 6.0f);
            if (fadeOut < 0) fadeOut = 0;
            intensity *= fadeOut;

            if (wave % 2 == 0) {
              finalR = max(finalR, (uint8_t)(LedColors::Red.r * intensity));
              finalG = max(finalG, (uint8_t)(LedColors::Red.g * intensity));
              finalB = max(finalB, (uint8_t)(LedColors::Red.b * intensity));
            } else {
              finalR = max(finalR, (uint8_t)(LedColors::Yellow.r * intensity));
              finalG = max(finalG, (uint8_t)(LedColors::Yellow.g * intensity));
              finalB = max(finalB, (uint8_t)(LedColors::Yellow.b * intensity));
            }
          }
        }
        leds.setSquareLED(row, col, LedRGB{finalR, finalG, finalB});
      }
    }
    leds.setSquareLED(centerRow, centerCol, LedColors::Red);
    leds.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  leds.clearAllLEDs();
}

void runPromotion(BoardLEDBatch& leds, int col) {
  leds.clearAllLEDs(false);
  for (int step = 0; step < 16; step++) {
    for (int row = 0; row < 8; row++) {
      if ((step + row) % 8 < 4)
        leds.setSquareLED(row, col, LedColors::Yellow);
      else
        leds.setSquareLED(row, col, LedColors::Off);
    }
    leds.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  leds.clearAllLEDs();
}

void runFlash(BoardLEDBatch& leds, LedRGB color, int times) {
  for (int i = 0; i < times; i++) {
    leds.clearAllLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++)
        leds.setSquareLED(row, col, color);
    leds.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  leds.clearAllLEDs();
}

void runThinking(BoardLEDBatch& leds, std::atomic<bool>* stopFlag) {
  static const int corners[][2] = {{0, 0}, {0, 7}, {7, 0}, {7, 7}};

  static const float HUE_CENTER = 240.0f;
  static const float HUE_RANGE = 10.0f;
  static const float BRIGHTNESS_MIN = 0.08f;
  static const float BRIGHTNESS_MAX = 1.0f;

  float phase = 0.0f;
  const float phaseStep = 0.04f;

  leds.clearAllLEDs(false);
  while (!stopFlag || !stopFlag->load()) {
    float breathe = (sinf(phase) + 1.0f) * 0.5f;
    float brightness = BRIGHTNESS_MIN + breathe * (BRIGHTNESS_MAX - BRIGHTNESS_MIN);
    float hue = HUE_CENTER + HUE_RANGE * (1.0f - breathe);

    float h = fmod(hue, 360.0f) / 60.0f;
    int hi = (int)h;
    float f = h - hi;
    float v = brightness;
    float q = v * (1.0f - f);
    float t = v * f;

    uint8_t r = 0, g = 0, b = 0;
    switch (hi) {
      case 0:
        r = v * 255;
        g = t * 255;
        b = 0;
        break;
      case 1:
        r = q * 255;
        g = v * 255;
        b = 0;
        break;
      case 2:
        r = 0;
        g = v * 255;
        b = t * 255;
        break;
      case 3:
        r = 0;
        g = q * 255;
        b = v * 255;
        break;
      case 4:
        r = t * 255;
        g = 0;
        b = v * 255;
        break;
      default:
        r = v * 255;
        g = 0;
        b = q * 255;
        break;
    }

    for (auto& corner : corners)
      leds.setSquareLED(corner[0], corner[1], LedRGB{r, g, b});
    leds.showLEDs();

    phase += phaseStep;
    if (phase >= 2.0f * M_PI)
      phase -= 2.0f * M_PI;

    vTaskDelay(pdMS_TO_TICKS(30));
  }
  leds.clearAllLEDs();
}

void runWaiting(BoardLEDBatch& leds, std::atomic<bool>* stopFlag) {
  static const int positions[][2] = {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {1, 7}, {2, 7}, {3, 7}, {4, 7}, {5, 7}, {6, 7}, {7, 7}, {7, 6}, {7, 5}, {7, 4}, {7, 3}, {7, 2}, {7, 1}, {7, 0}, {6, 0}, {5, 0}, {4, 0}, {3, 0}, {2, 0}, {1, 0}};
  static const int numPositions = sizeof(positions) / sizeof(positions[0]);

  int frame = 0;
  while (!stopFlag || !stopFlag->load()) {
    leds.clearAllLEDs(false);
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 2; j++) {
        int idx = (frame + i + (j * 14)) % numPositions;
        leds.setSquareLED(positions[idx][0], positions[idx][1], LedColors::White);
      }
    }
    leds.showLEDs();
    frame = (frame + 1) % numPositions;
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  leds.clearAllLEDs();
}

void runConnecting(BoardLEDBatch& leds) {
  for (int i = 0; i < 8; i++) {
    leds.setSquareLED(3, i, LedColors::Blue);
    leds.setSquareLED(4, i, LedColors::Blue);
    leds.showLEDs();
    delay(100);
  }
  leds.clearAllLEDs();
}

}  // namespace

AnimationJob AnimationJob::capture(int row, int col) {
  AnimationJob job = {AnimationType::CAPTURE, nullptr, {}};
  job.params.capture = {row, col};
  return job;
}

AnimationJob AnimationJob::promotion(int col) {
  AnimationJob job = {AnimationType::PROMOTION, nullptr, {}};
  job.params.promotion.col = col;
  return job;
}

AnimationJob AnimationJob::blink(int row, int col, LedRGB color, int times, bool clearAfter, bool clearBefore) {
  AnimationJob job = {AnimationType::BLINK, nullptr, {}};
  job.params.blink = {row, col, color, times, clearAfter, clearBefore};
  return job;
}

AnimationJob AnimationJob::firework(LedRGB color) {
  AnimationJob job = {AnimationType::FIREWORK, nullptr, {}};
  job.params.firework = {color};
  return job;
}

AnimationJob AnimationJob::flash(LedRGB color, int times) {
  AnimationJob job = {AnimationType::FLASH, nullptr, {}};
  job.params.flash = {color, times};
  return job;
}

AnimationJob AnimationJob::thinking(std::atomic<bool>* stopFlag) {
  AnimationJob job = {AnimationType::THINKING, stopFlag, {}};
  return job;
}

AnimationJob AnimationJob::waiting(std::atomic<bool>* stopFlag) {
  AnimationJob job = {AnimationType::WAITING, stopFlag, {}};
  return job;
}

AnimationJob AnimationJob::connecting() {
  return {AnimationType::CONNECTING, nullptr, {}};
}

AnimationJob AnimationJob::sync() {
  return {AnimationType::SYNC, nullptr, {}};
}

namespace BoardAnimations {

bool isCancellable(AnimationType type) {
  return type == AnimationType::THINKING || type == AnimationType::WAITING;
}

bool signalsCompletion(AnimationType type) {
  return isCancellable(type) || type == AnimationType::SYNC;
}

void execute(BoardLEDBatch& leds, const AnimationJob& job) {
  switch (job.type) {
    case AnimationType::CAPTURE:
      runCapture(leds, job.params.capture.row, job.params.capture.col);
      break;
    case AnimationType::PROMOTION:
      runPromotion(leds, job.params.promotion.col);
      break;
    case AnimationType::BLINK:
      runBlink(leds, job.params.blink.row, job.params.blink.col, job.params.blink.color, job.params.blink.times, job.params.blink.clearAfter, job.params.blink.clearBefore);
      break;
    case AnimationType::WAITING:
      runWaiting(leds, job.stopFlag);
      break;
    case AnimationType::THINKING:
      runThinking(leds, job.stopFlag);
      break;
    case AnimationType::FIREWORK:
      runFirework(leds, job.params.firework.color);
      break;
    case AnimationType::FLASH:
      runFlash(leds, job.params.flash.color, job.params.flash.times);
      break;
    case AnimationType::CONNECTING:
      runConnecting(leds);
      break;
    case AnimationType::SYNC:
      break;
  }
}

}  // namespace BoardAnimations
