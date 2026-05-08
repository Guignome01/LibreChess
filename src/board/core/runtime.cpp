#include "board/core/runtime.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "board/workflows/calibration.h"

// ---------------------------------------------------------------------------
// BoardRuntime — wires driver + canvas + input + effects + renderer.
// ---------------------------------------------------------------------------
// `begin()` ordering matters:
//   1. driver_.begin() — initializes hardware.
//   2. BoardCalibration::load()/run()/save() — writes raw LEDs without a
//      mutex; safe because the renderer task isn't running yet.
//   3. Sync input baseline so workflows don't see false "lifted" events
//      from board state captured during calibration.
//   4. Spawn the input poll task (sensor cadence ~40 ms).
//   5. Spawn the renderer task (~30 Hz) — from now on, ALL canvas
//      mutation must hold the canvas guard.
// ---------------------------------------------------------------------------

namespace {

constexpr UBaseType_t INPUT_TASK_PRIORITY = 1;
constexpr uint32_t INPUT_TASK_STACK_BYTES = 2048;
constexpr BaseType_t INPUT_TASK_CORE = 1;

}  // namespace

void BoardRuntime::inputPollTrampoline(void* self) {
  static_cast<BoardRuntime*>(self)->inputPollLoop();
}

void BoardRuntime::inputPollLoop() {
  for (;;) {
    driver_.readSensors();
    bool sensors[8][8];
    for (int r = 0; r < 8; ++r) {
      for (int c = 0; c < 8; ++c) {
        sensors[r][c] = driver_.getSensorState(r, c);
      }
    }
    takeInputMutex();
    input_.poll(sensors, millis());
    giveInputMutex();
    vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_DELAY_MS));
  }
}

// ---------------------------------------------------------------------------
// CanvasGuard
// ---------------------------------------------------------------------------

CanvasGuard::CanvasGuard(BoardRuntime& runtime, BoardCanvas& canvas, BoardEffects& effects)
    : canvas(canvas), effects(effects), runtime_(runtime) {
  auto* mtx = static_cast<SemaphoreHandle_t>(runtime.mutexHandle());
  if (mtx != nullptr) {
    xSemaphoreTake(mtx, portMAX_DELAY);
  }
}

CanvasGuard::~CanvasGuard() {
  auto* mtx = static_cast<SemaphoreHandle_t>(runtime_.mutexHandle());
  if (mtx != nullptr) {
    xSemaphoreGive(mtx);
  }
}

// ---------------------------------------------------------------------------
// BoardRuntime
// ---------------------------------------------------------------------------

BoardRuntime::BoardRuntime()
    : driver_(),
      canvas_(),
      input_(),
      effects_(),
      renderer_(),
      inputMutex_(nullptr),
      inputTaskHandle_(nullptr) {}

bool BoardRuntime::begin() {
  driver_.begin();

  // Startup calibration: load saved mapping or run a fresh calibration.
  // Calibration drives raw LEDs and raw sensors directly; safe here
  // because the renderer task is not running yet.
  BoardCalibration calibration(driver_);
  if (!calibration.load()) {
    const bool wasSkipped = calibration.run();
    if (!wasSkipped) {
      calibration.save();
    }
  }

  inputMutex_ = xSemaphoreCreateMutex();
  if (inputMutex_ == nullptr) {
    Serial.println("BoardRuntime: input mutex allocation failed");
    return false;
  }

  // Capture the post-calibration sensor state as the input baseline so we
  // don't emit phantom LIFTED events for pieces that were already on the
  // board.
  driver_.readSensors();
  bool sensors[8][8];
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      sensors[r][c] = driver_.getSensorState(r, c);
    }
  }
  takeInputMutex();
  input_.syncBaseline(sensors, millis());
  giveInputMutex();

  if (!startInputPollTask()) {
    Serial.println("BoardRuntime: input poll task startup failed");
    releaseInputMutex();
    return false;
  }

  if (!renderer_.begin(driver_, canvas_, effects_)) {
    Serial.println("BoardRuntime: renderer startup failed");
    stopInputPollTask();
    releaseInputMutex();
    return false;
  }
  return true;
}

void BoardRuntime::shutdown() {
  renderer_.stop();
  stopInputPollTask();
  releaseInputMutex();
}

CanvasGuard BoardRuntime::lockCanvas() {
  return CanvasGuard(*this, canvas_, effects_);
}

void* BoardRuntime::mutexHandle() {
  return renderer_.mutex();
}

void BoardRuntime::setBrightness(uint8_t value) {
  // Driver writes go through the renderer mutex so a concurrent show()
  // doesn't race with the brightness change.
  auto g = lockCanvas();
  (void)g;
  driver_.setBrightness(value);
}

void BoardRuntime::setDimMultiplier(uint8_t value) {
  auto g = lockCanvas();
  (void)g;
  driver_.setDimMultiplier(value);
}

bool BoardRuntime::inputOccupied(int row, int col) {
  takeInputMutex();
  const bool occupied = input_.occupied(row, col);
  giveInputMutex();
  return occupied;
}

void BoardRuntime::copyInputOccupancy(bool (&out)[BoardInput::ROWS][BoardInput::COLS]) {
  takeInputMutex();
  for (int row = 0; row < BoardInput::ROWS; ++row) {
    for (int col = 0; col < BoardInput::COLS; ++col) {
      out[row][col] = input_.occupied(row, col);
    }
  }
  giveInputMutex();
}

BoardInputEventBatch BoardRuntime::drainInputEvents() {
  BoardInputEventBatch batch;
  takeInputMutex();
  batch.overflowed = input_.overflowed();
  batch.count = input_.eventCount();
  for (uint8_t i = 0; i < batch.count; ++i) {
    batch.events[i] = input_.peek(i);
  }
  input_.clearEvents();
  input_.clearOverflow();
  giveInputMutex();
  return batch;
}

void BoardRuntime::clearInputEvents() {
  takeInputMutex();
  input_.clearEvents();
  input_.clearOverflow();
  giveInputMutex();
}

bool BoardRuntime::startInputPollTask() {
  if (inputTaskHandle_ != nullptr) return true;
  TaskHandle_t handle = nullptr;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      &BoardRuntime::inputPollTrampoline,
      "BoardInputPoll",
      INPUT_TASK_STACK_BYTES,
      this,
      INPUT_TASK_PRIORITY,
      &handle,
      INPUT_TASK_CORE);
  if (ok != pdPASS || handle == nullptr) return false;
  inputTaskHandle_ = handle;
  return true;
}

void BoardRuntime::stopInputPollTask() {
  if (inputTaskHandle_ == nullptr) return;
  vTaskDelete(static_cast<TaskHandle_t>(inputTaskHandle_));
  inputTaskHandle_ = nullptr;
}

void BoardRuntime::releaseInputMutex() {
  if (inputMutex_ == nullptr) return;
  vSemaphoreDelete(static_cast<SemaphoreHandle_t>(inputMutex_));
  inputMutex_ = nullptr;
}

void BoardRuntime::takeInputMutex() {
  auto* mtx = static_cast<SemaphoreHandle_t>(inputMutex_);
  if (mtx != nullptr) {
    xSemaphoreTake(mtx, portMAX_DELAY);
  }
}

void BoardRuntime::giveInputMutex() {
  auto* mtx = static_cast<SemaphoreHandle_t>(inputMutex_);
  if (mtx != nullptr) {
    xSemaphoreGive(mtx);
  }
}
