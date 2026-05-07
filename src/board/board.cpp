#include "board.h"

#include "calibration.h"
#include "core/controller.h"
#include "diagnostics.h"
#include "gameplay.h"
#include "menu.h"

#include <Arduino.h>

struct Board::Impl {
  BoardController controller;
  BoardGameplay gameplay;
  BoardDiagnostics diagnostics;
  BoardCalibration calibration;
  BoardMenu menu;

  Impl()
    : controller(),
      gameplay(controller),
      diagnostics(controller),
      calibration(controller),
      menu(controller) {}
};

Board::Board() : impl_(std::make_unique<Impl>()) {}

Board::~Board() = default;

void Board::begin() {
  if (!impl_->controller.begin()) {
    Serial.println("Board scheduler initialization failed");
  }
}

uint8_t Board::getBrightness() const {
  return impl_->controller.getBrightness();
}

uint8_t Board::getDimMultiplier() const {
  return impl_->controller.getDimMultiplier();
}

void Board::setBrightness(uint8_t value) {
  impl_->controller.setBrightness(value);
}

void Board::setDimMultiplier(uint8_t value) {
  impl_->controller.setDimMultiplier(value);
}

void Board::saveLedSettings() {
  impl_->controller.saveLedSettings();
}

uint16_t Board::sensorReadDelayMs() const {
  return impl_->controller.sensorReadDelayMs();
}

BoardGameplay& Board::gameplay() {
  return impl_->gameplay;
}

BoardMenu& Board::menu() {
  return impl_->menu;
}

BoardDiagnostics& Board::diagnostics() {
  return impl_->diagnostics;
}

BoardCalibration& Board::calibration() {
  return impl_->calibration;
}

void Board::clearAllLEDs(bool show) {
  impl_->controller.clearAllLEDs(show);
}

void Board::showConnectingAnimation() {
  impl_->controller.showConnectingAnimation();
}
