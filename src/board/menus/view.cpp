#include "board/menus/view.h"

#include "board/core/runtime.h"

#include <Arduino.h>

static constexpr LedRGB BACK_BUTTON_COLOR = LedColors::White;
static constexpr MenuItem CONFIRM_ITEMS[] = {
    {4, 3, LedColors::Green, 1},
    {4, 4, LedColors::Red, 0},
};

bool inBounds(int8_t row, int8_t col) {
  return row >= 0 && row < 8 && col >= 0 && col < 8;
}

// ---------------------------------------------------------------------------
// MenuView::SelectionDebouncer
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// MenuView
// ---------------------------------------------------------------------------

MenuView::MenuView(BoardRuntime& runtime)
    : runtime_(runtime),
      surface_(),
      items_(nullptr),
      itemCount_(0),
      flipped_(false),
      hasBack_(false),
      backRow_(0),
      backCol_(0) {}

MenuView::~MenuView() {
  auto g = runtime_.lockCanvas();
  g.canvas.releaseSurface(surface_);
}

BoardCanvasHandle MenuView::writableSurface(BoardCanvas& canvas) {
  if (!canvas.active(surface_)) {
    surface_ = canvas.acquireSurface();
  }
  canvas.bringToFront(surface_);
  return surface_;
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

void MenuView::draw() {
  auto g = runtime_.lockCanvas();
  BoardCanvasHandle surface = writableSurface(g.canvas);
  g.canvas.clearSurface(surface);
  for (uint8_t i = 0; i < itemCount_; ++i) {
    Square sq = transformSquare(items_[i].row, items_[i].col);
    g.canvas.setPixel(surface, sq.row, sq.col, items_[i].color);
  }
  if (hasBack_) {
    Square sq = transformSquare(backRow_, backCol_);
    g.canvas.setPixel(surface, sq.row, sq.col, BACK_BUTTON_COLOR);
  }
}

void MenuView::erase() {
  auto g = runtime_.lockCanvas();
  if (g.canvas.active(surface_)) g.canvas.clearSurface(surface_);
}

void MenuView::reset() {
  for (auto& state : states_) state.reset();
}

MenuView::Square MenuView::transformSquare(int8_t row, int8_t col) const {
  if (flipped_) return Square{static_cast<int8_t>(7 - row), col};
  return Square{row, col};
}

int MenuView::trySelect(SelectionDebouncer& state, const bool (&occupied)[8][8], int8_t row,
                        int8_t col, LedRGB color, int id) {
  Square sq = transformSquare(row, col);
  const bool squareOccupied = inBounds(sq.row, sq.col) && occupied[sq.row][sq.col];
  if (state.update(squareOccupied)) {
    auto g = runtime_.lockCanvas();
    g.animations.startBlink(sq.row, sq.col, color, 1, millis());
    return id;
  }
  return RESULT_NONE;
}

int MenuView::poll() {
  bool occupied[8][8];
  runtime_.copyInputOccupancy(occupied);
  for (uint8_t i = 0; i < itemCount_; ++i) {
    int result = trySelect(states_[i], occupied, items_[i].row, items_[i].col, items_[i].color,
                           items_[i].id);
    if (result != RESULT_NONE) return result;
  }
  if (hasBack_) {
    int result = trySelect(states_[itemCount_], occupied, backRow_, backCol_, BACK_BUTTON_COLOR,
                           RESULT_BACK);
    if (result != RESULT_NONE) return result;
  }
  return RESULT_NONE;
}

bool confirmBoardPrompt(BoardRuntime& runtime, bool flipped) {
  MenuView prompt(runtime);
  prompt.setItems(CONFIRM_ITEMS, 2);
  prompt.setFlipped(flipped);
  prompt.reset();
  prompt.draw();
  while (true) {
    int result = prompt.poll();
    if (result != MENU_RESULT_NONE) {
      prompt.erase();
      return result == 1;
    }
    delay(runtime.cadenceMs());
  }
}
