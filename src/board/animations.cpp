#include "animations.h"

#include "driver.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

namespace {

void runBlink(BoardDriver& driver, int row, int col, LedRGB color, int times, bool clearAfter, bool clearBefore) {
  if (clearBefore)
    driver.clearAllLEDs(false);
  for (int i = 0; i < times; i++) {
    driver.setSquareLED(row, col, color);
    driver.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
    driver.setSquareLED(row, col, LedColors::Off);
    driver.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (!clearAfter) {
    driver.setSquareLED(row, col, color);
    driver.showLEDs();
  }
}

void runFirework(BoardDriver& driver, LedRGB color) {
  driver.clearAllLEDs(false);
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
          driver.setSquareLED(row, col, color);
        else
          driver.setSquareLED(row, col, LedColors::Off);
      }
    driver.showLEDs();
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
          driver.setSquareLED(row, col, color);
        else
          driver.setSquareLED(row, col, LedColors::Off);
      }
    driver.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  driver.clearAllLEDs();
}

void runCapture(BoardDriver& driver, int centerRow, int centerCol) {
  float centerX = centerCol + 0.5f;
  float centerY = centerRow + 0.5f;

  // Wave animation with multiple expanding rings in 2 colors.
  const int numWaves = 3;
  const int totalFrames = 20;
  const float waveSpeed = 0.4f;
  const float waveWidth = 1.2f;

  driver.clearAllLEDs(false);
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
        driver.setSquareLED(row, col, LedRGB{finalR, finalG, finalB});
      }
    }
    driver.setSquareLED(centerRow, centerCol, LedColors::Red);
    driver.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  driver.clearAllLEDs();
}

void runPromotion(BoardDriver& driver, int col) {
  driver.clearAllLEDs(false);
  for (int step = 0; step < 16; step++) {
    for (int row = 0; row < 8; row++) {
      if ((step + row) % 8 < 4)
        driver.setSquareLED(row, col, LedColors::Yellow);
      else
        driver.setSquareLED(row, col, LedColors::Off);
    }
    driver.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  driver.clearAllLEDs();
}

void runFlash(BoardDriver& driver, LedRGB color, int times) {
  for (int i = 0; i < times; i++) {
    driver.clearAllLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++)
        driver.setSquareLED(row, col, color);
    driver.showLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  driver.clearAllLEDs();
}

void runThinking(BoardDriver& driver, std::atomic<bool>* stopFlag) {
  static const int corners[][2] = {{0, 0}, {0, 7}, {7, 0}, {7, 7}};

  static const float HUE_CENTER = 240.0f;
  static const float HUE_RANGE = 10.0f;
  static const float BRIGHTNESS_MIN = 0.08f;
  static const float BRIGHTNESS_MAX = 1.0f;

  float phase = 0.0f;
  const float phaseStep = 0.04f;

  driver.clearAllLEDs(false);
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
      driver.setSquareLED(corner[0], corner[1], LedRGB{r, g, b});
    driver.showLEDs();

    phase += phaseStep;
    if (phase >= 2.0f * M_PI)
      phase -= 2.0f * M_PI;

    vTaskDelay(pdMS_TO_TICKS(30));
  }
  driver.clearAllLEDs();
}

void runWaiting(BoardDriver& driver, std::atomic<bool>* stopFlag) {
  static const int positions[][2] = {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {1, 7}, {2, 7}, {3, 7}, {4, 7}, {5, 7}, {6, 7}, {7, 7}, {7, 6}, {7, 5}, {7, 4}, {7, 3}, {7, 2}, {7, 1}, {7, 0}, {6, 0}, {5, 0}, {4, 0}, {3, 0}, {2, 0}, {1, 0}};
  static const int numPositions = sizeof(positions) / sizeof(positions[0]);

  int frame = 0;
  while (!stopFlag || !stopFlag->load()) {
    driver.clearAllLEDs(false);
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 2; j++) {
        int idx = (frame + i + (j * 14)) % numPositions;
        driver.setSquareLED(positions[idx][0], positions[idx][1], LedColors::White);
      }
    }
    driver.showLEDs();
    frame = (frame + 1) % numPositions;
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  driver.clearAllLEDs();
}

}  // namespace

namespace BoardAnimations {

void execute(BoardDriver& driver, const AnimationJob& job) {
  switch (job.type) {
    case AnimationType::CAPTURE:
      runCapture(driver, job.params.capture.row, job.params.capture.col);
      break;
    case AnimationType::PROMOTION:
      runPromotion(driver, job.params.promotion.col);
      break;
    case AnimationType::BLINK:
      runBlink(driver, job.params.blink.row, job.params.blink.col, job.params.blink.color, job.params.blink.times, job.params.blink.clearAfter, job.params.blink.clearBefore);
      break;
    case AnimationType::WAITING:
      runWaiting(driver, job.stopFlag);
      break;
    case AnimationType::THINKING:
      runThinking(driver, job.stopFlag);
      break;
    case AnimationType::FIREWORK:
      runFirework(driver, job.params.firework.color);
      break;
    case AnimationType::FLASH:
      runFlash(driver, job.params.flash.color, job.params.flash.times);
      break;
    case AnimationType::SYNC:
      break;
  }
}

void runConnecting(BoardDriver& driver) {
  for (int i = 0; i < 8; i++) {
    driver.setSquareLED(3, i, LedColors::Blue);
    driver.setSquareLED(4, i, LedColors::Blue);
    driver.showLEDs();
    delay(100);
  }
  driver.clearAllLEDs();
}

}  // namespace BoardAnimations
