#include "controller.h"

#include "board/calibration.h"
#include "board/gui/animations.h"

BoardLEDBatch::BoardLEDBatch(BoardDriver& driver) : driver_(driver) {}

void BoardLEDBatch::clearAllLEDs(bool show) {
  driver_.clearAllLEDs(show);
}

void BoardLEDBatch::setSquareLED(int row, int col, LedRGB color) {
  driver_.setSquareLED(row, col, color);
}

void BoardLEDBatch::showLEDs() {
  driver_.showLEDs();
}

void BoardLEDBatch::setBrightness(uint8_t value) {
  driver_.setBrightness(value);
}

void BoardLEDBatch::setDimMultiplier(uint8_t value) {
  driver_.setDimMultiplier(value);
}

BoardController::BoardController()
    : driver_(),
      scheduler_(),
      layering_(*this),
      feedback_(*this, layering_),
      assistance_(*this, layering_),
      stack_() {}

bool BoardController::begin() {
  driver_.begin();

  BoardCalibration calibration(driver_);
  if (!calibration.load()) {
    bool wasSkipped = calibration.run();
    if (!wasSkipped) {
      calibration.save();
    }
  }

  return scheduler_.begin(&driver_);
}

void BoardController::readSensors() {
  driver_.readSensors();
}

bool BoardController::occupied(int row, int col) const {
  return driver_.getSensorState(row, col);
}

bool BoardController::runAnimation(const AnimationJob& job) {
  return scheduler_.runAnimation(job);
}

void BoardController::runAnimationNow(const AnimationJob& job) {
  scheduler_.runAnimationNow(job);
}

std::atomic<bool>* BoardController::startAnimation(AnimationType type) {
  return scheduler_.startAnimation(type);
}

void BoardController::stopAndWaitForAnimation(std::atomic<bool>*& stopFlag) {
  scheduler_.stopAndWaitForAnimation(stopFlag);
}

void BoardController::waitForAnimationQueueDrain() {
  scheduler_.waitForAnimationQueueDrain();
}

uint8_t BoardController::getBrightness() const {
  return driver_.getBrightness();
}

uint8_t BoardController::getDimMultiplier() const {
  return driver_.getDimMultiplier();
}

void BoardController::setBrightness(uint8_t value) {
  batchLEDs([&](LEDWriter& leds) {
    leds.setBrightness(value);
  });
}

void BoardController::setDimMultiplier(uint8_t value) {
  batchLEDs([&](LEDWriter& leds) {
    leds.setDimMultiplier(value);
  });
}

void BoardController::saveLedSettings() {
  driver_.saveLedSettings();
}

uint16_t BoardController::sensorReadDelayMs() const {
  return SENSOR_READ_DELAY_MS;
}

void BoardController::clearAllLEDs(bool show) {
  layering_.clearAll(show);
}

void BoardController::showConnectingAnimation() {
  layering_.clearAll(false);
  scheduler_.runAnimationNow(AnimationJob::connecting());
}

BoardLayering& BoardController::layering() {
  return layering_;
}

BoardFeedback& BoardController::feedback() {
  return feedback_;
}

BoardAssistance& BoardController::assistance() {
  return assistance_;
}

BoardStack& BoardController::stack() {
  return stack_;
}

void BoardController::beginLEDBatch() {
  scheduler_.acquireLEDs();
}

void BoardController::endLEDBatch() {
  scheduler_.releaseLEDs();
}