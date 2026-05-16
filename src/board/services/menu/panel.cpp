#include "board/services/menu/panel.h"

#include "board/runtime/helpers.h"
#include "board/runtime/runtime.h"
#include "board/services/visual/animations.h"

#include <Arduino.h>

// ---------------------------------------------------------------------------
// MenuPanel::SelectionDebouncer
// ---------------------------------------------------------------------------

MenuPanel::SelectionDebouncer::SelectionDebouncer(uint8_t stableCycles)
    : stableCycles_(stableCycles == 0 ? 1 : stableCycles),
      emptyCount_(0),
      occupiedCount_(0),
      readyForSelection_(false),
      selectionLatched_(false) {}

void MenuPanel::SelectionDebouncer::reset() {
  emptyCount_ = 0;
  occupiedCount_ = 0;
  readyForSelection_ = false;
  selectionLatched_ = false;
}

bool MenuPanel::SelectionDebouncer::update(bool occupied) {
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

// ---------------------------------------------------------------------------
// MenuPanel
// ---------------------------------------------------------------------------

MenuPanel::MenuPanel(BoardRuntime& runtime, BoardAnimations& animations)
    : runtime_(runtime),
      animations_(animations),
      surface_(),
      options_(nullptr),
      optionCount_(0),
      flipped_(false) {}

MenuPanel::~MenuPanel() {
  auto g = runtime_.lockCanvas();
  BoardSurface::release(g.canvas, surface_);
}

void MenuPanel::setFlipped(bool flipped) {
  flipped_ = flipped;
}

void MenuPanel::show(const MenuOption* options, uint8_t count) {
  options_ = options;
  optionCount_ = (count > MENU_PANEL_OPTION_COUNT) ? MENU_PANEL_OPTION_COUNT : count;

  auto g = runtime_.lockCanvas();
  BoardCanvasHandle surface = BoardSurface::writable(g.canvas, surface_);
  g.canvas.clearSurface(surface);
  for (uint8_t i = 0; i < optionCount_; ++i) {
    Square sq = transformSquare(options_[i].row, options_[i].col);
    g.canvas.setPixel(surface, sq.row, sq.col, options_[i].color);
  }
}

void MenuPanel::erase() {
  auto g = runtime_.lockCanvas();
  BoardSurface::clear(g.canvas, surface_);
}

void MenuPanel::reset() {
  for (auto& state : states_) state.reset();
}

MenuPanel::Square MenuPanel::transformSquare(int8_t row, int8_t col) const {
  if (flipped_) return Square{static_cast<int8_t>(BoardHelpers::LAST_ROW - row), col};
  return Square{row, col};
}

int MenuPanel::trySelect(SelectionDebouncer& state,
                         const bool (&occupied)[BoardHelpers::ROWS][BoardHelpers::COLS],
                         const MenuOption& option) {
  Square sq = transformSquare(option.row, option.col);
  const bool squareOccupied = BoardHelpers::inBounds(sq.row, sq.col) && occupied[sq.row][sq.col];
  if (state.update(squareOccupied)) {
    auto g = runtime_.lockCanvas();
    animations_.startBlink(sq.row, sq.col, option.color, 1, millis());
    return option.id;
  }
  return MENU_RESULT_NONE;
}

int MenuPanel::poll() {
  if (options_ == nullptr) return MENU_RESULT_NONE;

  bool occupied[BoardInput::ROWS][BoardInput::COLS];
  runtime_.copyInputOccupancy(occupied);
  for (uint8_t i = 0; i < optionCount_; ++i) {
    int result = trySelect(states_[i], occupied, options_[i]);
    if (result != MENU_RESULT_NONE) return result;
  }
  return MENU_RESULT_NONE;
}