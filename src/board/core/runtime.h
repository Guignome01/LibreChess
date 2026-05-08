#ifndef BOARD_CORE_RUNTIME_H
#define BOARD_CORE_RUNTIME_H

#include "board/core/canvas.h"
#include "board/core/driver.h"
#include "board/core/effects.h"
#include "board/core/input.h"
#include "board/core/renderer.h"

#include <stdint.h>

class BoardCalibration;

// ---------------------------------------------------------------------------
// BoardRuntime — board-internal runtime owner shared by all board workflows.
// ---------------------------------------------------------------------------
// Owns: BoardDriver (LEDs + sensors), BoardCanvas (multi-layer surface),
// BoardInput (debounced occupancy + event queue), BoardEffects (retained
// tick-stepped animations), BoardRenderer (~30 Hz FreeRTOS flush task).
//
// External firmware consumes `Board` (the public package root); workflows
// take `BoardRuntime&` directly via constructor.
//
// Synchronization: the renderer owns a single FreeRTOS mutex. Workflows
// must mutate the canvas / start effects only while holding the
// `CanvasGuard` returned by `lockCanvas()`. The guard releases on
// destruction. The renderer holds the same mutex for the duration of one
// frame (microseconds).
//
// Calibration runs *before* the renderer task starts, so it can drive raw
// driver writes without contending for the mutex. `BoardCalibration` is a
// friend so it can call private `driver()` for raw access.
// ---------------------------------------------------------------------------

class BoardRuntime;

/// Snapshot of the input event queue drained under the runtime input mutex.
/// `overflowed` means at least one event was dropped before this drain, so
/// event-driven workflows should discard the partial gesture and resync from
/// current occupancy.
struct BoardInputEventBatch {
  BoardInput::Event events[BoardInput::EVENT_QUEUE_SIZE];
  uint8_t count = 0;
  bool overflowed = false;
};

/// RAII canvas + effects accessor. Acquires the renderer mutex on
/// construction, releases on destruction. Workflows mutate the canvas and
/// start/cancel effects through the public references; changes become
/// visible to the renderer on its next tick.
class CanvasGuard {
 public:
  CanvasGuard(BoardRuntime& runtime, BoardCanvas& canvas, BoardEffects& effects);
  ~CanvasGuard();

  CanvasGuard(const CanvasGuard&) = delete;
  CanvasGuard& operator=(const CanvasGuard&) = delete;
  CanvasGuard(CanvasGuard&&) = delete;
  CanvasGuard& operator=(CanvasGuard&&) = delete;

  BoardCanvas& canvas;
  BoardEffects& effects;

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

  // -------------------------------------------------------------------------
  // Workflow accessors
  // -------------------------------------------------------------------------

  /// Acquire the renderer mutex for canvas + effect mutation. Hold for
  /// microseconds only — the renderer also blocks on the same mutex.
  CanvasGuard lockCanvas();

  /// Return the latest debounced occupancy for one square. Thread-safe.
  bool inputOccupied(int row, int col);

  /// Copy the full debounced occupancy matrix under the input mutex.
  void copyInputOccupancy(bool (&out)[BoardInput::ROWS][BoardInput::COLS]);

  /// Drain all queued input events under the input mutex. Also clears the
  /// overflow flag. Event-driven workflows should process the returned batch
  /// outside the mutex so the poll task is never blocked by gameplay logic.
  BoardInputEventBatch drainInputEvents();

  /// Drop queued input events and clear overflow. Does not alter occupancy.
  void clearInputEvents();

  /// Direct canvas access (for read-only queries). Mutation must go
  /// through `lockCanvas()`.
  const BoardCanvas& canvas() const { return canvas_; }

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
  friend class BoardCalibration;
  friend class CanvasGuard;

  /// Friends' raw-driver access. Used during calibration before the
  /// renderer task is running.
  BoardDriver& driver() { return driver_; }

  BoardDriver driver_;
  BoardCanvas canvas_;
  BoardInput input_;
  BoardEffects effects_;
  BoardRenderer renderer_;

  // Input state is produced by the input poll task and consumed by workflows.
  // It has its own mutex so long canvas/effect operations never delay sensor
  // ingestion, and input reads never wait for LED rendering.
  void* inputMutex_;

  // Input poll task handle (FreeRTOS) — opaque to keep this header
  // platform-agnostic.
  void* inputTaskHandle_;
  bool startInputPollTask();
  void stopInputPollTask();
  void releaseInputMutex();
  void takeInputMutex();
  void giveInputMutex();

  /// Body of the input polling task. Reads driver sensors at sensor
  /// cadence and feeds them into BoardInput. Never returns; exits via
  /// vTaskDelete from `stopInputPollTask`.
  void inputPollLoop();
  static void inputPollTrampoline(void* self);
};

#endif  // BOARD_CORE_RUNTIME_H
