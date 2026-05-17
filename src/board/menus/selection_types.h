#ifndef BOARD_MENUS_SELECTION_TYPES_H
#define BOARD_MENUS_SELECTION_TYPES_H

#include <stdint.h>

/// Public game mode selected through the physical board menu flow.
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

  bool hasSelection() const {
    if (mode == BoardGameSelectionMode::NONE) return false;
    if (mode != BoardGameSelectionMode::BOT) return true;
    return botDifficulty != 0 && playerColor != ' ';
  }
};

#endif  // BOARD_MENUS_SELECTION_TYPES_H