#include "board/menus/game_selection.h"

#include <stdlib.h>

namespace {

using namespace GameSelectionMenuOptionId;

// Flat tile array spanning all three pages. Authored in white-side
// orientation (row 7 = rank 1). Auto-advance metadata routes the user
// through the GAME → DIFFICULTY → COLOR flow without explicit hook code.
static constexpr MenuTile TILES[] = {
    // ---- Page 0: GAME ----
    {3, 3, LedColors::Blue,   CHESS_MOVES,       GAME_SELECTION_PAGE_GAME,
     MenuAdvance::CLOSE, 0},
    {3, 4, LedColors::Green,  BOT,               GAME_SELECTION_PAGE_GAME,
     MenuAdvance::NEXT,  GAME_SELECTION_PAGE_DIFFICULTY},
    {4, 3, LedColors::Yellow, LICHESS,           GAME_SELECTION_PAGE_GAME,
     MenuAdvance::CLOSE, 0},
    {4, 4, LedColors::Red,    BOARD_DIAGNOSTICS, GAME_SELECTION_PAGE_GAME,
     MenuAdvance::CLOSE, 0},

    // ---- Page 1: DIFFICULTY ----
    {3, 0, LedColors::Green,   DIFF_1, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 1, LedColors::Lime,    DIFF_2, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 2, LedColors::Yellow,  DIFF_3, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 3, LedColors::Orange,  DIFF_4, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 4, LedColors::Red,     DIFF_5, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 5, LedColors::Crimson, DIFF_6, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 6, LedColors::Purple,  DIFF_7, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 7, LedColors::Blue,    DIFF_8, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},

    // ---- Page 2: COLOR ----
    {3, 3, LedColors::White,
     PLAY_WHITE, GAME_SELECTION_PAGE_COLOR, MenuAdvance::CLOSE, 0},
    {3, 4, LedColors::scaleColor(LedColors::White, 40.0f / 255.0f),
     PLAY_BLACK, GAME_SELECTION_PAGE_COLOR, MenuAdvance::CLOSE, 0},
    {3, 5, LedColors::Yellow,
     PLAY_RANDOM, GAME_SELECTION_PAGE_COLOR, MenuAdvance::CLOSE, 0},
};

}  // namespace

LedRGB gameSelectionResumeIndicatorColor(BoardGameSelectionMode mode) {
  switch (mode) {
    case BoardGameSelectionMode::CHESS_MOVES:
      return LedColors::Blue;
    case BoardGameSelectionMode::BOT:
      return LedColors::Green;
    case BoardGameSelectionMode::LICHESS:
      return LedColors::Yellow;
    case BoardGameSelectionMode::BOARD_DIAGNOSTICS:
      return LedColors::Red;
    case BoardGameSelectionMode::NONE:
    default:
      return LedColors::White;
  }
}

GameSelectionMenu::GameSelectionMenu()
    : selection_(), pendingBotDifficulty_(4) {}

const MenuTile* GameSelectionMenu::tiles() const { return TILES; }
uint8_t GameSelectionMenu::tileCount() const {
  return sizeof(TILES) / sizeof(TILES[0]);
}

MenuPageConfig GameSelectionMenu::pageConfig(uint8_t pageId) const {
  // The root GAME page exposes no back tile (back == close); deeper pages
  // always offer the standard white back tile at (4,4).
  if (pageId == GAME_SELECTION_PAGE_DIFFICULTY ||
      pageId == GAME_SELECTION_PAGE_COLOR) {
    return MenuPageConfig{pageId, 4, 4};
  }
  return MenuPageConfig{pageId, -1, -1};
}

void GameSelectionMenu::onOpen(uint8_t pageId, MenuFlow& flow) {
  (void)flow;
  if (pageId == GAME_SELECTION_PAGE_GAME) {
    resetState();
  }
}

void GameSelectionMenu::onBack(uint8_t fromPage, uint8_t toPage, MenuFlow& flow) {
  (void)fromPage;
  (void)flow;
  // Going back to a page discards the selections captured on the page
  // (and all pages) we are leaving.
  if (toPage == GAME_SELECTION_PAGE_GAME) {
    resetState();
  } else if (toPage == GAME_SELECTION_PAGE_DIFFICULTY) {
    selection_.playerColor = ' ';
  }
}

void GameSelectionMenu::onSelect(uint8_t tileId, MenuFlow& flow) {
  (void)flow;
  using namespace GameSelectionMenuOptionId;
  // Difficulty tiles form a contiguous id range (DIFF_1..DIFF_8). Handle
  // them with a range check instead of an 8-case fall-through cascade so
  // the dispatch table stays small.
  if (tileId >= DIFF_1 && tileId <= DIFF_8) {
    pendingBotDifficulty_ = static_cast<uint8_t>(tileId - DIFF_1 + 1);
    selection_.botDifficulty = pendingBotDifficulty_;
    return;
  }
  switch (tileId) {
    case CHESS_MOVES:
      selection_ = BoardGameSelection{BoardGameSelectionMode::CHESS_MOVES, 0, ' '};
      return;
    case BOT:
      // Only the mode is fixed at this point; difficulty/color are captured
      // on the subsequent pages.
      selection_.mode = BoardGameSelectionMode::BOT;
      return;
    case LICHESS:
      selection_ = BoardGameSelection{BoardGameSelectionMode::LICHESS, 0, ' '};
      return;
    case BOARD_DIAGNOSTICS:
      selection_ = BoardGameSelection{BoardGameSelectionMode::BOARD_DIAGNOSTICS, 0, ' '};
      return;
    case PLAY_WHITE:
      selection_.playerColor = 'w';
      return;
    case PLAY_BLACK:
      selection_.playerColor = 'b';
      return;
    case PLAY_RANDOM:
      selection_.playerColor = randomPlayerColor();
      return;
    default:
      return;
  }
}

void GameSelectionMenu::onClose(MenuFlow& flow) {
  (void)flow;
  // A final selection is one whose mode is set AND, for BOT, has
  // difficulty + player color resolved. Other modes (CHESS_MOVES, LICHESS,
  // BOARD_DIAGNOSTICS) close from page 0 with the mode already set.
  if (!selection_.hasSelection()) return;
  if (selection_.mode == BoardGameSelectionMode::BOT) {
    if (selection_.botDifficulty == 0 || selection_.playerColor == ' ') return;
  }
  if (callback_) callback_(selection_);
}

void GameSelectionMenu::resetState() {
  selection_ = BoardGameSelection{};
  pendingBotDifficulty_ = 4;
}

char GameSelectionMenu::randomPlayerColor() const {
  return (rand() % 2 == 0) ? 'w' : 'b';
}
