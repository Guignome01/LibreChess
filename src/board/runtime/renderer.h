#ifndef BOARD_RUNTIME_RENDERER_H
#define BOARD_RUNTIME_RENDERER_H

#include "board/runtime/canvas.h"
#include "board/runtime/scheduler.h"

#include <stdint.h>

class BoardDriver;

// ---------------------------------------------------------------------------
// BoardRenderer — periodic FreeRTOS flush task at ~30 Hz.
// ---------------------------------------------------------------------------
// Lifecycle: BoardRuntime constructs the renderer, then calls `begin()`
// once driver + canvas + scheduler + input are ready. `begin()` creates the
// LED mutex and spawns the render task on Core 1 at priority 1 with a
// 4 KiB stack.
//
// Task body (~30 Hz cadence):
//   1. take mutex
//   2. scheduler.run(now, canvas)       — runs scheduled painters
//   3. if canvas.dirty(): canvas.compose(out); push to driver; show()
//   4. release mutex
//   5. wait for stop notification or the next 33 ms wake
//
// NeoPixelBus on ESP32 (I2S/RMT) is asynchronous DMA — `Show()` queues
// the buffer and returns; hardware streams the strip without blocking
// the task. The mutex therefore stays held only for the time to paint,
// compose, and queue (microseconds), not for the duration of an
// animation.
//
// `dirty()` short-circuit: an idle canvas costs only one mutex round-trip
// per wake (~50 us).
// ---------------------------------------------------------------------------

class BoardRenderer {
 public:
  BoardRenderer();

  BoardRenderer(const BoardRenderer&) = delete;
  BoardRenderer& operator=(const BoardRenderer&) = delete;

  /// Allocate the LED mutex and spawn the render task. Returns false if
  /// FreeRTOS allocation fails. Safe to call exactly once.
  bool begin(BoardDriver& driver, BoardCanvas& canvas, BoardScheduler& scheduler);

  /// Signal the task to exit and join. Returns false if the task does not
  /// acknowledge shutdown before the bounded wait expires. Idempotent.
  bool stop();

  /// Return whether the render task is believed to be active.
  bool running() const { return taskHandle_ != nullptr; }

  /// Opaque handle for `BoardRuntime::lockCanvas`. Caller treats it as a
  /// FreeRTOS `SemaphoreHandle_t`. nullptr until `begin()` succeeds.
  void* mutex() { return mutex_; }

 private:
  static void taskTrampoline(void* self);
  void taskBody();

  BoardDriver* driver_;
  BoardCanvas* canvas_;
  BoardScheduler* scheduler_;
  void* mutex_;       // SemaphoreHandle_t (FreeRTOS)
  void* taskHandle_;  // TaskHandle_t (FreeRTOS)
  void* exitSemaphore_;  // SemaphoreHandle_t signalled by task exit.
};

#endif  // BOARD_RUNTIME_RENDERER_H
