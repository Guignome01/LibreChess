#ifndef BOARD_SELECTION_H
#define BOARD_SELECTION_H

#include <cstdint>

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

#endif  // BOARD_SELECTION_H
