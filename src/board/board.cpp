#include "board.h"

Board::Board() : driver_(), feedback_(this), assistance_(this), state_() {}

void Board::begin() {
  driver_.begin();
  syncStateFromDriver(true);
}

void Board::tick() {
  driver_.readSensors();
  syncStateFromDriver(false);
}

void Board::readSensors() {
  tick();
}

bool Board::occupied(int row, int col) const {
  return state_.occupied(row, col);
}

bool Board::wasOccupied(int row, int col) const {
  return state_.wasOccupied(row, col);
}

bool Board::wasLifted(int row, int col) const {
  return state_.wasLifted(row, col);
}

bool Board::wasPlaced(int row, int col) const {
  return state_.wasPlaced(row, col);
}

bool Board::changed(int row, int col) const {
  return state_.changed(row, col);
}

uint8_t Board::changedCount() const {
  return state_.changedCount();
}

LibreChess::board::BoardSquare Board::changedSquare(uint8_t index) const {
  return state_.changedSquare(index);
}

void Board::syncOccupancyBaseline() {
  syncStateFromDriver(true);
}

void Board::acquireLEDs() {
  driver_.acquireLEDs();
}

void Board::releaseLEDs() {
  driver_.releaseLEDs();
}

void Board::clearAllLEDs(bool show) {
  driver_.clearAllLEDs(show);
}

void Board::setSquareLED(int row, int col, LedRGB color) {
  driver_.setSquareLED(row, col, color);
}

void Board::showLEDs() {
  driver_.showLEDs();
}

void Board::fireworkAnimation(LedRGB color) {
  driver_.fireworkAnimation(color);
}

void Board::captureAnimation(int row, int col) {
  driver_.captureAnimation(row, col);
}

void Board::promotionAnimation(int col) {
  driver_.promotionAnimation(col);
}

void Board::blinkSquare(int row, int col, LedRGB color, int times, bool clearAfter, bool clearBefore) {
  driver_.blinkSquare(row, col, color, times, clearAfter, clearBefore);
}

void Board::showConnectingAnimation() {
  driver_.showConnectingAnimation();
}

void Board::flashBoardAnimation(LedRGB color, int times) {
  driver_.flashBoardAnimation(color, times);
}

std::atomic<bool>* Board::startThinkingAnimation() {
  return driver_.startThinkingAnimation();
}

std::atomic<bool>* Board::startWaitingAnimation() {
  return driver_.startWaitingAnimation();
}

void Board::stopAndWaitForAnimation(std::atomic<bool>*& stopFlag) {
  driver_.stopAndWaitForAnimation(stopFlag);
}

void Board::waitForAnimationQueueDrain() {
  driver_.waitForAnimationQueueDrain();
}

uint8_t Board::getBrightness() const {
  return driver_.getBrightness();
}

uint8_t Board::getDimMultiplier() const {
  return driver_.getDimMultiplier();
}

void Board::setBrightness(uint8_t value) {
  driver_.setBrightness(value);
}

void Board::setDimMultiplier(uint8_t value) {
  driver_.setDimMultiplier(value);
}

void Board::saveLedSettings() {
  driver_.saveLedSettings();
}

void Board::triggerCalibration() {
  driver_.triggerCalibration();
}

void Board::syncStateFromDriver(bool initializeBaseline) {
  bool occupancy[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS];
  for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row) {
    for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
      occupancy[row][col] = driver_.getSensorState(row, col);
    }
  }

  if (initializeBaseline)
    state_.sync(occupancy);
  else
    state_.update(occupancy);
}
