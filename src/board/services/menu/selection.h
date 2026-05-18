#ifndef BOARD_SERVICES_MENU_SELECTION_H
#define BOARD_SERVICES_MENU_SELECTION_H

#include "board/runtime/canvas.h"
#include "board/runtime/helpers.h"
#include "board/services/menu/types.h"

#include <stdint.h>

class BoardRuntime;

// ---------------------------------------------------------------------------
// MenuSelection — shared physical-board menu primitive.
// ---------------------------------------------------------------------------
// Owns one canvas surface and the full physical menu interaction loop:
//   - Paints option tiles (plus an optional white back tile) through the
//     runtime canvas lock.
//   - Snapshots sensor occupancy once per poll and debounces deliberate
//     empty-then-occupied-then-empty selection gestures per square.
//   - Handles white/black orientation flipping for both painting and polling.
//   - Returns selected option ids (or `MENU_RESULT_BACK`) without
//     interpreting them — the runner owns transition semantics.
// Capacity: up to `MENU_SELECTION_OPTION_COUNT` selectable options plus one
// reserved slot for the back tile.
// ---------------------------------------------------------------------------

class MenuSelection {
 public:
  static constexpr uint8_t DEFAULT_DEBOUNCE_CYCLES = 5;

  explicit MenuSelection(BoardRuntime& runtime);
  ~MenuSelection();

  MenuSelection(const MenuSelection&) = delete;
  MenuSelection& operator=(const MenuSelection&) = delete;

  /// Replace the selectable option set. Options are copied into fixed
  /// internal storage; excess entries are silently truncated.
  void setOptions(const MenuOption* options, uint8_t count);

  /// Designate a square as the standard white back tile.
  void setBackButton(int8_t row, int8_t col);

  /// Remove the back tile.
  void clearBackButton();

  /// Set orientation. When true, coordinates are vertically mirrored on both
  /// rendering and occupancy lookup.
  void setFlipped(bool flipped);

  /// Paint the current options (plus the optional back tile) onto the
  /// owned canvas surface.
  void draw();

  /// Clear the owned canvas surface.
  void erase();

  /// Reset all debounce counters for a fresh selection cycle.
  void reset();

  /// Non-blocking poll. Returns a selected option id, `MENU_RESULT_BACK`,
  /// or `MENU_RESULT_NONE`.
  int poll();

  /// Debounces one option square through empty, press, and release phases.
  class SelectionDebouncer {
   public:
    enum class Result : uint8_t { NONE, PRESSED, RELEASED };

    explicit SelectionDebouncer(uint8_t stableCycles = DEFAULT_DEBOUNCE_CYCLES)
        : stableCycles_(stableCycles == 0 ? 1 : stableCycles),
          emptyCount_(0),
          occupiedCount_(0),
          readyForPress_(false),
          waitingForRelease_(false) {}

    void reset() {
      emptyCount_ = 0;
      occupiedCount_ = 0;
      readyForPress_ = false;
      waitingForRelease_ = false;
    }

    Result update(bool occupied) {
      if (!occupied) {
        if (emptyCount_ < stableCycles_) ++emptyCount_;
        occupiedCount_ = 0;
        if (waitingForRelease_ && emptyCount_ >= stableCycles_) {
          waitingForRelease_ = false;
          readyForPress_ = true;
          return Result::RELEASED;
        }
        if (emptyCount_ >= stableCycles_) readyForPress_ = true;
        return Result::NONE;
      }

      emptyCount_ = 0;
      if (waitingForRelease_) return Result::NONE;
      if (!readyForPress_) {
        occupiedCount_ = 0;
        return Result::NONE;
      }

      if (occupiedCount_ < stableCycles_) ++occupiedCount_;
      if (occupiedCount_ >= stableCycles_) {
        readyForPress_ = false;
        waitingForRelease_ = true;
        return Result::PRESSED;
      }
      return Result::NONE;
    }

   private:
    uint8_t stableCycles_;
    uint8_t emptyCount_;
    uint8_t occupiedCount_;
    bool readyForPress_;
    bool waitingForRelease_;
  };

 private:
  static constexpr uint8_t SLOT_COUNT = MENU_SELECTION_OPTION_COUNT + 1;

  struct Square {
    int8_t row;
    int8_t col;
  };

  Square transformSquare(int8_t row, int8_t col) const;
  uint8_t effectiveOptionCount();
  int trySelect(SelectionDebouncer& state,
                const bool (&occupied)[BoardHelpers::ROWS][BoardHelpers::COLS],
                const MenuOption& option);

  BoardRuntime& runtime_;
  BoardCanvasHandle surface_;
  MenuOption options_[SLOT_COUNT];
  uint8_t optionCount_;
  bool hasBack_;
  MenuOption backOption_;
  bool flipped_;
  SelectionDebouncer states_[SLOT_COUNT];
};

#endif  // BOARD_SERVICES_MENU_SELECTION_H
