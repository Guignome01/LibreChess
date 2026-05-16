#ifndef BOARD_SERVICES_MENU_PANEL_H
#define BOARD_SERVICES_MENU_PANEL_H

#include "board/runtime/canvas.h"
#include "board/runtime/helpers.h"
#include "board/services/menu/types.h"

#include <stdint.h>

class BoardRuntime;
class BoardAnimations;

// ---------------------------------------------------------------------------
// MenuPanel — shared physical-board menu mechanics
// ---------------------------------------------------------------------------
// Owns the canvas surface, orientation transform, occupancy polling, and
// piece-placement debounce used by typed menus. It returns selected option ids
// without interpreting them.
// ---------------------------------------------------------------------------

class MenuPanel {
 public:
  MenuPanel(BoardRuntime& runtime, BoardAnimations& animations);
  ~MenuPanel();

  MenuPanel(const MenuPanel&) = delete;
  MenuPanel& operator=(const MenuPanel&) = delete;

  /// Set orientation. When true, coordinates are vertically mirrored.
  void setFlipped(bool flipped);

  /// Draw the supplied options onto this panel's owned surface.
  void show(const MenuOption* options, uint8_t count);

  template <uint8_t N>
  void show(const MenuOption (&options)[N]) {
    show(options, N);
  }

  /// Clear this panel's owned surface.
  void erase();

  /// Reset all debounce counters for a fresh selection cycle.
  void reset();

  /// Non-blocking poll. Returns a selected option id or MENU_RESULT_NONE.
  int poll();

 private:
  static constexpr uint8_t DEFAULT_DEBOUNCE_CYCLES = 5;

  /// Debounces one option square through empty-then-occupied phases.
  class SelectionDebouncer {
   public:
    explicit SelectionDebouncer(uint8_t stableCycles = DEFAULT_DEBOUNCE_CYCLES);
    void reset();
    bool update(bool occupied);

   private:
    uint8_t stableCycles_;
    uint8_t emptyCount_;
    uint8_t occupiedCount_;
    bool readyForSelection_;
    bool selectionLatched_;
  };

  struct Square {
    int8_t row;
    int8_t col;
  };

  BoardRuntime& runtime_;
  BoardAnimations& animations_;
  BoardCanvasHandle surface_;
  const MenuOption* options_;
  uint8_t optionCount_;
  bool flipped_;
  SelectionDebouncer states_[MENU_PANEL_OPTION_COUNT];

  Square transformSquare(int8_t row, int8_t col) const;
  int trySelect(SelectionDebouncer& state,
                const bool (&occupied)[BoardHelpers::ROWS][BoardHelpers::COLS],
                const MenuOption& option);
};

#endif  // BOARD_SERVICES_MENU_PANEL_H