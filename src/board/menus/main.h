#ifndef BOARD_MENUS_MAIN_H
#define BOARD_MENUS_MAIN_H

#include "board/menus/game_selection.h"
#include "board/services/menu/menu.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// MainMenu - predefined root menu plus child-menu orchestration.
// ---------------------------------------------------------------------------
// MainMenu owns the root mode-selection page and embeds GameSelectionMenu for
// bot difficulty/color setup. Callers show only MainMenu; internal routing
// between root and child pages stays inside this menu object.
// ---------------------------------------------------------------------------

/// Stable tile ids returned through `BoardMenu::onSelect`.
namespace MainMenuOptionId {
constexpr uint8_t CHESS_MOVES = 0;
constexpr uint8_t BOT = 1;
constexpr uint8_t LICHESS = 2;
constexpr uint8_t BOARD_DIAGNOSTICS = 3;
}  // namespace MainMenuOptionId

/// Page id for the root mode menu.
constexpr uint8_t MAIN_MENU_PAGE_ROOT = 0;

constexpr uint8_t MAIN_MENU_ROOT_TILE_COUNT = 4;
constexpr uint8_t MAIN_MENU_TILE_COUNT =
    MAIN_MENU_ROOT_TILE_COUNT + GameSelectionMenu::TILE_COUNT;

enum class MainMenuUpdateResult : uint8_t {
  WAITING = 0,
  SELECTED = 1,
  REOPENED = 2,
};

class MainMenuHost {
 public:
  virtual ~MainMenuHost() = default;

  virtual void stopProgram() = 0;
  virtual void clearAssistanceProvider() = 0;
  virtual void showMenu(BoardMenu& menu) = 0;
  virtual bool hasActiveAnimations() = 0;
};

/// Return the mode-coloured indicator used by root and resume menus.
LedRGB mainMenuModeColor(BoardGameSelectionMode mode);

class MainMenu final : public BoardMenu {
 public:
  MainMenu();

  const MenuTile* tiles() const override;
  uint8_t tileCount() const override;
  MenuPageConfig pageConfig(uint8_t pageId) const override;

  void onOpen(uint8_t pageId, MenuFlow& flow) override;
  void onNext(uint8_t fromPage, uint8_t toPage, MenuFlow& flow) override;
  void onBack(uint8_t fromPage, uint8_t toPage, MenuFlow& flow) override;
  void onSelect(uint8_t tileId, MenuFlow& flow) override;

  void open(MainMenuHost& host);
  bool canOpen(MainMenuHost& host);
  MainMenuUpdateResult update(MainMenuHost& host, bool menuFinished);

  uint8_t promptLineCount() const;
  const char* promptLine(uint8_t index) const;

  /// Final selection (valid once the menu closes via auto-advance CLOSE).
  const BoardGameSelection& selection() const { return selection_; }
  bool hasSelection() const { return selection_.hasSelection(); }

 private:
  GameSelectionMenu gameSelectionMenu_;
  MenuTile tiles_[MAIN_MENU_TILE_COUNT];
  BoardGameSelection selection_;

  void copyTiles();
  void resetState();
  bool isGameSelectionPage(uint8_t pageId) const;
};

#endif  // BOARD_MENUS_MAIN_H