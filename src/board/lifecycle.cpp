#include "lifecycle.h"

#include "driver.h"

#include <new>

BoardAnimationLifecycle::BoardAnimationLifecycle()
    : driver_(nullptr), queue_(nullptr), taskHandle_(nullptr), ledMutex_(nullptr), doneSemaphore_(nullptr), initialized_(false) {}

bool BoardAnimationLifecycle::begin(BoardDriver* driver) {
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

void BoardAnimationLifecycle::acquireLEDs() {
  if (!initialized_) return;
  xSemaphoreTake(ledMutex_, portMAX_DELAY);
}

void BoardAnimationLifecycle::releaseLEDs() {
  if (!initialized_) return;
  xSemaphoreGive(ledMutex_);
}

void BoardAnimationLifecycle::showConnectingAnimation() {
  if (!initialized_ || !driver_) return;
  acquireLEDs();
  BoardAnimations::runConnecting(*driver_);
  releaseLEDs();
}

void BoardAnimationLifecycle::fireworkAnimation(LedRGB color) {
  AnimationJob job = {AnimationType::FIREWORK, nullptr, {}};
  job.params.firework = {color};
  enqueue(job);
}

void BoardAnimationLifecycle::captureAnimation(int row, int col) {
  AnimationJob job = {AnimationType::CAPTURE, nullptr, {}};
  job.params.capture = {row, col};
  enqueue(job);
}

void BoardAnimationLifecycle::promotionAnimation(int col) {
  AnimationJob job = {AnimationType::PROMOTION, nullptr, {}};
  job.params.promotion.col = col;
  enqueue(job);
}

void BoardAnimationLifecycle::blinkSquare(int row, int col, LedRGB color, int times, bool clearAfter, bool clearBefore) {
  AnimationJob job = {AnimationType::BLINK, nullptr, {}};
  job.params.blink = {row, col, color, times, clearAfter, clearBefore};
  enqueue(job);
}

void BoardAnimationLifecycle::flashBoardAnimation(LedRGB color, int times) {
  AnimationJob job = {AnimationType::FLASH, nullptr, {}};
  job.params.flash = {color, times};
  enqueue(job);
}

std::atomic<bool>* BoardAnimationLifecycle::startThinkingAnimation() {
  if (!initialized_) return nullptr;
  auto* stopFlag = new (std::nothrow) std::atomic<bool>(false);
  if (!stopFlag) return nullptr;
  AnimationJob job = {AnimationType::THINKING, stopFlag, {}};
  if (!enqueue(job)) {
    delete stopFlag;
    return nullptr;
  }
  return stopFlag;
}

std::atomic<bool>* BoardAnimationLifecycle::startWaitingAnimation() {
  if (!initialized_) return nullptr;
  auto* stopFlag = new (std::nothrow) std::atomic<bool>(false);
  if (!stopFlag) return nullptr;
  AnimationJob job = {AnimationType::WAITING, stopFlag, {}};
  if (!enqueue(job)) {
    delete stopFlag;
    return nullptr;
  }
  return stopFlag;
}

void BoardAnimationLifecycle::stopAndWaitForAnimation(std::atomic<bool>*& stopFlag) {
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

void BoardAnimationLifecycle::waitForAnimationQueueDrain() {
  if (!initialized_) return;
  AnimationJob job = {AnimationType::SYNC, nullptr, {}};
  if (!enqueue(job)) return;
  xSemaphoreTake(doneSemaphore_, portMAX_DELAY);
}

void BoardAnimationLifecycle::workerTask(void* param) {
  static_cast<BoardAnimationLifecycle*>(param)->runWorker();
}

void BoardAnimationLifecycle::runWorker() {
  AnimationJob job;
  while (true) {
    if (xQueueReceive(queue_, &job, portMAX_DELAY) == pdTRUE) {
      acquireLEDs();
      if (driver_)
        BoardAnimations::execute(*driver_, job);
      releaseLEDs();
      signalCompletionFor(job);
    }
  }
}

bool BoardAnimationLifecycle::enqueue(const AnimationJob& job) {
  return initialized_ && xQueueSend(queue_, &job, portMAX_DELAY) == pdTRUE;
}

void BoardAnimationLifecycle::releaseResources() {
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

void BoardAnimationLifecycle::signalCompletionFor(const AnimationJob& job) {
  if (job.type == AnimationType::THINKING || job.type == AnimationType::WAITING || job.type == AnimationType::SYNC)
    xSemaphoreGive(doneSemaphore_);
}