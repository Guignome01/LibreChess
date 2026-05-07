#include "menu.h"

#include "config.h"
#include "core/controller.h"
#include "core/colors.h"
#include "gui/animations.h"
#include "gui/stack.h"

#include <Arduino.h>

namespace {

LedRGB resumeIndicatorColor(BoardGameSelectionMode mode) {
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

}  // namespace

BoardMenu::BoardMenu(BoardController& board)
    : BoardWorkflow(board),
  gameMenu_(board, board.layering()),
  botDifficultyMenu_(board, board.layering()),
  botColorMenu_(board, board.layering()),
      pendingBotDifficulty_(4) {
  configureMenus(gameMenu_, botDifficultyMenu_, botColorMenu_);
}

void BoardMenu::start() {
  clear();
  pendingBotDifficulty_ = 4;
  board().stack().push(&gameMenu_);
}

void BoardMenu::clear() {
  board().stack().clear();
}

BoardMenu::GameSelection BoardMenu::poll() {
  board().readSensors();

  GameSelection selection;
  int result = board().stack().poll();
  if (result == BoardDrawable::RESULT_NONE || result == BoardDrawable::RESULT_BACK)
    return selection;

  switch (result) {
    case MenuId::CHESS_MOVES:
      selection.mode = GameSelectionMode::CHESS_MOVES;
      clear();
      return selection;
    case MenuId::BOT:
      board().stack().push(&botDifficultyMenu_);
      return selection;
    case MenuId::LICHESS:
      selection.mode = GameSelectionMode::LICHESS;
      clear();
      return selection;
    case MenuId::BOARD_DIAGNOSTICS:
      selection.mode = GameSelectionMode::BOARD_DIAGNOSTICS;
      clear();
      return selection;
    case MenuId::DIFF_1:
    case MenuId::DIFF_2:
    case MenuId::DIFF_3:
    case MenuId::DIFF_4:
    case MenuId::DIFF_5:
    case MenuId::DIFF_6:
    case MenuId::DIFF_7:
    case MenuId::DIFF_8:
      pendingBotDifficulty_ = static_cast<uint8_t>(result - MenuId::DIFF_1 + 1);
      board().stack().push(&botColorMenu_);
      return selection;
    case MenuId::PLAY_WHITE:
      selection.mode = GameSelectionMode::BOT;
      selection.botDifficulty = pendingBotDifficulty_;
      selection.playerColor = 'w';
      clear();
      return selection;
    case MenuId::PLAY_BLACK:
      selection.mode = GameSelectionMode::BOT;
      selection.botDifficulty = pendingBotDifficulty_;
      selection.playerColor = 'b';
      clear();
      return selection;
    case MenuId::PLAY_RANDOM:
      selection.mode = GameSelectionMode::BOT;
      selection.botDifficulty = pendingBotDifficulty_;
      selection.playerColor = (random(2) == 0) ? 'w' : 'b';
      clear();
      return selection;
    default:
      return selection;
  }
}

bool BoardMenu::confirmAction(bool flipped) {
  // Two-square modal prompt rendered via a transient MenuView. Implemented
  // inline rather than as a separate workflow type because confirmation is a
  // pure request/response interaction that never coexists with another
  // stack frame and shares the menu primitive's debounce logic.
  static constexpr MenuItem confirmItems[] = {
      {4, 3, LedColors::Green, 1},  // Yes -- d4
      {4, 4, LedColors::Red, 0},    // No  -- e4
  };

  MenuView prompt(board(), board().layering());
  prompt.setItems(confirmItems, 2);
  prompt.setFlipped(flipped);
  return prompt.waitForSelection() == 1;
}

bool BoardMenu::confirmResume(GameSelectionMode mode, bool flipped) {
  board().runAnimation(AnimationJob::blink(3, 3, resumeIndicatorColor(mode), 2));
  board().waitForAnimationQueueDrain();
  return confirmAction(flipped);
}
