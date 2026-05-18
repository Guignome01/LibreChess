#include "board/runtime/runtime.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "board/runtime/calibration.h"

// ---------------------------------------------------------------------------
// BoardRuntime — wires driver + canvas + input + scheduler + renderer.
// ---------------------------------------------------------------------------
// `begin()` ordering matters:
//   1. driver_.begin() — initializes hardware.
//   2. BoardCalibrationRunner::load()/run()/save() — writes raw LEDs without a
//      mutex; safe because the renderer task isn't running yet.
//   3. Sync input baseline so programs don't see false "lifted" events
//      from board state captured during calibration.
//   4. Spawn the input poll task (sensor cadence ~40 ms).
//   5. Spawn the renderer task (~30 Hz) — from now on, ALL canvas
//      mutation must hold the canvas guard.
// ---------------------------------------------------------------------------

namespace {

constexpr UBaseType_t INPUT_TASK_PRIORITY = 1;
constexpr uint32_t INPUT_TASK_STACK_BYTES = 2048;
constexpr BaseType_t INPUT_TASK_CORE = 1;
constexpr TickType_t INPUT_TASK_STOP_TIMEOUT_TICKS = pdMS_TO_TICKS(250);

}  // namespace

void BoardRuntime::inputPollTrampoline(void* self) {
  static_cast<BoardRuntime*>(self)->inputPollLoop();
}

void BoardRuntime::inputPollLoop() {
  for (;;) {
    if (inputStopRequested_.load()) break;
    bool sensors[BoardInput::ROWS][BoardInput::COLS];
    readDebouncedSensors(sensors);
    takeInputMutex();
    if (!inputStopRequested_.load()) {
      input_.poll(sensors, millis());
    }
    giveInputMutex();
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SENSOR_READ_DELAY_MS)) > 0) break;
  }

  auto* exited = static_cast<SemaphoreHandle_t>(inputExitSemaphore_);
  if (exited != nullptr) {
    xSemaphoreGive(exited);
  }
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// CanvasGuard
// ---------------------------------------------------------------------------

CanvasGuard::CanvasGuard(BoardRuntime& runtime, BoardCanvas& canvas)
    : canvas(canvas), runtime_(runtime) {
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
      scheduler_(),
      renderer_(),
      inputMutex_(nullptr),
      inputTaskHandle_(nullptr),
      inputExitSemaphore_(nullptr),
      inputStopRequested_(false) {}

bool BoardRuntime::begin() {
  if (renderer_.running() && inputTaskHandle_ != nullptr) return true;
  if (renderer_.running() || inputTaskHandle_ != nullptr || inputMutex_ != nullptr) return false;

  driver_.begin();

  // Startup calibration: load saved mapping or run a fresh calibration.
  // Calibration drives raw LEDs and raw sensors directly; safe here
  // because the renderer task is not running yet.
  BoardCalibrationRunner calibration(driver_);
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

  // Capture the post-calibration sensor state as the input baseline and seed
  // the driver's debounce state from the same scan, so already-placed pieces
  // do not briefly appear as missing when the poll task starts.
  bool sensors[BoardInput::ROWS][BoardInput::COLS];
  driver_.syncSensorBaseline(sensors);
  takeInputMutex();
  input_.syncBaseline(sensors, millis());
  giveInputMutex();

  if (!startInputPollTask()) {
    Serial.println("BoardRuntime: input poll task startup failed");
    releaseInputMutex();
    return false;
  }

  if (!renderer_.begin(driver_, canvas_, scheduler_)) {
    Serial.println("BoardRuntime: renderer startup failed");
    const bool inputStopped = stopInputPollTask();
    if (inputStopped || inputTaskHandle_ == nullptr) {
      releaseInputMutex();
    }
    return false;
  }
  return true;
}

void BoardRuntime::shutdown() {
  if (!renderer_.running() && inputMutex_ == nullptr && inputTaskHandle_ == nullptr) return;

  renderer_.stop();
  const bool inputStopped = stopInputPollTask();
  if (inputStopped || inputTaskHandle_ == nullptr) {
    releaseInputMutex();
  }
}

CanvasGuard BoardRuntime::lockCanvas() {
  return CanvasGuard(*this, canvas_);
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
  batch.droppedEventCount = input_.droppedEventCount();
  batch.maxQueueDepth = input_.maxQueueDepth();
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
  inputStopRequested_.store(false);
  inputExitSemaphore_ = xSemaphoreCreateBinary();
  if (inputExitSemaphore_ == nullptr) return false;

  TaskHandle_t handle = nullptr;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      &BoardRuntime::inputPollTrampoline,
      "BoardInputPoll",
      INPUT_TASK_STACK_BYTES,
      this,
      INPUT_TASK_PRIORITY,
      &handle,
      INPUT_TASK_CORE);
  if (ok != pdPASS || handle == nullptr) {
    vSemaphoreDelete(static_cast<SemaphoreHandle_t>(inputExitSemaphore_));
    inputExitSemaphore_ = nullptr;
    return false;
  }
  inputTaskHandle_ = handle;
  return true;
}

bool BoardRuntime::stopInputPollTask() {
  if (inputTaskHandle_ == nullptr) return true;
  auto* exited = static_cast<SemaphoreHandle_t>(inputExitSemaphore_);
  if (exited == nullptr) {
    Serial.println("BoardRuntime: missing input exit semaphore during stop");
    return false;
  }
  inputStopRequested_.store(true);
  xTaskNotifyGive(static_cast<TaskHandle_t>(inputTaskHandle_));
  const bool stoppedGracefully = xSemaphoreTake(exited, INPUT_TASK_STOP_TIMEOUT_TICKS) == pdTRUE;
  if (!stoppedGracefully) {
    Serial.println("BoardRuntime: input poll task stop timed out; forcing task deletion");
    vTaskDelete(static_cast<TaskHandle_t>(inputTaskHandle_));
  }
  vSemaphoreDelete(exited);
  inputExitSemaphore_ = nullptr;
  inputTaskHandle_ = nullptr;
  inputStopRequested_.store(false);
  return stoppedGracefully;
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

void BoardRuntime::readDebouncedSensors(bool (&sensors)[BoardInput::ROWS][BoardInput::COLS]) {
  driver_.readSensors();
  for (int row = 0; row < BoardInput::ROWS; ++row) {
    for (int col = 0; col < BoardInput::COLS; ++col) {
      sensors[row][col] = driver_.getSensorState(row, col);
    }
  }
}
