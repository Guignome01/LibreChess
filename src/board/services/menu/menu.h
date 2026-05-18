#ifndef BOARD_SERVICES_MENU_MENU_H
#define BOARD_SERVICES_MENU_MENU_H

#include "board/runtime/colors.h"
#include "board/services/menu/selection.h"
#include "board/services/menu/types.h"

#include <stdint.h>

class BoardRuntime;
class BoardAnimations;

// ---------------------------------------------------------------------------
// Menu redesign — page-stack runner with declarative tile arrays.
// ---------------------------------------------------------------------------
// Menus declare a flat array of `MenuTile` entries and per-page configuration
// (back tile placement). The runner owns the page stack, polling, drawing,
// and lifecycle dispatch. Menus only describe *what* their pages look like
// and *how* to react to user actions through small, focused hooks
// (`onOpen`, `onNext`, `onBack`, `onSelect`, `onClose`). The runner converts
// the relevant subset of tiles for the active page into `MenuOption` entries
// before each draw.
//
// Per-tile auto-advance metadata (`MenuAdvance`) lets a menu push the next
// page or close itself after `onSelect` without writing transition code in
// every hook. Hooks may also queue transitions manually via the `MenuFlow`
// API (`flow.next(page)`, `flow.back()`, `flow.close()`); explicit hook
// transitions take priority over a tile's auto-advance.
// ---------------------------------------------------------------------------

/// Sentinel reserved by the runner for the standard white back tile.
/// Concrete menu tile ids must not collide with this value.
constexpr uint8_t MENU_BACK_TILE_ID = 0xFF;

/// Maximum nested-page depth (root page + nested pushes).
constexpr uint8_t MENU_PAGE_STACK_DEPTH = 8;

/// How the runner advances after a tile's `onSelect` hook returns.
enum class MenuAdvance : uint8_t {
  STAY,   ///< Remain on the same page (e.g. partial-state capture).
  NEXT,   ///< Push `autoAdvanceTarget` onto the page stack.
  CLOSE,  ///< Close the menu entirely.
};

/// A selectable tile in a menu page.
struct MenuTile {
  uint8_t row;
  uint8_t col;
  LedRGB color;
  uint8_t tileId;             ///< Stable identifier (must not equal MENU_BACK_TILE_ID).
  uint8_t pageId;             ///< Page on which this tile appears.
  MenuAdvance autoAdvance = MenuAdvance::STAY;
  uint8_t autoAdvanceTarget = 0;  ///< For MenuAdvance::NEXT: target pageId.
};

/// Per-page runner configuration (currently: optional back-tile placement).
/// Use `backRow = -1` to omit the back tile for a page.
struct MenuPageConfig {
  uint8_t pageId;
  int8_t backRow = -1;
  int8_t backCol = -1;
};

// ---------------------------------------------------------------------------
// MenuFlow — page-stack and feedback API exposed to menu hooks.
// ---------------------------------------------------------------------------
// Hooks call `flow.next(p)`, `flow.back()`, or `flow.close()` to queue a
// transition that is applied once the hook returns. `currentPage()` reports
// the active page id. `blink()` / `wait()` provide synchronous UX primitives
// available during `onOpen` (e.g. resume confirmation pre-blink).
// ---------------------------------------------------------------------------

class MenuFlow {
 public:
  virtual ~MenuFlow() = default;

  virtual void next(uint8_t pageId) = 0;
  virtual void back() = 0;
  virtual void close() = 0;
  virtual uint8_t currentPage() const = 0;
  virtual void blink(int8_t row, int8_t col, LedRGB color, int times) = 0;
  virtual void wait(uint32_t durationMs) = 0;
};

// ---------------------------------------------------------------------------
// BoardMenu — typed physical-board menu contract.
// ---------------------------------------------------------------------------

class BoardMenu {
 public:
  virtual ~BoardMenu() = default;

  /// Flat array of tiles for all pages.
  virtual const MenuTile* tiles() const = 0;
  /// Number of entries in `tiles()`.
  virtual uint8_t tileCount() const = 0;
  /// Page shown by the runner when the menu is opened. Default 0.
  virtual uint8_t initialPage() const { return 0; }
  /// Page configuration (back-tile placement). Default = no back tile.
  virtual MenuPageConfig pageConfig(uint8_t pageId) const {
    return MenuPageConfig{pageId, -1, -1};
  }

  // -------------------------------------------------------------------------
  // Lifecycle hooks (all fire for both manual and auto-advance triggers).
  // -------------------------------------------------------------------------

  virtual void onOpen(uint8_t pageId, MenuFlow& flow) {
    (void)pageId;
    (void)flow;
  }
  virtual void onNext(uint8_t fromPage, uint8_t toPage, MenuFlow& flow) {
    (void)fromPage;
    (void)toPage;
    (void)flow;
  }
  virtual void onBack(uint8_t fromPage, uint8_t toPage, MenuFlow& flow) {
    (void)fromPage;
    (void)toPage;
    (void)flow;
  }
  virtual void onSelect(uint8_t tileId, MenuFlow& flow) = 0;
  virtual void onClose(MenuFlow& flow) { (void)flow; }
};

// ---------------------------------------------------------------------------
// BoardMenuRunner — board-owned page-stack polling/rendering service.
// ---------------------------------------------------------------------------

class BoardMenuRunner : private MenuFlow {
 public:
  BoardMenuRunner(BoardRuntime& runtime, BoardAnimations& animations);

  BoardMenuRunner(const BoardMenuRunner&) = delete;
  BoardMenuRunner& operator=(const BoardMenuRunner&) = delete;

  /// Display a typed menu and make it the active menu.
  void show(BoardMenu& menu, bool flipped = false);

  /// Poll the active menu once. Returns true only when a menu finishes.
  bool poll();

  /// Run a typed menu until it finishes, polling at board sensor cadence.
  bool run(BoardMenu& menu, bool flipped = false);

  /// Cancel and erase the active menu, if any.
  void stop();

 private:
  // MenuFlow callbacks queue a transition that is processed after the active
  // hook returns. This guarantees deterministic ordering when an `onOpen` or
  // `onSelect` body issues multiple flow calls.
  void next(uint8_t pageId) override;
  void back() override;
  void close() override;
  uint8_t currentPage() const override;
  void blink(int8_t row, int8_t col, LedRGB color, int times) override;
  void wait(uint32_t durationMs) override;

  void drawCurrentPage();
  void clearActiveMenu();
  void applyPendingTransitions();
  bool processSelectionResult(int result);

  enum class Pending : uint8_t { NONE, NEXT, BACK, CLOSE };

  BoardRuntime& runtime_;
  BoardAnimations& animations_;
  MenuSelection selection_;
  BoardMenu* activeMenu_;
  bool finished_;

  uint8_t pageStack_[MENU_PAGE_STACK_DEPTH];
  uint8_t pageStackDepth_;

  Pending pending_;
  uint8_t pendingNextTarget_;
};

#endif  // BOARD_SERVICES_MENU_MENU_H
