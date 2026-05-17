#ifndef BOARD_MENUS_CONFIRM_H
#define BOARD_MENUS_CONFIRM_H

#include "board/menus/main.h"
#include "board/services/menu/menu.h"

namespace ConfirmMenuOptionId {
constexpr uint8_t CONFIRM_NO = 30;
constexpr uint8_t CONFIRM_YES = 31;
}  // namespace ConfirmMenuOptionId

// ---------------------------------------------------------------------------
// ConfirmMenu — predefined green/red confirmation prompt.
// ---------------------------------------------------------------------------
// Single-page menu with two tiles. Both tiles auto-close so a press
// immediately terminates the prompt; the caller then queries `answered()`
// and `accepted()`.
// ---------------------------------------------------------------------------

class ConfirmMenu : public BoardMenu {
 public:
  ConfirmMenu();

  const MenuTile* tiles() const override;
  uint8_t tileCount() const override;

  void onOpen(uint8_t pageId, MenuFlow& flow) override;
  void onSelect(uint8_t tileId, MenuFlow& flow) override;

  bool answered() const { return answered_; }
  bool accepted() const { return answered_ && accepted_; }

 protected:
  void resetAnswers();

 private:
  bool answered_;
  bool accepted_;
};

// ---------------------------------------------------------------------------
// ResumeConfirmMenu — confirmation prompt with a mode-coloured pre-blink.
// ---------------------------------------------------------------------------

class ResumeConfirmMenu final : public ConfirmMenu {
 public:
  explicit ResumeConfirmMenu(BoardGameSelectionMode mode);

  void onOpen(uint8_t pageId, MenuFlow& flow) override;

 private:
  BoardGameSelectionMode mode_;
};

#endif  // BOARD_MENUS_CONFIRM_H
