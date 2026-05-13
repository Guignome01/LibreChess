#ifndef BOARD_MENUS_VIEW_H
#define BOARD_MENUS_VIEW_H

#include "board/core/canvas.h"
#include "board/core/colors.h"

#include <stdint.h>

class BoardRuntime;

constexpr int MENU_RESULT_NONE = -1;
constexpr int MENU_RESULT_BACK = -2;

// ---------------------------------------------------------------------------
// MenuView — drawable menu primitive on the 8x8 LED board
// ---------------------------------------------------------------------------
// Reusable menu primitive built around BoardRuntime / BoardCanvas. Items
// are placed freely on the grid. Selection uses a two-phase debounce
// (empty → occupied) for reliable piece-placement detection. Supports
// orientation flipping so menus face the active player.
//
// MenuView owns a canvas surface under the canvas guard. It does
// NOT inherit from anything — it is just a small object owned by a
// workflow and driven by its owning state machine.
// ---------------------------------------------------------------------------

/// A single selectable option on the board.
/// Coordinates are authored in white-side orientation
/// (row 7 = rank 1 = white's back rank).
struct MenuItem {
  int8_t row;
  int8_t col;
  LedRGB color;
  int8_t id;  ///< Unique identifier returned on selection (>= 0).
};

class MenuView {
 public:
  static constexpr int RESULT_NONE = MENU_RESULT_NONE;
  static constexpr int RESULT_BACK = MENU_RESULT_BACK;
  static constexpr uint8_t MAX_ITEMS = 16;

  explicit MenuView(BoardRuntime& runtime);
  ~MenuView();

  MenuView(const MenuView&) = delete;
  MenuView& operator=(const MenuView&) = delete;

  // -------------------------------------------------------------------------
  // Configuration
  // -------------------------------------------------------------------------

  /// Configure menu options. Items pointer must outlive the menu.
  void setItems(const MenuItem* items, uint8_t count);

  template <uint8_t N>
  void setItems(const MenuItem (&items)[N]) { setItems(items, N); }

  /// Designate a corner/edge square as a back button (lit white).
  void setBackButton(int8_t row, int8_t col);

  /// Clear back button.
  void clearBackButton() { hasBack_ = false; }

  /// Set orientation. When true, coordinates are vertically mirrored
  /// (row' = 7 - row) so the menu faces a player on the black side.
  void setFlipped(bool flipped);

  // -------------------------------------------------------------------------
  // Drawing
  // -------------------------------------------------------------------------

  /// Paint items + back button onto this view's surface. Idempotent.
  void draw();

  /// Clear this view's surface.
  void erase();

  /// Reset all debounce counters for a fresh selection cycle.
  void reset();

  // -------------------------------------------------------------------------
  // Polling
  // -------------------------------------------------------------------------

  /// Non-blocking poll. Returns:
  ///   - MenuItem::id on confirmed selection (also fires a blink).
  ///   - RESULT_BACK if back button selected.
  ///   - RESULT_NONE if no selection yet.
  int poll();

 private:
  static constexpr uint8_t DEFAULT_DEBOUNCE_CYCLES = 5;

  /// Debounces one selection square through empty-then-occupied phases.
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
  BoardCanvasHandle surface_;
  const MenuItem* items_;
  uint8_t itemCount_;
  bool flipped_;
  bool hasBack_;
  int8_t backRow_;
  int8_t backCol_;

  // +1 slot for the back button.
  SelectionDebouncer states_[MAX_ITEMS + 1];

  Square transformSquare(int8_t row, int8_t col) const;
  BoardCanvasHandle writableSurface(BoardCanvas& canvas);
  int trySelect(SelectionDebouncer& state, const bool (&occupied)[8][8], int8_t row,
                int8_t col, LedRGB color, int id);
};

/// Run the standard blocking green/red confirmation prompt on the board.
bool confirmBoardPrompt(BoardRuntime& runtime, bool flipped);

#endif  // BOARD_MENUS_VIEW_H
