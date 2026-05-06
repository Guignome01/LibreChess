#include "board.h"

#include "core/system.h"
#include "gui/gui.h"
#include "services.h"

#include <Arduino.h>

struct Board::Impl {
  BoardSystem system;
  BoardGui gui;

  Impl() : system(), gui(system) {}
};

Board::Board() : impl_(std::make_unique<Impl>()) {}

Board::~Board() = default;

void Board::begin() {
  if (!impl_->system.begin()) {
    Serial.println("Board scheduler initialization failed");
  }
}

uint8_t Board::getBrightness() const {
  return impl_->system.getBrightness();
}

uint8_t Board::getDimMultiplier() const {
  return impl_->system.getDimMultiplier();
}

void Board::setBrightness(uint8_t value) {
  impl_->system.setBrightness(value);
}

void Board::setDimMultiplier(uint8_t value) {
  impl_->system.setDimMultiplier(value);
}

void Board::saveLedSettings() {
  impl_->system.saveLedSettings();
}

uint16_t Board::sensorReadDelayMs() const {
  return impl_->system.sensorReadDelayMs();
}

BoardServices& Board::services() {
  return impl_->gui.services();
}
