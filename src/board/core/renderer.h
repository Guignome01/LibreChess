#ifndef BOARD_CORE_RENDERER_H
#define BOARD_CORE_RENDERER_H

#include "board/core/canvas.h"
#include "board/core/effects.h"

#include <stdint.h>

class BoardDriver;

// ---------------------------------------------------------------------------
// BoardRenderer — periodic FreeRTOS flush task at ~30 Hz.
// ---------------------------------------------------------------------------
// Lifecycle: BoardRuntime constructs the renderer, then calls `begin()`
// once driver + canvas + effects + input are ready. `begin()` creates the
// LED mutex and spawns the render task on Core 1 at priority 1 with a
// 4 KiB stack.
//
// Task body (~30 Hz tick):
//   1. take mutex
//   2. effects.step(now, canvas)        — paints frames into layers
//   3. if canvas.dirty(): canvas.compose(out); push to driver; show()
//   4. release mutex
//   5. wait for stop notification or next 33 ms tick
//
// NeoPixelBus on ESP32 (I2S/RMT) is asynchronous DMA — `Show()` queues
// the buffer and returns; hardware streams the strip without blocking
// the task. The mutex therefore stays held only for the time to paint,
// compose, and queue (microseconds), not for the duration of an
// animation.
//
// `dirty()` short-circuit: an idle canvas costs only one mutex round-trip
// per tick (~50 us).
// ---------------------------------------------------------------------------

class BoardRenderer {
 public:
  BoardRenderer();

  BoardRenderer(const BoardRenderer&) = delete;
  BoardRenderer& operator=(const BoardRenderer&) = delete;

  /// Allocate the LED mutex and spawn the render task. Returns false if
  /// FreeRTOS allocation fails. Safe to call exactly once.
  bool begin(BoardDriver& driver, BoardCanvas& canvas, BoardEffects& effects);

  /// Signal the task to exit and join. Idempotent.
  void stop();

  /// Opaque handle for `BoardRuntime::lockCanvas`. Caller treats it as a
  /// FreeRTOS `SemaphoreHandle_t`. nullptr until `begin()` succeeds.
  void* mutex() { return mutex_; }

 private:
  static void taskTrampoline(void* self);
  void taskBody();

  BoardDriver* driver_;
  BoardCanvas* canvas_;
  BoardEffects* effects_;
  void* mutex_;       // SemaphoreHandle_t (FreeRTOS)
  void* taskHandle_;  // TaskHandle_t (FreeRTOS)
  void* exitSemaphore_;  // SemaphoreHandle_t signalled by task exit.
};

#endif  // BOARD_CORE_RENDERER_H
