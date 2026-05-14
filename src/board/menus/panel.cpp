#include "board/menus/panel.h"

#include "board/core/runtime.h"

#include <Arduino.h>

namespace {

bool inBounds(int8_t row, int8_t col) {
  return row >= 0 && row < 8 && col >= 0 && col < 8;
}

}  // namespace

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

MenuPanel::MenuPanel(BoardRuntime& runtime)
    : runtime_(runtime), surface_(), options_(nullptr), optionCount_(0), flipped_(false) {}

MenuPanel::~MenuPanel() {
  auto g = runtime_.lockCanvas();
  g.canvas.releaseSurface(surface_);
}

void MenuPanel::setFlipped(bool flipped) {
  flipped_ = flipped;
}

BoardCanvasHandle MenuPanel::writableSurface(BoardCanvas& canvas) {
  if (!canvas.active(surface_)) {
    surface_ = canvas.acquireSurface();
  }
  canvas.bringToFront(surface_);
  return surface_;
}

void MenuPanel::show(const MenuOption* options, uint8_t count) {
  options_ = options;
  optionCount_ = (count > MENU_PANEL_OPTION_COUNT) ? MENU_PANEL_OPTION_COUNT : count;

  auto g = runtime_.lockCanvas();
  BoardCanvasHandle surface = writableSurface(g.canvas);
  g.canvas.clearSurface(surface);
  for (uint8_t i = 0; i < optionCount_; ++i) {
    Square sq = transformSquare(options_[i].row, options_[i].col);
    g.canvas.setPixel(surface, sq.row, sq.col, options_[i].color);
  }
}

void MenuPanel::erase() {
  auto g = runtime_.lockCanvas();
  if (g.canvas.active(surface_)) g.canvas.clearSurface(surface_);
}

void MenuPanel::reset() {
  for (auto& state : states_) state.reset();
}

MenuPanel::Square MenuPanel::transformSquare(int8_t row, int8_t col) const {
  if (flipped_) return Square{static_cast<int8_t>(7 - row), col};
  return Square{row, col};
}

int MenuPanel::trySelect(SelectionDebouncer& state, const bool (&occupied)[8][8],
                         const MenuOption& option) {
  Square sq = transformSquare(option.row, option.col);
  const bool squareOccupied = inBounds(sq.row, sq.col) && occupied[sq.row][sq.col];
  if (state.update(squareOccupied)) {
    auto g = runtime_.lockCanvas();
    g.animations.startBlink(sq.row, sq.col, option.color, 1, millis());
    return option.id;
  }
  return MENU_RESULT_NONE;
}

int MenuPanel::poll() {
  if (options_ == nullptr) return MENU_RESULT_NONE;

  bool occupied[8][8];
  runtime_.copyInputOccupancy(occupied);
  for (uint8_t i = 0; i < optionCount_; ++i) {
    int result = trySelect(states_[i], occupied, options_[i]);
    if (result != MENU_RESULT_NONE) return result;
  }
  return MENU_RESULT_NONE;
}
