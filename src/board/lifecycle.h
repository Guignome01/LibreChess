#ifndef BOARD_LIFECYCLE_H
#define BOARD_LIFECYCLE_H

#include "animations.h"

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

class BoardDriver;

// Board-internal lifecycle owner for asynchronous LED animation execution.
class BoardAnimationLifecycle {
 public:
  BoardAnimationLifecycle();

  /// Initialize queue, mutex, completion semaphore, and worker task.
  bool begin(BoardDriver* driver);

  /// Block until direct LED writes can safely use the strip.
  void acquireLEDs();

  /// Release the strip after a direct LED write batch.
  void releaseLEDs();

  /// Run the one-shot connecting animation synchronously under the LED mutex.
  void showConnectingAnimation();

  /// Queue fire-and-forget animations.
  void fireworkAnimation(LedRGB color = LedColors::White);
  void captureAnimation(int row, int col);
  void promotionAnimation(int col);
  void blinkSquare(int row, int col, LedRGB color, int times = 3, bool clearAfter = true, bool clearBefore = false);
  void flashBoardAnimation(LedRGB color, int times = 3);

  /// Queue cancellable long-running animations and return their stop flag.
  std::atomic<bool>* startThinkingAnimation();
  std::atomic<bool>* startWaitingAnimation();

  /// Stop a cancellable animation, wait for worker completion, and delete flag.
  void stopAndWaitForAnimation(std::atomic<bool>*& stopFlag);

  /// Block until all previously queued animations have finished executing.
  void waitForAnimationQueueDrain();

 private:
  BoardDriver* driver_;
  QueueHandle_t queue_;
  TaskHandle_t taskHandle_;
  SemaphoreHandle_t ledMutex_;
  SemaphoreHandle_t doneSemaphore_;
  bool initialized_;

  static void workerTask(void* param);
  void runWorker();
  bool enqueue(const AnimationJob& job);
  void releaseResources();
  void signalCompletionFor(const AnimationJob& job);
};

#endif  // BOARD_LIFECYCLE_H