#include "board/workflows/menu.h"

#include "board/core/colors.h"
#include "board/core/runtime.h"
#include "board/gui/animations.h"
#include "board/menus/prompt.h"

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

BoardMenu::BoardMenu(BoardRuntime& runtime, BoardAnimations& animations)
    : runtime_(runtime),
      animations_(animations),
      gameMenu_(runtime, animations),
      botDifficultyMenu_(runtime, animations),
      botColorMenu_(runtime, animations),
      pendingBotDifficulty_(4),
      stage_(Stage::IDLE) {
  gameMenu_.setOptions(GAME_MENU_OPTIONS);
  botDifficultyMenu_.setOptions(BOT_DIFFICULTY_OPTIONS);
  botDifficultyMenu_.setBackButton(4, 4);
  botColorMenu_.setOptions(BOT_COLOR_OPTIONS);
  botColorMenu_.setBackButton(4, 4);
}

// ---------------------------------------------------------------------------
// Game-selection state machine.
// ---------------------------------------------------------------------------
// The game selection tree is small (3 levels) and uses three different
// MenuSelection screens. A local state machine keeps the active surface and
// back-navigation rules explicit without a generic tree walker.
// ---------------------------------------------------------------------------

MenuSelection* BoardMenu::activeSelection() {
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
  MenuSelection* active = activeSelection();
  if (!active) return selection;

  int result = active->poll();
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
    case MenuOptionId::CHESS_MOVES:
      selection.mode = GameSelectionMode::CHESS_MOVES;
      clear();
      return selection;
    case MenuOptionId::BOT:
      gameMenu_.erase();
      stage_ = Stage::DIFFICULTY;
      botDifficultyMenu_.reset();
      botDifficultyMenu_.draw();
      return selection;
    case MenuOptionId::LICHESS:
      selection.mode = GameSelectionMode::LICHESS;
      clear();
      return selection;
    case MenuOptionId::BOARD_DIAGNOSTICS:
      selection.mode = GameSelectionMode::BOARD_DIAGNOSTICS;
      clear();
      return selection;
    case MenuOptionId::DIFF_1:
    case MenuOptionId::DIFF_2:
    case MenuOptionId::DIFF_3:
    case MenuOptionId::DIFF_4:
    case MenuOptionId::DIFF_5:
    case MenuOptionId::DIFF_6:
    case MenuOptionId::DIFF_7:
    case MenuOptionId::DIFF_8:
      pendingBotDifficulty_ = static_cast<uint8_t>(result - MenuOptionId::DIFF_1 + 1);
      botDifficultyMenu_.erase();
      stage_ = Stage::COLOR;
      botColorMenu_.reset();
      botColorMenu_.draw();
      return selection;
    case MenuOptionId::PLAY_WHITE:
      selection.mode = GameSelectionMode::BOT;
      selection.botDifficulty = pendingBotDifficulty_;
      selection.playerColor = 'w';
      clear();
      return selection;
    case MenuOptionId::PLAY_BLACK:
      selection.mode = GameSelectionMode::BOT;
      selection.botDifficulty = pendingBotDifficulty_;
      selection.playerColor = 'b';
      clear();
      return selection;
    case MenuOptionId::PLAY_RANDOM:
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
  return MenuPrompt::confirm(runtime_, animations_, flipped);
}

bool BoardMenu::confirmResume(GameSelectionMode mode, bool flipped) {
  {
    auto g = runtime_.lockCanvas();
    animations_.startBlink(3, 3, resumeIndicatorColor(mode), 2, millis());
  }
  // Let the blink play out before the prompt appears.
  delay(900);
  return confirmAction(flipped);
}
