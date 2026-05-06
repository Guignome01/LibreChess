#include "system.h"

#include "board/calibration.h"

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

BoardSystem::BoardSystem() : driver_(), scheduler_() {}

bool BoardSystem::begin() {
  driver_.begin();
  BoardCalibrationWorkflow calibration(driver_);
  if (!calibration.load()) {
    bool wasSkipped = calibration.run();
    if (!wasSkipped) {
      calibration.save();
    }
  }
  return scheduler_.begin(&driver_);
}

void BoardSystem::readSensors() {
  driver_.readSensors();
}

bool BoardSystem::occupied(int row, int col) const {
  return driver_.getSensorState(row, col);
}

void BoardSystem::clearAllLEDs(bool show) {
  batchLEDs([&](LEDWriter& leds) {
    leds.clearAllLEDs(show);
  });
}

void BoardSystem::setSquareLED(int row, int col, LedRGB color) {
  batchLEDs([&](LEDWriter& leds) {
    leds.setSquareLED(row, col, color);
  });
}

void BoardSystem::showLEDs() {
  batchLEDs([&](LEDWriter& leds) {
    leds.showLEDs();
  });
}

bool BoardSystem::runAnimation(const AnimationJob& job) {
  return scheduler_.runAnimation(job);
}

void BoardSystem::runAnimationNow(const AnimationJob& job) {
  scheduler_.runAnimationNow(job);
}

std::atomic<bool>* BoardSystem::startAnimation(AnimationType type) {
  return scheduler_.startAnimation(type);
}

void BoardSystem::stopAndWaitForAnimation(std::atomic<bool>*& stopFlag) {
  scheduler_.stopAndWaitForAnimation(stopFlag);
}

void BoardSystem::waitForAnimationQueueDrain() {
  scheduler_.waitForAnimationQueueDrain();
}

uint8_t BoardSystem::getBrightness() const {
  return driver_.getBrightness();
}

uint8_t BoardSystem::getDimMultiplier() const {
  return driver_.getDimMultiplier();
}

void BoardSystem::setBrightness(uint8_t value) {
  batchLEDs([&](LEDWriter& leds) {
    leds.setBrightness(value);
  });
}

void BoardSystem::setDimMultiplier(uint8_t value) {
  batchLEDs([&](LEDWriter& leds) {
    leds.setDimMultiplier(value);
  });
}

void BoardSystem::saveLedSettings() {
  driver_.saveLedSettings();
}

BoardCalibrationWorkflow BoardSystem::makeCalibrationWorkflow() {
  return BoardCalibrationWorkflow(driver_);
}

uint16_t BoardSystem::sensorReadDelayMs() const {
  return SENSOR_READ_DELAY_MS;
}

void BoardSystem::beginLEDBatch() {
  scheduler_.acquireLEDs();
}

void BoardSystem::endLEDBatch() {
  scheduler_.releaseLEDs();
}
