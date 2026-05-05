#include "system.h"

namespace {

void snapshotOccupancy(BoardDriver& driver, bool (&occupancy)[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS]) {
  for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row) {
    for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
      occupancy[row][col] = driver.getSensorState(row, col);
    }
  }
}

}  // namespace

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

BoardSystem::BoardSystem() : driver_(), scheduler_(), state_() {}

bool BoardSystem::begin() {
  driver_.begin();
  return scheduler_.begin(&driver_);
}

void BoardSystem::readSensors() {
  driver_.readSensors();
  refreshState(false);
}

void BoardSystem::syncOccupancyBaseline() {
  refreshState(true);
}

bool BoardSystem::occupied(int row, int col) const {
  return state_.occupied(row, col);
}

bool BoardSystem::wasOccupied(int row, int col) const {
  return state_.wasOccupied(row, col);
}

bool BoardSystem::wasLifted(int row, int col) const {
  return state_.wasLifted(row, col);
}

bool BoardSystem::wasPlaced(int row, int col) const {
  return state_.wasPlaced(row, col);
}

bool BoardSystem::changed(int row, int col) const {
  return state_.changed(row, col);
}

uint8_t BoardSystem::changedCount() const {
  return state_.changedCount();
}

LibreChess::board::BoardSquare BoardSystem::changedSquare(uint8_t index) const {
  return state_.changedSquare(index);
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

void BoardSystem::triggerCalibration() {
  driver_.triggerCalibration();
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

void BoardSystem::refreshState(bool initializeBaseline) {
  bool occupancy[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS];
  snapshotOccupancy(driver_, occupancy);

  if (initializeBaseline)
    state_.sync(occupancy);
  else
    state_.update(occupancy);
}