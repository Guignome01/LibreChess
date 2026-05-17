#include "board/menus/main.h"

namespace {

using namespace MainMenuOptionId;

// Root menu authored in white-side orientation (row 7 = rank 1).
static constexpr MenuTile ROOT_TILES[] = {
    {3, 3, LedColors::Blue, CHESS_MOVES, MAIN_MENU_PAGE_ROOT,
     MenuAdvance::CLOSE, 0},
    {3, 4, LedColors::Green, BOT, MAIN_MENU_PAGE_ROOT,
     MenuAdvance::NEXT, GAME_SELECTION_PAGE_DIFFICULTY},
    {4, 3, LedColors::Yellow, LICHESS, MAIN_MENU_PAGE_ROOT,
     MenuAdvance::CLOSE, 0},
    {4, 4, LedColors::Red, BOARD_DIAGNOSTICS, MAIN_MENU_PAGE_ROOT,
     MenuAdvance::CLOSE, 0},
};

bool isGameSelectionTile(uint8_t tileId) {
  return (tileId >= GameSelectionMenuOptionId::DIFF_1 &&
          tileId <= GameSelectionMenuOptionId::DIFF_8) ||
         (tileId >= GameSelectionMenuOptionId::PLAY_WHITE &&
          tileId <= GameSelectionMenuOptionId::PLAY_RANDOM);
}

static constexpr const char* MAIN_MENU_PROMPT_LINES[] = {
    "==================== Main Menu ====================",
    "Four LEDs are lit in the center of the board:",
    "  Blue:   Chess Moves (Human vs Human)",
    "  Green:  Chess Bot (Human vs AI)",
    "  Yellow: Lichess (Play online games)",
    "  Red:    Sensor Test",
    "Place a chess piece on a LED, then lift it to select that mode",
    "===================================================",
};

}  // namespace

LedRGB mainMenuModeColor(BoardGameSelectionMode mode) {
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

MainMenu::MainMenu() : gameSelectionMenu_(), tiles_(), selection_() {
  copyTiles();
}

const MenuTile* MainMenu::tiles() const { return tiles_; }

uint8_t MainMenu::tileCount() const { return MAIN_MENU_TILE_COUNT; }

MenuPageConfig MainMenu::pageConfig(uint8_t pageId) const {
  if (isGameSelectionPage(pageId)) return gameSelectionMenu_.pageConfig(pageId);
  return MenuPageConfig{pageId, -1, -1};
}

void MainMenu::onOpen(uint8_t pageId, MenuFlow& flow) {
  (void)flow;
  if (pageId == MAIN_MENU_PAGE_ROOT) {
    resetState();
  } else if (isGameSelectionPage(pageId)) {
    gameSelectionMenu_.onOpen(pageId, flow);
  }
}

void MainMenu::onNext(uint8_t fromPage, uint8_t toPage, MenuFlow& flow) {
  if (fromPage == MAIN_MENU_PAGE_ROOT && toPage == GAME_SELECTION_PAGE_DIFFICULTY) {
    gameSelectionMenu_.onOpen(toPage, flow);
    return;
  }
  if (isGameSelectionPage(fromPage) || isGameSelectionPage(toPage)) {
    gameSelectionMenu_.onNext(fromPage, toPage, flow);
  }
}

void MainMenu::onBack(uint8_t fromPage, uint8_t toPage, MenuFlow& flow) {
  if (toPage == MAIN_MENU_PAGE_ROOT) {
    resetState();
    return;
  }
  if (isGameSelectionPage(fromPage) || isGameSelectionPage(toPage)) {
    gameSelectionMenu_.onBack(fromPage, toPage, flow);
    selection_ = gameSelectionMenu_.selection();
  }
}

void MainMenu::onSelect(uint8_t tileId, MenuFlow& flow) {
  if (isGameSelectionTile(tileId)) {
    gameSelectionMenu_.onSelect(tileId, flow);
    selection_ = gameSelectionMenu_.selection();
    return;
  }
  switch (tileId) {
    case CHESS_MOVES:
      selection_ = BoardGameSelection{BoardGameSelectionMode::CHESS_MOVES, 0, ' '};
      return;
    case BOT:
      selection_.mode = BoardGameSelectionMode::BOT;
      return;
    case LICHESS:
      selection_ = BoardGameSelection{BoardGameSelectionMode::LICHESS, 0, ' '};
      return;
    case BOARD_DIAGNOSTICS:
      selection_ = BoardGameSelection{BoardGameSelectionMode::BOARD_DIAGNOSTICS,
                                      0, ' '};
      return;
    default:
      resetState();
      return;
  }
}

void MainMenu::open(MainMenuHost& host) {
  resetState();
  host.stopProgram();
  host.clearAssistanceProvider();
  host.showMenu(*this);
}

bool MainMenu::canOpen(MainMenuHost& host) {
  return !host.hasActiveAnimations();
}

MainMenuUpdateResult MainMenu::update(MainMenuHost& host, bool menuFinished) {
  if (!menuFinished) return MainMenuUpdateResult::WAITING;
  if (hasSelection()) return MainMenuUpdateResult::SELECTED;
  open(host);
  return MainMenuUpdateResult::REOPENED;
}

uint8_t MainMenu::promptLineCount() const {
  return sizeof(MAIN_MENU_PROMPT_LINES) / sizeof(MAIN_MENU_PROMPT_LINES[0]);
}

const char* MainMenu::promptLine(uint8_t index) const {
  if (index >= promptLineCount()) return "";
  return MAIN_MENU_PROMPT_LINES[index];
}

void MainMenu::copyTiles() {
  for (uint8_t i = 0; i < MAIN_MENU_ROOT_TILE_COUNT; ++i) {
    tiles_[i] = ROOT_TILES[i];
  }
  const MenuTile* childTiles = gameSelectionMenu_.tiles();
  for (uint8_t i = 0; i < GameSelectionMenu::TILE_COUNT; ++i) {
    tiles_[MAIN_MENU_ROOT_TILE_COUNT + i] = childTiles[i];
  }
}

void MainMenu::resetState() {
  selection_ = BoardGameSelection{};
  gameSelectionMenu_.reset();
}

bool MainMenu::isGameSelectionPage(uint8_t pageId) const {
  return pageId == GAME_SELECTION_PAGE_DIFFICULTY ||
         pageId == GAME_SELECTION_PAGE_COLOR;
}