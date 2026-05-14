#ifndef BOARD_MENUS_PANEL_H
#define BOARD_MENUS_PANEL_H

#include "board/core/canvas.h"
#include "board/menus/options.h"

#include <stdint.h>

class BoardRuntime;
class BoardAnimations;

// ---------------------------------------------------------------------------
// MenuPanel — shared physical-board menu mechanics
// ---------------------------------------------------------------------------
// Owns the canvas surface, orientation transform, occupancy polling, and
// piece-placement debounce used by menu selection screens and prompts. It does
// not interpret option ids or know which workflow requested the menu.
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
  BoardCanvasHandle writableSurface(BoardCanvas& canvas);
  int trySelect(SelectionDebouncer& state, const bool (&occupied)[8][8], const MenuOption& option);
};

#endif  // BOARD_MENUS_PANEL_H
