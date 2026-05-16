#ifndef BOARD_SERVICES_MENU_SELECTION_H
#define BOARD_SERVICES_MENU_SELECTION_H

#include "board/services/menu/panel.h"
#include "board/services/menu/types.h"

#include <stdint.h>

class BoardRuntime;
class BoardAnimations;

// ---------------------------------------------------------------------------
// MenuSelection — selectable physical menu page
// ---------------------------------------------------------------------------
// Thin page wrapper over MenuPanel. It stores one page of options and
// optionally appends the standard white back button.
// ---------------------------------------------------------------------------

class MenuSelection {
 public:
  MenuSelection(BoardRuntime& runtime, BoardAnimations& animations);

  MenuSelection(const MenuSelection&) = delete;
  MenuSelection& operator=(const MenuSelection&) = delete;

  /// Configure menu options. Options are copied into fixed internal storage.
  void setOptions(const MenuOption* options, uint8_t count);

  template <uint8_t N>
  void setOptions(const MenuOption (&options)[N]) {
    setOptions(options, N);
  }

  /// Designate a square as the standard white back button.
  void setBackButton(int8_t row, int8_t col);

  /// Clear back button.
  void clearBackButton();

  /// Set orientation for this menu page.
  void setFlipped(bool flipped);

  /// Paint the options and optional back button.
  void draw();

  /// Clear this page's surface.
  void erase();

  /// Reset all debounce counters for a fresh selection cycle.
  void reset();

  /// Non-blocking poll. Returns a selected option id, MENU_RESULT_BACK, or MENU_RESULT_NONE.
  int poll();

 private:
  MenuPanel panel_;
  MenuOption options_[MENU_PANEL_OPTION_COUNT];
  uint8_t optionCount_;
  bool hasBack_;
  MenuOption backOption_;

  uint8_t effectiveOptionCount();
};

#endif  // BOARD_SERVICES_MENU_SELECTION_H