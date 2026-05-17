#include "board/services/menu/selection.h"

#include "board/runtime/runtime.h"
#include "board/services/visual/animations.h"

#include <Arduino.h>

namespace {
constexpr LedRGB BACK_BUTTON_COLOR = LedColors::White;
}

// ---------------------------------------------------------------------------
// MenuSelection
// ---------------------------------------------------------------------------

MenuSelection::MenuSelection(BoardRuntime& runtime, BoardAnimations& animations)
    : runtime_(runtime),
      animations_(animations),
      surface_(),
      confirmation_(),
      optionCount_(0),
      hasBack_(false),
      backOption_{0, 0, BACK_BUTTON_COLOR, MENU_RESULT_BACK},
      flipped_(false) {}

MenuSelection::~MenuSelection() {
  auto g = runtime_.lockCanvas();
  animations_.cancel(confirmation_);
  BoardSurface::release(g.canvas, surface_);
}

void MenuSelection::setOptions(const MenuOption* options, uint8_t count) {
  optionCount_ = (count > MENU_SELECTION_OPTION_COUNT) ? MENU_SELECTION_OPTION_COUNT : count;
  for (uint8_t i = 0; i < optionCount_; ++i) {
    options_[i] = options[i];
  }
}

void MenuSelection::setBackButton(int8_t row, int8_t col) {
  hasBack_ = true;
  backOption_ = MenuOption{row, col, BACK_BUTTON_COLOR, MENU_RESULT_BACK};
}

void MenuSelection::clearBackButton() {
  hasBack_ = false;
}

void MenuSelection::setFlipped(bool flipped) {
  flipped_ = flipped;
}

uint8_t MenuSelection::effectiveOptionCount() {
  if (!hasBack_) return optionCount_;
  options_[optionCount_] = backOption_;
  return static_cast<uint8_t>(optionCount_ + 1);
}

void MenuSelection::draw() {
  const uint8_t count = effectiveOptionCount();
  auto g = runtime_.lockCanvas();
  BoardCanvasHandle surface = BoardSurface::writable(g.canvas, surface_);
  g.canvas.clearSurface(surface);
  for (uint8_t i = 0; i < count; ++i) {
    Square sq = transformSquare(options_[i].row, options_[i].col);
    g.canvas.setPixel(surface, sq.row, sq.col, options_[i].color);
  }
}

void MenuSelection::erase() {
  auto g = runtime_.lockCanvas();
  animations_.cancel(confirmation_);
  BoardSurface::clear(g.canvas, surface_);
}

void MenuSelection::reset() {
  for (auto& state : states_) state.reset();
  confirmation_ = BoardScheduledHandle{};
}

MenuSelection::Square MenuSelection::transformSquare(int8_t row, int8_t col) const {
  if (flipped_) return Square{static_cast<int8_t>(BoardHelpers::LAST_ROW - row), col};
  return Square{row, col};
}

int MenuSelection::trySelect(SelectionDebouncer& state,
                             const bool (&occupied)[BoardHelpers::ROWS][BoardHelpers::COLS],
                             const MenuOption& option) {
  Square sq = transformSquare(option.row, option.col);
  const bool squareOccupied = BoardHelpers::inBounds(sq.row, sq.col) && occupied[sq.row][sq.col];
  const SelectionDebouncer::Result result = state.update(squareOccupied);
  if (result == SelectionDebouncer::Result::PRESSED) {
    auto g = runtime_.lockCanvas();
    g.canvas.clearSurfaceSquare(surface_, sq.row, sq.col);
    return MENU_RESULT_NONE;
  }
  if (result == SelectionDebouncer::Result::RELEASED) {
    auto g = runtime_.lockCanvas();
    confirmation_ = animations_.startBlink(sq.row, sq.col, option.color, 1, millis());
    return option.id;
  }
  return MENU_RESULT_NONE;
}

bool MenuSelection::confirmationActive() {
  if (!confirmation_.valid()) return false;
  auto g = runtime_.lockCanvas();
  if (animations_.active(confirmation_)) return true;
  confirmation_ = BoardScheduledHandle{};
  return false;
}

int MenuSelection::poll() {
  const uint8_t count = effectiveOptionCount();
  if (count == 0) return MENU_RESULT_NONE;

  bool occupied[BoardInput::ROWS][BoardInput::COLS];
  runtime_.copyInputOccupancy(occupied);
  for (uint8_t i = 0; i < count; ++i) {
    int result = trySelect(states_[i], occupied, options_[i]);
    if (result != MENU_RESULT_NONE) return result;
  }
  return MENU_RESULT_NONE;
}
