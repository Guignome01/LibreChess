#ifndef BOARD_MENUS_GAME_SELECTION_H
#define BOARD_MENUS_GAME_SELECTION_H

#include "board/menus/selection_types.h"
#include "board/services/menu/menu.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// GameSelectionMenu - predefined bot setup menu.
// ---------------------------------------------------------------------------
// Owns the bot-specific selection pages. MainMenu embeds this menu and
// delegates bot setup navigation to it, while tests can still exercise the
// submenu directly as a standalone typed menu.
// ---------------------------------------------------------------------------

/// Stable bot setup tile ids returned through `BoardMenu::onSelect`.
namespace GameSelectionMenuOptionId {
constexpr uint8_t DIFF_1 = 10;
constexpr uint8_t DIFF_2 = 11;
constexpr uint8_t DIFF_3 = 12;
constexpr uint8_t DIFF_4 = 13;
constexpr uint8_t DIFF_5 = 14;
constexpr uint8_t DIFF_6 = 15;
constexpr uint8_t DIFF_7 = 16;
constexpr uint8_t DIFF_8 = 17;

constexpr uint8_t PLAY_WHITE = 20;
constexpr uint8_t PLAY_BLACK = 21;
constexpr uint8_t PLAY_RANDOM = 22;
}  // namespace GameSelectionMenuOptionId

/// Page ids used by GameSelectionMenu. Page 0 is reserved for MainMenu root.
constexpr uint8_t GAME_SELECTION_PAGE_DIFFICULTY = 1;
constexpr uint8_t GAME_SELECTION_PAGE_COLOR = 2;

class GameSelectionMenu final : public BoardMenu {
 public:
  static constexpr uint8_t TILE_COUNT = 11;

  GameSelectionMenu();

  const MenuTile* tiles() const override;
  uint8_t tileCount() const override;
  uint8_t initialPage() const override { return GAME_SELECTION_PAGE_DIFFICULTY; }
  MenuPageConfig pageConfig(uint8_t pageId) const override;

  void onOpen(uint8_t pageId, MenuFlow& flow) override;
  void onBack(uint8_t fromPage, uint8_t toPage, MenuFlow& flow) override;
  void onSelect(uint8_t tileId, MenuFlow& flow) override;

  const BoardGameSelection& selection() const { return selection_; }
  bool hasSelection() const { return selection_.hasSelection(); }
  void reset();

 private:
  BoardGameSelection selection_;
  uint8_t pendingBotDifficulty_;

  char randomPlayerColor() const;
};

#endif  // BOARD_MENUS_GAME_SELECTION_H