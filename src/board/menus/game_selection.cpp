#include "board/menus/game_selection.h"

#include <stdlib.h>

namespace {

using namespace GameSelectionMenuOptionId;

static constexpr MenuOption GAME_MENU_OPTIONS[] = {
    {3, 3, LedColors::Blue, CHESS_MOVES},
    {3, 4, LedColors::Green, BOT},
    {4, 3, LedColors::Yellow, LICHESS},
    {4, 4, LedColors::Red, BOARD_DIAGNOSTICS},
};

static constexpr MenuOption BOT_DIFFICULTY_OPTIONS[] = {
    {3, 0, LedColors::Green, DIFF_1},
    {3, 1, LedColors::Lime, DIFF_2},
    {3, 2, LedColors::Yellow, DIFF_3},
    {3, 3, LedColors::Orange, DIFF_4},
    {3, 4, LedColors::Red, DIFF_5},
    {3, 5, LedColors::Crimson, DIFF_6},
    {3, 6, LedColors::Purple, DIFF_7},
    {3, 7, LedColors::Blue, DIFF_8},
};

static constexpr MenuOption BOT_COLOR_OPTIONS[] = {
    {3, 3, LedColors::White, PLAY_WHITE},
    {3, 4, LedColors::scaleColor(LedColors::White, 40.0f / 255.0f), PLAY_BLACK},
    {3, 5, LedColors::Yellow, PLAY_RANDOM},
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
    : selection_(), pendingBotDifficulty_(4), stage_(Stage::IDLE) {}

void GameSelectionMenu::begin(BoardMenuController& controller) {
  reset();
  showGame(controller);
}

void GameSelectionMenu::onSelect(int optionId, BoardMenuController& controller) {
  switch (optionId) {
    case GameSelectionMenuOptionId::CHESS_MOVES:
      finish(controller, BoardGameSelection{BoardGameSelectionMode::CHESS_MOVES, 0, ' '});
      return;
    case GameSelectionMenuOptionId::BOT:
      showDifficulty(controller);
      return;
    case GameSelectionMenuOptionId::LICHESS:
      finish(controller, BoardGameSelection{BoardGameSelectionMode::LICHESS, 0, ' '});
      return;
    case GameSelectionMenuOptionId::BOARD_DIAGNOSTICS:
      finish(controller, BoardGameSelection{BoardGameSelectionMode::BOARD_DIAGNOSTICS, 0, ' '});
      return;
    case GameSelectionMenuOptionId::DIFF_1:
    case GameSelectionMenuOptionId::DIFF_2:
    case GameSelectionMenuOptionId::DIFF_3:
    case GameSelectionMenuOptionId::DIFF_4:
    case GameSelectionMenuOptionId::DIFF_5:
    case GameSelectionMenuOptionId::DIFF_6:
    case GameSelectionMenuOptionId::DIFF_7:
    case GameSelectionMenuOptionId::DIFF_8:
      pendingBotDifficulty_ = static_cast<uint8_t>(optionId - GameSelectionMenuOptionId::DIFF_1 + 1);
      showColor(controller);
      return;
    case GameSelectionMenuOptionId::PLAY_WHITE:
      finish(controller, BoardGameSelection{BoardGameSelectionMode::BOT, pendingBotDifficulty_, 'w'});
      return;
    case GameSelectionMenuOptionId::PLAY_BLACK:
      finish(controller, BoardGameSelection{BoardGameSelectionMode::BOT, pendingBotDifficulty_, 'b'});
      return;
    case GameSelectionMenuOptionId::PLAY_RANDOM:
      finish(controller,
             BoardGameSelection{BoardGameSelectionMode::BOT, pendingBotDifficulty_, randomPlayerColor()});
      return;
    default:
      return;
  }
}

void GameSelectionMenu::onBack(BoardMenuController& controller) {
  if (stage_ == Stage::COLOR) {
    showDifficulty(controller);
  } else if (stage_ == Stage::DIFFICULTY) {
    showGame(controller);
  }
}

void GameSelectionMenu::cancel(BoardMenuController& controller) {
  reset();
  controller.erase();
}

void GameSelectionMenu::reset() {
  selection_ = {};
  pendingBotDifficulty_ = 4;
  stage_ = Stage::IDLE;
}

void GameSelectionMenu::showGame(BoardMenuController& controller) {
  stage_ = Stage::GAME;
  controller.show(GAME_MENU_OPTIONS);
}

void GameSelectionMenu::showDifficulty(BoardMenuController& controller) {
  stage_ = Stage::DIFFICULTY;
  controller.showWithBack(BOT_DIFFICULTY_OPTIONS, 4, 4);
}

void GameSelectionMenu::showColor(BoardMenuController& controller) {
  stage_ = Stage::COLOR;
  controller.showWithBack(BOT_COLOR_OPTIONS, 4, 4);
}

void GameSelectionMenu::finish(BoardMenuController& controller, BoardGameSelection selection) {
  selection_ = selection;
  stage_ = Stage::IDLE;
  controller.finish();
}

char GameSelectionMenu::randomPlayerColor() const {
  return (rand() % 2 == 0) ? 'w' : 'b';
}