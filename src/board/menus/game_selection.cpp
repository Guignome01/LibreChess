#include "board/menus/game_selection.h"

#include <stdlib.h>

namespace {

using namespace GameSelectionMenuOptionId;

// Bot setup pages authored in white-side orientation (row 7 = rank 1).
static constexpr MenuTile GAME_SELECTION_TILES[] = {
    // ---- Page 1: DIFFICULTY ----
    {3, 0, LedColors::Green, DIFF_1, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 1, LedColors::Lime, DIFF_2, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 2, LedColors::Yellow, DIFF_3, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 3, LedColors::Orange, DIFF_4, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 4, LedColors::Red, DIFF_5, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 5, LedColors::Crimson, DIFF_6, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 6, LedColors::Purple, DIFF_7, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},
    {3, 7, LedColors::Blue, DIFF_8, GAME_SELECTION_PAGE_DIFFICULTY,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_COLOR},

    // ---- Page 2: COLOR ----
    {3, 3, LedColors::White, PLAY_WHITE, GAME_SELECTION_PAGE_COLOR,
     MenuAdvance::CLOSE, 0},
    {3, 4, LedColors::scaleColor(LedColors::White, 40.0f / 255.0f),
     PLAY_BLACK, GAME_SELECTION_PAGE_COLOR, MenuAdvance::CLOSE, 0},
    {3, 5, LedColors::Yellow, PLAY_RANDOM, GAME_SELECTION_PAGE_COLOR,
     MenuAdvance::CLOSE, 0},
};

static_assert(sizeof(GAME_SELECTION_TILES) / sizeof(GAME_SELECTION_TILES[0]) ==
                  GameSelectionMenu::TILE_COUNT,
              "GameSelectionMenu tile count mismatch");

}  // namespace

GameSelectionMenu::GameSelectionMenu() : selection_(), pendingBotDifficulty_(4) {}

const MenuTile* GameSelectionMenu::tiles() const { return GAME_SELECTION_TILES; }

uint8_t GameSelectionMenu::tileCount() const {
  return sizeof(GAME_SELECTION_TILES) / sizeof(GAME_SELECTION_TILES[0]);
}

MenuPageConfig GameSelectionMenu::pageConfig(uint8_t pageId) const {
  return MenuPageConfig{pageId, 4, 4};
}

void GameSelectionMenu::onOpen(uint8_t pageId, MenuFlow& flow) {
  (void)flow;
  if (pageId == GAME_SELECTION_PAGE_DIFFICULTY) reset();
}

void GameSelectionMenu::onBack(uint8_t fromPage, uint8_t toPage,
                               MenuFlow& flow) {
  (void)fromPage;
  (void)flow;
  if (toPage == GAME_SELECTION_PAGE_DIFFICULTY) {
    selection_.playerColor = ' ';
  }
}

void GameSelectionMenu::onSelect(uint8_t tileId, MenuFlow& flow) {
  (void)flow;
  if (tileId >= DIFF_1 && tileId <= DIFF_8) {
    pendingBotDifficulty_ = static_cast<uint8_t>(tileId - DIFF_1 + 1);
    selection_.mode = BoardGameSelectionMode::BOT;
    selection_.botDifficulty = pendingBotDifficulty_;
    return;
  }
  switch (tileId) {
    case PLAY_WHITE:
      selection_.mode = BoardGameSelectionMode::BOT;
      selection_.playerColor = 'w';
      return;
    case PLAY_BLACK:
      selection_.mode = BoardGameSelectionMode::BOT;
      selection_.playerColor = 'b';
      return;
    case PLAY_RANDOM:
      selection_.mode = BoardGameSelectionMode::BOT;
      selection_.playerColor = randomPlayerColor();
      return;
    default:
      return;
  }
}

void GameSelectionMenu::reset() {
  selection_ = BoardGameSelection{};
  pendingBotDifficulty_ = 4;
}

char GameSelectionMenu::randomPlayerColor() const {
  return (rand() % 2 == 0) ? 'w' : 'b';
}
