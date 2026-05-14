#include "board/menus/selection.h"

static constexpr LedRGB BACK_BUTTON_COLOR = LedColors::White;

MenuSelection::MenuSelection(BoardRuntime& runtime, BoardAnimations& animations)
    : panel_(runtime, animations),
      optionCount_(0),
      hasBack_(false),
      backOption_{0, 0, BACK_BUTTON_COLOR, MENU_RESULT_BACK} {}

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
  panel_.setFlipped(flipped);
}

uint8_t MenuSelection::effectiveOptionCount() {
  if (!hasBack_) return optionCount_;
  options_[optionCount_] = backOption_;
  return optionCount_ + 1;
}

void MenuSelection::draw() {
  panel_.show(options_, effectiveOptionCount());
}

void MenuSelection::erase() {
  panel_.erase();
}

void MenuSelection::reset() {
  panel_.reset();
}

int MenuSelection::poll() {
  return panel_.poll();
}
