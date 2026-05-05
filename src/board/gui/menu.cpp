#include "menu.h"

#include "board/core/layering.h"

#include <Arduino.h>

// Back button LED color
static constexpr LedRGB BACK_BUTTON_COLOR = LedColors::White;

// ---------------------------
// BoardMenu
// ---------------------------

BoardMenu::BoardMenu(BoardSystem* system, BoardLayering* layering)
    : system_(system),
      layering_(layering),
      items_(nullptr),
      itemCount_(0),
      flipped_(false),
      hasBack_(false),
      backRow_(0),
      backCol_(0) {
}

BoardMenu::SelectionDebouncer::SelectionDebouncer(uint8_t stableCycles)
    : stableCycles_(stableCycles == 0 ? 1 : stableCycles),
      emptyCount_(0),
      occupiedCount_(0),
      readyForSelection_(false),
      selectionLatched_(false) {}

void BoardMenu::SelectionDebouncer::reset() {
  emptyCount_ = 0;
  occupiedCount_ = 0;
  readyForSelection_ = false;
  selectionLatched_ = false;
}

bool BoardMenu::SelectionDebouncer::update(bool occupied) {
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

void BoardMenu::setItems(const MenuItem* items, uint8_t count) {
  items_ = items;
  itemCount_ = (count > MAX_ITEMS) ? MAX_ITEMS : count;
}

void BoardMenu::setBackButton(int8_t row, int8_t col) {
  hasBack_ = true;
  backRow_ = row;
  backCol_ = col;
}

void BoardMenu::setFlipped(bool flipped) {
  flipped_ = flipped;
}

void BoardMenu::show() {
  auto drawMenu = [&](auto& leds) {
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
  };

  if (layering_)
    layering_->replaceBase(drawMenu);
  else
    system_->batchLEDs(drawMenu);
}

void BoardMenu::hide() {
  if (layering_) {
    layering_->clearBase();
    return;
  }
  system_->batchLEDs([&](BoardSystem::LEDWriter& leds) {
    leds.clearAllLEDs(false);
    leds.showLEDs();
  });
}

void BoardMenu::reset() {
  for (auto& state : states_)
    state.reset();
}

LibreChess::board::BoardSquare BoardMenu::transformSquare(int8_t row, int8_t col) const {
  if (flipped_)
    return LibreChess::board::BoardSquare{static_cast<int8_t>(LibreChess::board::BOARD_ROWS - 1 - row), col};
  return LibreChess::board::BoardSquare{row, col};
}

int BoardMenu::trySelect(SelectionDebouncer& state, int8_t row, int8_t col, LedRGB color, int id) {
  auto square = transformSquare(row, col);
  bool squareOccupied = system_->occupied(square.row, square.col);
  if (state.update(squareOccupied)) {
    system_->runAnimation(AnimationJob::blink(square.row, square.col, color, 1));
    system_->waitForAnimationQueueDrain();
    // Wait for piece removal so the next menu starts with a clean square
    while (system_->occupied(square.row, square.col)) {
      system_->readSensors();
      delay(SENSOR_READ_DELAY_MS);
    }
    return id;
  }
  return RESULT_NONE;
}

int BoardMenu::poll() {
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

int BoardMenu::waitForSelection() {
  reset();
  show();
  while (true) {
    system_->readSensors();
    int result = poll();
    if (result != RESULT_NONE) {
      hide();
      return result;
    }
    delay(SENSOR_READ_DELAY_MS);
  }
}

// ---------------------------
// boardConfirm
// ---------------------------

bool boardConfirm(BoardSystem* system, BoardLayering* layering, bool flipped) {
  static constexpr MenuItem confirmItems[] = {
      {4, 3, LedColors::Green, 1}, // Yes — d4
      {4, 4, LedColors::Red, 0},   // No  — e4
  };

  BoardMenu menu(system, layering);
  menu.setItems(confirmItems, 2);
  menu.setFlipped(flipped);
  return menu.waitForSelection() == 1;
}
