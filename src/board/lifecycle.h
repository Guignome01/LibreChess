#ifndef BOARD_LIFECYCLE_H
#define BOARD_LIFECYCLE_H

#include "animations.h"

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

class BoardDriver;
class BoardSystem;

// Board-internal lifecycle owner for asynchronous LED animation execution.
class BoardAnimationLifecycle {
 public:
  BoardAnimationLifecycle();

  /// Initialize queue, mutex, completion semaphore, and worker task.
  bool begin(BoardDriver* driver);

  /// Queue one prebuilt fire-and-forget animation job.
  bool runAnimation(const AnimationJob& job);

  /// Execute one prebuilt animation immediately under the LED mutex.
  void runAnimationNow(const AnimationJob& job);

  /// Queue one cancellable long-running animation and return its stop flag.
  std::atomic<bool>* startAnimation(AnimationType type);

  /// Stop a cancellable animation, wait for worker completion, and delete flag.
  void stopAndWaitForAnimation(std::atomic<bool>*& stopFlag);

  /// Block until all previously queued animations have finished executing.
  void waitForAnimationQueueDrain();

 private:
  friend class BoardSystem;

  BoardDriver* driver_;
  QueueHandle_t queue_;
  TaskHandle_t taskHandle_;
  SemaphoreHandle_t ledMutex_;
  SemaphoreHandle_t doneSemaphore_;
  bool initialized_;

  void acquireLEDs();
  void releaseLEDs();
  static void workerTask(void* param);
  void runWorker();
  bool enqueue(const AnimationJob& job);
  void releaseResources();
  void signalCompletionFor(const AnimationJob& job);
};

#endif  // BOARD_LIFECYCLE_H