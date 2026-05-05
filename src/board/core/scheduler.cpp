#include "scheduler.h"

#include "driver.h"
#include "system.h"

#include <new>

BoardScheduler::BoardScheduler()
    : driver_(nullptr), queue_(nullptr), taskHandle_(nullptr), ledMutex_(nullptr), doneSemaphore_(nullptr), initialized_(false) {}

bool BoardScheduler::begin(BoardDriver* driver) {
  driver_ = driver;
  ledMutex_ = xSemaphoreCreateMutex();
  doneSemaphore_ = xSemaphoreCreateBinary();
  queue_ = xQueueCreate(8, sizeof(AnimationJob));

  if (!ledMutex_ || !doneSemaphore_ || !queue_) {
    releaseResources();
    return false;
  }

  if (xTaskCreatePinnedToCore(workerTask, "AnimWorker", 4096, this, 1, &taskHandle_, 1) != pdPASS) {
    releaseResources();
    return false;
  }

  initialized_ = true;
  return true;
}

void BoardScheduler::acquireLEDs() {
  if (!initialized_) return;
  xSemaphoreTake(ledMutex_, portMAX_DELAY);
}

void BoardScheduler::releaseLEDs() {
  if (!initialized_) return;
  xSemaphoreGive(ledMutex_);
}

bool BoardScheduler::runAnimation(const AnimationJob& job) {
  return enqueue(job);
}

void BoardScheduler::runAnimationNow(const AnimationJob& job) {
  if (!initialized_ || !driver_) return;
  acquireLEDs();
  BoardLEDBatch leds(*driver_);
  BoardAnimations::execute(leds, job);
  releaseLEDs();
}

std::atomic<bool>* BoardScheduler::startAnimation(AnimationType type) {
  if (!initialized_ || !BoardAnimations::isCancellable(type)) return nullptr;
  auto* stopFlag = new (std::nothrow) std::atomic<bool>(false);
  if (!stopFlag) return nullptr;

  AnimationJob job = (type == AnimationType::THINKING)
                         ? AnimationJob::thinking(stopFlag)
                         : AnimationJob::waiting(stopFlag);
  if (!enqueue(job)) {
    delete stopFlag;
    return nullptr;
  }
  return stopFlag;
}

void BoardScheduler::stopAndWaitForAnimation(std::atomic<bool>*& stopFlag) {
  if (!stopFlag) return;
  stopFlag->store(true);
  if (!initialized_) {
    delete stopFlag;
    stopFlag = nullptr;
    return;
  }
  xSemaphoreTake(doneSemaphore_, portMAX_DELAY);
  delete stopFlag;
  stopFlag = nullptr;
}

void BoardScheduler::waitForAnimationQueueDrain() {
  if (!initialized_) return;
  AnimationJob job = AnimationJob::sync();
  if (!enqueue(job)) return;
  xSemaphoreTake(doneSemaphore_, portMAX_DELAY);
}

void BoardScheduler::workerTask(void* param) {
  static_cast<BoardScheduler*>(param)->runWorker();
}

void BoardScheduler::runWorker() {
  AnimationJob job;
  while (true) {
    if (xQueueReceive(queue_, &job, portMAX_DELAY) == pdTRUE) {
      if (driver_) {
        acquireLEDs();
        BoardLEDBatch leds(*driver_);
        BoardAnimations::execute(leds, job);
        releaseLEDs();
      }
      signalCompletionFor(job);
    }
  }
}

bool BoardScheduler::enqueue(const AnimationJob& job) {
  return initialized_ && xQueueSend(queue_, &job, portMAX_DELAY) == pdTRUE;
}

void BoardScheduler::releaseResources() {
  if (queue_) {
    vQueueDelete(queue_);
    queue_ = nullptr;
  }
  if (doneSemaphore_) {
    vSemaphoreDelete(doneSemaphore_);
    doneSemaphore_ = nullptr;
  }
  if (ledMutex_) {
    vSemaphoreDelete(ledMutex_);
    ledMutex_ = nullptr;
  }
  taskHandle_ = nullptr;
  driver_ = nullptr;
  initialized_ = false;
}

void BoardScheduler::signalCompletionFor(const AnimationJob& job) {
  if (BoardAnimations::signalsCompletion(job.type))
    xSemaphoreGive(doneSemaphore_);
}
