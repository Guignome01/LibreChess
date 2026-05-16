#ifndef BOARD_RUNTIME_RUNTIME_H
#define BOARD_RUNTIME_RUNTIME_H

#include "board/runtime/canvas.h"
#include "board/runtime/driver.h"
#include "board/runtime/input.h"
#include "board/runtime/renderer.h"
#include "board/runtime/scheduler.h"

#include <atomic>
#include <stdint.h>

// ---------------------------------------------------------------------------
// BoardRuntime — board-internal runtime owner shared by board services/programs.
// ---------------------------------------------------------------------------
// Owns: BoardDriver (LEDs + sensors), BoardCanvas (ordered surfaces),
// BoardInput (debounced occupancy + event queue), BoardScheduler (timed
// painters), BoardRenderer (~30 Hz FreeRTOS flush task).
//
// External firmware consumes `Board` (the public package root); programs
// take `BoardRuntime&` directly via constructor.
//
// Synchronization: the renderer owns a single FreeRTOS mutex. Programs
// must mutate the canvas / schedule presentation work only while holding the
// `CanvasGuard` returned by `lockCanvas()`. The guard releases on
// destruction. The renderer holds the same mutex for the duration of one
// frame (microseconds).
//
// Startup calibration runs *before* the renderer task starts, so it can drive
// raw driver writes without contending for the mutex.
// ---------------------------------------------------------------------------

class BoardRuntime;

/// Snapshot of the input event queue drained under the runtime input mutex.
/// `overflowed` means at least one event was dropped before this drain, so
/// event-driven programs should discard the partial gesture and resync from
/// current occupancy.
struct BoardInputEventBatch {
  BoardInput::Event events[BoardInput::EVENT_QUEUE_SIZE];
  uint8_t count = 0;
  bool overflowed = false;
  uint32_t droppedEventCount = 0;
  uint8_t maxQueueDepth = 0;
};

/// RAII canvas accessor. Acquires the renderer mutex on construction, releases
/// on destruction. Programs mutate the canvas and schedule presentation work
/// while holding this guard; changes become visible when the renderer task next
/// wakes.
class CanvasGuard {
 public:
  CanvasGuard(BoardRuntime& runtime, BoardCanvas& canvas);
  ~CanvasGuard();

  CanvasGuard(const CanvasGuard&) = delete;
  CanvasGuard& operator=(const CanvasGuard&) = delete;
  CanvasGuard(CanvasGuard&&) = delete;
  CanvasGuard& operator=(CanvasGuard&&) = delete;

  BoardCanvas& canvas;

 private:
  BoardRuntime& runtime_;
};

class BoardRuntime {
 public:
  BoardRuntime();

  BoardRuntime(const BoardRuntime&) = delete;
  BoardRuntime& operator=(const BoardRuntime&) = delete;

  /// Initialize hardware, run startup calibration, sync input baseline,
  /// start input polling timer, start the renderer task.
  bool begin();

  /// Stop the renderer task and the input polling timer.
  void shutdown();

  /// Return whether the input poll task is believed to be active.
  bool inputPollRunning() const { return inputTaskHandle_ != nullptr; }

  /// Return whether the render task is believed to be active.
  bool rendererRunning() const { return renderer_.running(); }

  // -------------------------------------------------------------------------
  // Program/runtime accessors
  // -------------------------------------------------------------------------

  /// Acquire the renderer mutex for canvas/presentation mutation. Hold for
  /// microseconds only — the renderer also blocks on the same mutex.
  CanvasGuard lockCanvas();

  /// Return the latest debounced occupancy for one square. Thread-safe.
  bool inputOccupied(int row, int col);

  /// Copy the full debounced occupancy matrix under the input mutex.
  void copyInputOccupancy(bool (&out)[BoardInput::ROWS][BoardInput::COLS]);

  /// Drain all queued input events under the input mutex. Also clears the
  /// overflow flag. Event-driven programs should process the returned batch
  /// outside the mutex so the poll task is never blocked by gameplay logic.
  BoardInputEventBatch drainInputEvents();

  /// Drop queued input events and clear overflow. Does not alter occupancy.
  void clearInputEvents();

  /// Direct canvas access (for read-only queries). Mutation must go
  /// through `lockCanvas()`.
  const BoardCanvas& canvas() const { return canvas_; }

  /// Mutable canvas access for board-owned visual adapters. Callers must hold
  /// `lockCanvas()` before scheduling/cancelling painters.
  BoardCanvas& presentationCanvas() { return canvas_; }

  /// Scheduler access for board-owned visual adapters. Callers must hold `lockCanvas()`
  /// before scheduling/cancelling painters.
  BoardScheduler& presentationScheduler() { return scheduler_; }

  // -------------------------------------------------------------------------
  // LED settings (passthrough to driver)
  // -------------------------------------------------------------------------

  uint8_t getBrightness() const { return driver_.getBrightness(); }
  uint8_t getDimMultiplier() const { return driver_.getDimMultiplier(); }
  void setBrightness(uint8_t value);
  void setDimMultiplier(uint8_t value);
  void saveLedSettings() { driver_.saveLedSettings(); }

  /// Sensor poll cadence (ms). Main loop should `delay(cadenceMs())`.
  uint16_t cadenceMs() const { return SENSOR_READ_DELAY_MS; }

  // -------------------------------------------------------------------------
  // Internals exposed to CanvasGuard / friends
  // -------------------------------------------------------------------------

  /// Renderer mutex accessor used by CanvasGuard. Not for general use.
  void* mutexHandle();

 private:
  friend class CanvasGuard;

  BoardDriver driver_;
  BoardCanvas canvas_;
  BoardInput input_;
  BoardScheduler scheduler_;
  BoardRenderer renderer_;

  // Input state is produced by the input poll task and consumed by programs.
  // It has its own mutex so long canvas/animation operations never delay sensor
  // ingestion, and input reads never wait for LED rendering.
  void* inputMutex_;

  // Input poll task handle (FreeRTOS) — opaque to keep this header
  // platform-agnostic.
  void* inputTaskHandle_;
  void* inputExitSemaphore_;
  std::atomic<bool> inputStopRequested_;
  bool startInputPollTask();
  bool stopInputPollTask();
  void releaseInputMutex();
  void takeInputMutex();
  void giveInputMutex();
  void readDebouncedSensors(bool (&sensors)[BoardInput::ROWS][BoardInput::COLS]);

  /// Body of the input polling task. Reads driver sensors at sensor
  /// cadence and feeds them into BoardInput until shutdown is requested.
  void inputPollLoop();
  static void inputPollTrampoline(void* self);
};

#endif  // BOARD_RUNTIME_RUNTIME_H
