#ifndef BOARD_MENUS_OPTIONS_H
#define BOARD_MENUS_OPTIONS_H

#include "board/core/colors.h"

#include <stdint.h>

static constexpr int MENU_RESULT_NONE = -1;
static constexpr int MENU_RESULT_BACK = -2;
static constexpr uint8_t MENU_SELECTION_OPTION_COUNT = 16;
static constexpr uint8_t MENU_PANEL_OPTION_COUNT = MENU_SELECTION_OPTION_COUNT + 1;

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

/// Identifiers returned when a physical menu option is selected.
namespace MenuOptionId {
constexpr int8_t CHESS_MOVES = 0;
constexpr int8_t BOT = 1;
constexpr int8_t LICHESS = 2;
constexpr int8_t BOARD_DIAGNOSTICS = 3;

constexpr int8_t DIFF_1 = 10;
constexpr int8_t DIFF_2 = 11;
constexpr int8_t DIFF_3 = 12;
constexpr int8_t DIFF_4 = 13;
constexpr int8_t DIFF_5 = 14;
constexpr int8_t DIFF_6 = 15;
constexpr int8_t DIFF_7 = 16;
constexpr int8_t DIFF_8 = 17;

constexpr int8_t PLAY_WHITE = 20;
constexpr int8_t PLAY_BLACK = 21;
constexpr int8_t PLAY_RANDOM = 22;

constexpr int8_t CONFIRM_NO = 30;
constexpr int8_t CONFIRM_YES = 31;
}  // namespace MenuOptionId

/// A selectable LED square in a physical board menu.
/// Coordinates are authored in white-side orientation
/// (row 7 = rank 1 = white's back rank).
struct MenuOption {
  int8_t row;
  int8_t col;
  LedRGB color;
  int8_t id;
};

static constexpr MenuOption GAME_MENU_OPTIONS[] = {
    {3, 3, LedColors::Blue, MenuOptionId::CHESS_MOVES},
    {3, 4, LedColors::Green, MenuOptionId::BOT},
    {4, 3, LedColors::Yellow, MenuOptionId::LICHESS},
    {4, 4, LedColors::Red, MenuOptionId::BOARD_DIAGNOSTICS},
};

static constexpr MenuOption BOT_DIFFICULTY_OPTIONS[] = {
    {3, 0, LedColors::Green, MenuOptionId::DIFF_1},
    {3, 1, LedColors::Lime, MenuOptionId::DIFF_2},
    {3, 2, LedColors::Yellow, MenuOptionId::DIFF_3},
    {3, 3, LedColors::Orange, MenuOptionId::DIFF_4},
    {3, 4, LedColors::Red, MenuOptionId::DIFF_5},
    {3, 5, LedColors::Crimson, MenuOptionId::DIFF_6},
    {3, 6, LedColors::Purple, MenuOptionId::DIFF_7},
    {3, 7, LedColors::Blue, MenuOptionId::DIFF_8},
};

static constexpr MenuOption BOT_COLOR_OPTIONS[] = {
    {3, 3, LedColors::White, MenuOptionId::PLAY_WHITE},
    {3, 4, LedColors::scaleColor(LedColors::White, 40.0f / 255.0f),
     MenuOptionId::PLAY_BLACK},
    {3, 5, LedColors::Yellow, MenuOptionId::PLAY_RANDOM},
};

static constexpr MenuOption CONFIRM_OPTIONS[] = {
    {4, 3, LedColors::Green, MenuOptionId::CONFIRM_YES},
    {4, 4, LedColors::Red, MenuOptionId::CONFIRM_NO},
};

#endif  // BOARD_MENUS_OPTIONS_H
