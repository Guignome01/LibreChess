#include "board/menus/confirm.h"

namespace {

static constexpr MenuTile CONFIRM_TILES[] = {
    {4, 3, LedColors::Green, ConfirmMenuOptionId::CONFIRM_YES, 0,
     MenuAdvance::CLOSE, 0},
    {4, 4, LedColors::Red, ConfirmMenuOptionId::CONFIRM_NO, 0,
     MenuAdvance::CLOSE, 0},
};

}  // namespace

ConfirmMenu::ConfirmMenu() : answered_(false), accepted_(false) {}

const MenuTile* ConfirmMenu::tiles() const { return CONFIRM_TILES; }
uint8_t ConfirmMenu::tileCount() const {
  return sizeof(CONFIRM_TILES) / sizeof(CONFIRM_TILES[0]);
}

void ConfirmMenu::onOpen(uint8_t pageId, MenuFlow& flow) {
  (void)pageId;
  (void)flow;
  resetAnswers();
}

void ConfirmMenu::onSelect(uint8_t tileId, MenuFlow& flow) {
  (void)flow;
  if (tileId == ConfirmMenuOptionId::CONFIRM_YES) {
    answered_ = true;
    accepted_ = true;
  } else if (tileId == ConfirmMenuOptionId::CONFIRM_NO) {
    answered_ = true;
    accepted_ = false;
  }
}

void ConfirmMenu::resetAnswers() {
  answered_ = false;
  accepted_ = false;
}

ResumeConfirmMenu::ResumeConfirmMenu(BoardGameSelectionMode mode) : mode_(mode) {}

void ResumeConfirmMenu::onOpen(uint8_t pageId, MenuFlow& flow) {
  // Pre-blink the mode indicator before showing the confirmation tiles so
  // the user can identify which game would resume.
  flow.blink(3, 3, mainMenuModeColor(mode_), 2);
  flow.wait(900);
  ConfirmMenu::onOpen(pageId, flow);
}
