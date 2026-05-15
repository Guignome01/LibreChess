#ifndef BOARD_CORE_MENU_MENU_H
#define BOARD_CORE_MENU_MENU_H

#include "board/core/colors.h"
#include "board/core/menu/selection.h"
#include "board/core/menu/types.h"

#include <stdint.h>

class BoardRuntime;
class BoardAnimations;

// ---------------------------------------------------------------------------
// BoardMenuController — menu-facing drawing and completion API.
// ---------------------------------------------------------------------------
// Typed menus receive this interface in lifecycle/selection hooks. The board
// runner implements it so menu objects never poll sensors or touch runtime
// internals directly.
// ---------------------------------------------------------------------------

class BoardMenuController {
 public:
  virtual ~BoardMenuController() = default;

  virtual void show(const MenuOption* options, uint8_t count) = 0;

  template <uint8_t N>
  void show(const MenuOption (&options)[N]) {
    show(options, N);
  }

  virtual void showWithBack(const MenuOption* options, uint8_t count, int8_t backRow,
                            int8_t backCol) = 0;

  template <uint8_t N>
  void showWithBack(const MenuOption (&options)[N], int8_t backRow, int8_t backCol) {
    showWithBack(options, N, backRow, backCol);
  }

  virtual void erase() = 0;
  virtual void finish() = 0;
  virtual void blink(int8_t row, int8_t col, LedRGB color, int times) = 0;
  virtual void wait(uint32_t durationMs) = 0;
};

// ---------------------------------------------------------------------------
// BoardMenu — typed physical-board menu contract.
// ---------------------------------------------------------------------------
// Menu implementations define pages, transitions, and semantic results. The
// board owns polling, debouncing, rendering, and blocking cadence loops.
// ---------------------------------------------------------------------------

class BoardMenu {
 public:
  virtual ~BoardMenu() = default;

  virtual void begin(BoardMenuController& controller) = 0;
  virtual void onSelect(int optionId, BoardMenuController& controller) = 0;
  virtual void onBack(BoardMenuController& controller) { (void)controller; }
  virtual void cancel(BoardMenuController& controller) { controller.erase(); }
};

// ---------------------------------------------------------------------------
// BoardMenuRunner — board-owned menu polling/rendering service.
// ---------------------------------------------------------------------------

class BoardMenuRunner : private BoardMenuController {
 public:
  BoardMenuRunner(BoardRuntime& runtime, BoardAnimations& animations);

  BoardMenuRunner(const BoardMenuRunner&) = delete;
  BoardMenuRunner& operator=(const BoardMenuRunner&) = delete;

  /// Display a typed menu and make it the active menu.
  void show(BoardMenu& menu, bool flipped = false);

  /// Poll the active menu once. Returns true only when a menu finishes.
  bool poll();

  /// Run a typed menu until it finishes, polling at board sensor cadence.
  bool runBlocking(BoardMenu& menu, bool flipped = false);

  /// Return true when a menu is currently active.
  bool active() const { return activeMenu_ != nullptr; }

  /// Cancel and erase the active menu, if any.
  void clear();

 private:
  void show(const MenuOption* options, uint8_t count) override;
  void showWithBack(const MenuOption* options, uint8_t count, int8_t backRow,
                    int8_t backCol) override;
  void erase() override;
  void finish() override;
  void blink(int8_t row, int8_t col, LedRGB color, int times) override;
  void wait(uint32_t durationMs) override;
  void finishActiveMenu();

  BoardRuntime& runtime_;
  BoardAnimations& animations_;
  MenuSelection selection_;
  BoardMenu* activeMenu_;
  bool finished_;
};

#endif  // BOARD_CORE_MENU_MENU_H