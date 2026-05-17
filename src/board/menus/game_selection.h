#ifndef BOARD_MENUS_GAME_SELECTION_H
#define BOARD_MENUS_GAME_SELECTION_H

#include "board/services/menu/menu.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// GameSelectionMenu — predefined game-selection tree.
// ---------------------------------------------------------------------------
// Three pages organized as a single declarative tile array:
//   - Page 0 (GAME): pick a game mode. CHESS_MOVES / LICHESS /
//     BOARD_DIAGNOSTICS auto-close; BOT auto-advances to DIFFICULTY.
//   - Page 1 (DIFFICULTY): DIFF_1..DIFF_8 each auto-advance to COLOR.
//   - Page 2 (COLOR): PLAY_WHITE / PLAY_BLACK / PLAY_RANDOM auto-close.
// Back tiles are present on the DIFFICULTY and COLOR pages.
// ---------------------------------------------------------------------------

/// Public game mode selected through the physical board menu.
enum class BoardGameSelectionMode : uint8_t {
  NONE = 0,
  CHESS_MOVES = 1,
  BOT = 2,
  LICHESS = 3,
  BOARD_DIAGNOSTICS = 4,
};

/// Public game selection returned by the physical board menu flow.
struct BoardGameSelection {
  BoardGameSelectionMode mode = BoardGameSelectionMode::NONE;
  uint8_t botDifficulty = 0;
  char playerColor = ' ';

  bool hasSelection() const { return mode != BoardGameSelectionMode::NONE; }
};

/// Stable tile ids returned through `BoardMenu::onSelect`.
namespace GameSelectionMenuOptionId {
constexpr uint8_t CHESS_MOVES = 0;
constexpr uint8_t BOT = 1;
constexpr uint8_t LICHESS = 2;
constexpr uint8_t BOARD_DIAGNOSTICS = 3;

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

/// Page ids for the game-selection menu.
constexpr uint8_t GAME_SELECTION_PAGE_GAME = 0;
constexpr uint8_t GAME_SELECTION_PAGE_DIFFICULTY = 1;
constexpr uint8_t GAME_SELECTION_PAGE_COLOR = 2;

/// Return the mode-coloured indicator used before resume confirmation.
LedRGB gameSelectionResumeIndicatorColor(BoardGameSelectionMode mode);

class GameSelectionMenu final : public BoardMenu {
 public:
  GameSelectionMenu();

  const MenuTile* tiles() const override;
  uint8_t tileCount() const override;
  MenuPageConfig pageConfig(uint8_t pageId) const override;

  void onOpen(uint8_t pageId, MenuFlow& flow) override;
  void onBack(uint8_t fromPage, uint8_t toPage, MenuFlow& flow) override;
  void onSelect(uint8_t tileId, MenuFlow& flow) override;
  void onClose(MenuFlow& flow) override;

  /// Final selection (valid once the menu closes via auto-advance CLOSE).
  const BoardGameSelection& selection() const { return selection_; }
  bool hasSelection() const { return selection_.hasSelection(); }

  /// Callback invoked from `onClose` when a complete selection was made.
  /// Wired once at setup; replaces external polling of `hasSelection()`.
  using SelectionCallback = void (*)(const BoardGameSelection&);
  void setOnSelected(SelectionCallback callback) { callback_ = callback; }

 private:
  BoardGameSelection selection_;
  uint8_t pendingBotDifficulty_;
  SelectionCallback callback_ = nullptr;

  void resetState();
  char randomPlayerColor() const;
};

#endif  // BOARD_MENUS_GAME_SELECTION_H
