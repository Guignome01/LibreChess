#include "board/runtime/renderer.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "board/runtime/driver.h"

// ---------------------------------------------------------------------------
// Implementation notes
// ---------------------------------------------------------------------------
// NeoPixelBus method `NeoEsp32I2s0800KbpsMethod` (used by BoardDriver) issues
// `Show()` via the I2S peripheral with DMA-driven serialization. The call
// queues the buffer into the I2S FIFO and returns; the strip is written
// asynchronously. If a previous transmission is still in flight when we
// call `Show()` again, the library blocks until it completes — but at
// 30 Hz with an 8x8 strip the DMA finishes in ~256 µs, far below our 33 ms
// cadence period, so we never wait in practice.
// ---------------------------------------------------------------------------

namespace {

constexpr UBaseType_t RENDER_TASK_PRIORITY = 1;
constexpr uint32_t RENDER_TASK_STACK_BYTES = 4096;
constexpr BaseType_t RENDER_TASK_CORE = 1;
constexpr TickType_t RENDER_FRAME_DELAY_MS = 33;  // ~30 Hz
constexpr TickType_t RENDER_STOP_TIMEOUT_TICKS = pdMS_TO_TICKS(250);

}  // namespace

BoardRenderer::BoardRenderer()
    : driver_(nullptr),
      canvas_(nullptr),
      scheduler_(nullptr),
      mutex_(nullptr),
      taskHandle_(nullptr),
      exitSemaphore_(nullptr) {}

bool BoardRenderer::begin(BoardDriver& driver, BoardCanvas& canvas, BoardScheduler& scheduler) {
  if (taskHandle_ != nullptr) return true;  // already running.
  driver_ = &driver;
  canvas_ = &canvas;
  scheduler_ = &scheduler;

  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) return false;

  exitSemaphore_ = xSemaphoreCreateBinary();
  if (exitSemaphore_ == nullptr) {
    vSemaphoreDelete(static_cast<SemaphoreHandle_t>(mutex_));
    mutex_ = nullptr;
    return false;
  }

  TaskHandle_t handle = nullptr;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      &BoardRenderer::taskTrampoline,
      "BoardRenderer",
      RENDER_TASK_STACK_BYTES,
      this,
      RENDER_TASK_PRIORITY,
      &handle,
      RENDER_TASK_CORE);
  if (ok != pdPASS) {
    vSemaphoreDelete(static_cast<SemaphoreHandle_t>(exitSemaphore_));
    exitSemaphore_ = nullptr;
    vSemaphoreDelete(static_cast<SemaphoreHandle_t>(mutex_));
    mutex_ = nullptr;
    return false;
  }
  taskHandle_ = handle;
  return true;
}

bool BoardRenderer::stop() {
  if (taskHandle_ == nullptr) return true;
  TaskHandle_t handle = static_cast<TaskHandle_t>(taskHandle_);
  auto* exited = static_cast<SemaphoreHandle_t>(exitSemaphore_);
  if (exited == nullptr) {
    Serial.println("BoardRenderer: missing exit semaphore during stop");
    return false;
  }
  xTaskNotifyGive(handle);
  const bool stoppedGracefully = xSemaphoreTake(exited, RENDER_STOP_TIMEOUT_TICKS) == pdTRUE;
  if (!stoppedGracefully) {
    Serial.println("BoardRenderer: stop timed out; forcing task deletion");
    vTaskDelete(handle);
  }
  vSemaphoreDelete(exited);
  exitSemaphore_ = nullptr;
  taskHandle_ = nullptr;
  if (mutex_ != nullptr) {
    vSemaphoreDelete(static_cast<SemaphoreHandle_t>(mutex_));
    mutex_ = nullptr;
  }
  driver_ = nullptr;
  canvas_ = nullptr;
  scheduler_ = nullptr;
  return stoppedGracefully;
}

void BoardRenderer::taskTrampoline(void* self) {
  static_cast<BoardRenderer*>(self)->taskBody();
}

void BoardRenderer::taskBody() {
  SemaphoreHandle_t mtx = static_cast<SemaphoreHandle_t>(mutex_);
  for (;;) {
    const uint32_t now = millis();
    if (xSemaphoreTake(mtx, portMAX_DELAY) == pdTRUE) {
      scheduler_->run(now, *canvas_);
      if (canvas_->dirty()) {
        LedRGB out[BoardCanvas::ROWS][BoardCanvas::COLS];
        canvas_->compose(out);
        for (int r = 0; r < BoardCanvas::ROWS; ++r) {
          for (int c = 0; c < BoardCanvas::COLS; ++c) {
            driver_->setSquareLED(r, c, out[r][c]);
          }
        }
        driver_->showLEDs();
      }
      xSemaphoreGive(mtx);
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RENDER_FRAME_DELAY_MS)) > 0) break;
  }

  auto* exited = static_cast<SemaphoreHandle_t>(exitSemaphore_);
  if (exited != nullptr) {
    xSemaphoreGive(exited);
  }
  vTaskDelete(nullptr);
}
