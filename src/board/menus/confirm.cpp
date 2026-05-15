#include "board/menus/confirm.h"

namespace {

static constexpr MenuOption CONFIRM_OPTIONS[] = {
    {4, 3, LedColors::Green, ConfirmMenuOptionId::CONFIRM_YES},
    {4, 4, LedColors::Red, ConfirmMenuOptionId::CONFIRM_NO},
};

}  // namespace

ConfirmMenu::ConfirmMenu() : answered_(false), accepted_(false) {}

void ConfirmMenu::begin(BoardMenuController& controller) {
  reset();
  controller.show(CONFIRM_OPTIONS);
}

void ConfirmMenu::onSelect(int optionId, BoardMenuController& controller) {
  if (optionId != ConfirmMenuOptionId::CONFIRM_YES &&
      optionId != ConfirmMenuOptionId::CONFIRM_NO) {
    return;
  }
  answered_ = true;
  accepted_ = optionId == ConfirmMenuOptionId::CONFIRM_YES;
  controller.finish();
}

void ConfirmMenu::cancel(BoardMenuController& controller) {
  reset();
  controller.erase();
}

void ConfirmMenu::reset() {
  answered_ = false;
  accepted_ = false;
}

ResumeConfirmMenu::ResumeConfirmMenu(BoardGameSelectionMode mode) : mode_(mode) {}

void ResumeConfirmMenu::begin(BoardMenuController& controller) {
  controller.blink(3, 3, gameSelectionResumeIndicatorColor(mode_), 2);
  controller.wait(900);
  ConfirmMenu::begin(controller);
}