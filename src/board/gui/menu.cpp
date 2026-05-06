#include "menu.h"

#include "animations.h"
#include "layering.h"

#include <Arduino.h>

// Back button LED color
static constexpr LedRGB BACK_BUTTON_COLOR = LedColors::White;

// ---------------------------
// MenuView
// ---------------------------

MenuView::MenuView(BoardSystem& system, BoardLayering& layering)
    : system_(system),
      layering_(layering),
      items_(nullptr),
      itemCount_(0),
      flipped_(false),
      hasBack_(false),
      backRow_(0),
      backCol_(0) {}

MenuView::SelectionDebouncer::SelectionDebouncer(uint8_t stableCycles)
    : stableCycles_(stableCycles == 0 ? 1 : stableCycles),
      emptyCount_(0),
      occupiedCount_(0),
      readyForSelection_(false),
      selectionLatched_(false) {}

void MenuView::SelectionDebouncer::reset() {
  emptyCount_ = 0;
  occupiedCount_ = 0;
  readyForSelection_ = false;
  selectionLatched_ = false;
}

bool MenuView::SelectionDebouncer::update(bool occupied) {
  if (!occupied) {
    if (emptyCount_ < stableCycles_) ++emptyCount_;
    occupiedCount_ = 0;
    selectionLatched_ = false;
    if (emptyCount_ >= stableCycles_) readyForSelection_ = true;
    return false;
  }

  emptyCount_ = 0;
  if (!readyForSelection_ || selectionLatched_) {
    occupiedCount_ = 0;
    return false;
  }

  if (occupiedCount_ < stableCycles_) ++occupiedCount_;
  if (occupiedCount_ >= stableCycles_) {
    readyForSelection_ = false;
    selectionLatched_ = true;
    return true;
  }
  return false;
}

void MenuView::setItems(const MenuItem* items, uint8_t count) {
  items_ = items;
  itemCount_ = (count > MAX_ITEMS) ? MAX_ITEMS : count;
}

void MenuView::setBackButton(int8_t row, int8_t col) {
  hasBack_ = true;
  backRow_ = row;
  backCol_ = col;
}

void MenuView::setFlipped(bool flipped) {
  flipped_ = flipped;
}

void MenuView::show() {
  layering_.replaceBase([&](BoardLayering::LayerWriter& leds) {
    leds.clearAllLEDs(false);
    for (uint8_t i = 0; i < itemCount_; ++i) {
      auto square = transformSquare(items_[i].row, items_[i].col);
      leds.setSquareLED(square.row, square.col, items_[i].color);
    }
    if (hasBack_) {
      auto square = transformSquare(backRow_, backCol_);
      leds.setSquareLED(square.row, square.col, BACK_BUTTON_COLOR);
    }
    leds.showLEDs();
  });
}

void MenuView::hide() {
  layering_.clearBase();
}

void MenuView::reset() {
  for (auto& state : states_)
    state.reset();
}

LibreChess::board::BoardSquare MenuView::transformSquare(int8_t row, int8_t col) const {
  if (flipped_)
    return LibreChess::board::BoardSquare{static_cast<int8_t>(LibreChess::board::BOARD_ROWS - 1 - row), col};
  return LibreChess::board::BoardSquare{row, col};
}

int MenuView::trySelect(SelectionDebouncer& state, int8_t row, int8_t col, LedRGB color, int id) {
  auto square = transformSquare(row, col);
  bool squareOccupied = system_.occupied(square.row, square.col);
  if (state.update(squareOccupied)) {
    system_.runAnimation(AnimationJob::blink(square.row, square.col, color, 1));
    system_.waitForAnimationQueueDrain();
    // Wait for piece removal so the next menu starts with a clean square
    while (system_.occupied(square.row, square.col)) {
      system_.readSensors();
      delay(SENSOR_READ_DELAY_MS);
    }
    return id;
  }
  return RESULT_NONE;
}

int MenuView::poll() {
  for (uint8_t i = 0; i < itemCount_; ++i) {
    int result = trySelect(states_[i], items_[i].row, items_[i].col, items_[i].color, items_[i].id);
    if (result != RESULT_NONE)
      return result;
  }
  if (hasBack_) {
    int result = trySelect(states_[itemCount_], backRow_, backCol_, BACK_BUTTON_COLOR, RESULT_BACK);
    if (result != RESULT_NONE)
      return result;
  }
  return RESULT_NONE;
}

int MenuView::waitForSelection() {
  reset();
  show();
  while (true) {
    system_.readSensors();
    int result = poll();
    if (result != RESULT_NONE) {
      hide();
      return result;
    }
    delay(SENSOR_READ_DELAY_MS);
  }
}
