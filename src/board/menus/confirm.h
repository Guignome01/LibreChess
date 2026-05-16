#ifndef BOARD_MENUS_CONFIRM_H
#define BOARD_MENUS_CONFIRM_H

#include "board/services/menu/menu.h"
#include "board/menus/game_selection.h"

namespace ConfirmMenuOptionId {
constexpr int8_t CONFIRM_NO = 30;
constexpr int8_t CONFIRM_YES = 31;
}  // namespace ConfirmMenuOptionId

// ---------------------------------------------------------------------------
// ConfirmMenu — predefined green/red confirmation prompt.
// ---------------------------------------------------------------------------

class ConfirmMenu : public BoardMenu {
 public:
  ConfirmMenu();

  void begin(BoardMenuController& controller) override;
  void onSelect(int optionId, BoardMenuController& controller) override;
  void cancel(BoardMenuController& controller) override;

  bool answered() const { return answered_; }
  bool accepted() const { return answered_ && accepted_; }

 protected:
  void reset();

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

  void begin(BoardMenuController& controller) override;

 private:
  BoardGameSelectionMode mode_;
};

#endif  // BOARD_MENUS_CONFIRM_H