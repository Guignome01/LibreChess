#ifndef BOARD_MENU_VIEW_H
#define BOARD_MENU_VIEW_H

#include "board/board.h"
#include "board/core/colors.h"
#include "board/core/system.h"
#include "drawable.h"
#include "layering.h"

#include <stdint.h>

// ---------------------------
// Board Menu View (drawable primitive)
// ---------------------------
// Reusable menu primitive for the 8x8 LED board, drawn through BoardLayering.
// Items are placed freely on the grid. Selection uses two-phase debounce
// (empty -> occupied) for reliable piece-placement detection. Supports
// orientation flipping so menus face the active player.

/// A single selectable option on the board.
/// Coordinates are authored in white-side orientation (row 7 = rank 1 = white's back rank).
struct MenuItem {
  int8_t row;
  int8_t col;
  LedRGB color;
  int8_t id;  // Unique identifier returned on selection
};

/// Reusable board menu drawable with two-phase debounce selection.
/// All state is stack-allocated -- no heap usage.
class MenuView : public BoardDrawable {
 public:
  static constexpr int RESULT_NONE = BoardDrawable::RESULT_NONE;
  static constexpr int RESULT_BACK = BoardDrawable::RESULT_BACK;
  static constexpr int MAX_ITEMS = 16;

  MenuView(BoardSystem& system, BoardLayering& layering);

  MenuView(const MenuView&) = delete;
  MenuView& operator=(const MenuView&) = delete;

  /// Configure menu options. Items pointer must outlive the menu.
  void setItems(const MenuItem* items, uint8_t count);

  template <uint8_t N>
  void setItems(const MenuItem (&items)[N]) { setItems(items, N); }

  /// Designate a corner/edge square as a back button (lit with LedColors::White).
  void setBackButton(int8_t row, int8_t col);

  /// Set orientation. When true, coordinates are vertically mirrored
  /// (row' = 7 - row) so the menu faces a player on the black side.
  void setFlipped(bool flipped);

  /// Light all menu items and back button on the persistent base layer.
  void show() override;

  /// Clear the menu from the persistent base layer.
  void hide() override;

  /// Reset all debounce counters for a fresh selection cycle.
  void reset() override;

  /// Non-blocking poll. Call after system.readSensors().
  /// Returns:
  ///   - MenuItem::id on confirmed selection (with blink feedback)
  ///   - RESULT_BACK if back button selected
  ///   - RESULT_NONE if no selection yet
  int poll() override;

  /// Blocking convenience: reset() -> show() -> poll loop -> return id.
  int waitForSelection();

 private:
  static constexpr uint8_t DEFAULT_DEBOUNCE_CYCLES = 5;

  /// Debounces one menu-selection square through empty-then-occupied phases.
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

  BoardSystem& system_;
  BoardLayering& layering_;
  const MenuItem* items_;
  uint8_t itemCount_;
  bool flipped_;
  bool hasBack_;
  int8_t backRow_;
  int8_t backCol_;

  // +1 slot for the back button
  SelectionDebouncer states_[MAX_ITEMS + 1];

  LibreChess::board::BoardSquare transformSquare(int8_t row, int8_t col) const;
  int trySelect(SelectionDebouncer& state, int8_t row, int8_t col, LedRGB color, int id);
};

#endif  // BOARD_MENU_VIEW_H
