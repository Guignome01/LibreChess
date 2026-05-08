#include "board/workflows/menu.h"

#include "board/config.h"
#include "board/core/colors.h"
#include "board/core/runtime.h"
#include "board/gui/layers.h"

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

BoardMenu::BoardMenu(BoardRuntime& runtime)
    : runtime_(runtime),
      gameMenu_(runtime),
      botDifficultyMenu_(runtime),
      botColorMenu_(runtime),
      pendingBotDifficulty_(4),
      stage_(Stage::IDLE) {
  configureMenus(gameMenu_, botDifficultyMenu_, botColorMenu_);
}

// ---------------------------------------------------------------------------
// Game-selection state machine.
// ---------------------------------------------------------------------------
// The game selection tree is small (3 levels) and uses three different
// MenuView instances. A local state machine keeps the active surface and
// back-navigation rules explicit without a generic tree walker.
// ---------------------------------------------------------------------------

MenuView* BoardMenu::activeView() {
  switch (stage_) {
    case Stage::GAME:
      return &gameMenu_;
    case Stage::DIFFICULTY:
      return &botDifficultyMenu_;
    case Stage::COLOR:
      return &botColorMenu_;
    case Stage::IDLE:
    default:
      return nullptr;
  }
}

void BoardMenu::start() {
  clear();
  pendingBotDifficulty_ = 4;
  stage_ = Stage::GAME;
  gameMenu_.reset();
  gameMenu_.draw();
}

void BoardMenu::clear() {
  stage_ = Stage::IDLE;
  gameMenu_.erase();
  botDifficultyMenu_.erase();
  botColorMenu_.erase();
}

BoardMenu::GameSelection BoardMenu::poll() {
  GameSelection selection;
  MenuView* view = activeView();
  if (!view) return selection;

  int result = view->poll();
  if (result == MENU_RESULT_NONE) return selection;

  if (result == MENU_RESULT_BACK) {
    if (stage_ == Stage::COLOR) {
      botColorMenu_.erase();
      stage_ = Stage::DIFFICULTY;
      botDifficultyMenu_.reset();
      botDifficultyMenu_.draw();
    } else if (stage_ == Stage::DIFFICULTY) {
      botDifficultyMenu_.erase();
      stage_ = Stage::GAME;
      gameMenu_.reset();
      gameMenu_.draw();
    } else {
      // No back from root.
    }
    return selection;
  }

  switch (result) {
    case MenuId::CHESS_MOVES:
      selection.mode = GameSelectionMode::CHESS_MOVES;
      clear();
      return selection;
    case MenuId::BOT:
      gameMenu_.erase();
      stage_ = Stage::DIFFICULTY;
      botDifficultyMenu_.reset();
      botDifficultyMenu_.draw();
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
      botDifficultyMenu_.erase();
      stage_ = Stage::COLOR;
      botColorMenu_.reset();
      botColorMenu_.draw();
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
  return confirmBoardPrompt(runtime_, flipped);
}

bool BoardMenu::confirmResume(GameSelectionMode mode, bool flipped) {
  {
    auto g = runtime_.lockCanvas();
    g.effects.startBlink(3, 3, resumeIndicatorColor(mode), 2, millis(), BoardLayer::MENU);
  }
  // Let the blink play out before the prompt appears.
  delay(900);
  return confirmAction(flipped);
}
