#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "menu.h"
#include "navigator.h"

// ---------------------------
// Menu Item IDs
// ---------------------------
// Distinct ranges per menu level for easy routing in handleMenuResult().
namespace MenuId {
  // Game selection (root)
  constexpr int8_t CHESS_MOVES = 0;
  constexpr int8_t BOT         = 1;
  constexpr int8_t LICHESS     = 2;
  constexpr int8_t BOARD_DIAGNOSTICS = 3;

  // Bot difficulty (1-based level, offset by 10)
  constexpr int8_t DIFF_1 = 10; // Beginner
  constexpr int8_t DIFF_2 = 11; // Easy
  constexpr int8_t DIFF_3 = 12; // Intermediate
  constexpr int8_t DIFF_4 = 13; // Medium
  constexpr int8_t DIFF_5 = 14; // Advanced
  constexpr int8_t DIFF_6 = 15; // Hard
  constexpr int8_t DIFF_7 = 16; // Expert
  constexpr int8_t DIFF_8 = 17; // Master

  // Bot color
  constexpr int8_t PLAY_WHITE  = 20;
  constexpr int8_t PLAY_BLACK  = 21;
  constexpr int8_t PLAY_RANDOM = 22;
}

// ---------------------------
// Menu Item Layouts
// ---------------------------
// Coordinates are in white-side orientation (row 7 = rank 1).
// All arrays are constexpr — stored in flash, zero RAM cost.

static constexpr MenuItem gameMenuItems[] = {
  {3, 3, LedColors::Blue,   MenuId::CHESS_MOVES},       // Chess Moves (Human vs Human)
  {3, 4, LedColors::Green,  MenuId::BOT},                // Chess Bot (Human vs AI)
  {4, 3, LedColors::Yellow, MenuId::LICHESS},            // Lichess (Online play)
  {4, 4, LedColors::Red,    MenuId::BOARD_DIAGNOSTICS},  // Sensor Test
};

static constexpr MenuItem botDifficultyItems[] = {
    {3, 0, LedColors::Green,   MenuId::DIFF_1}, // Beginner
    {3, 1, LedColors::Lime,    MenuId::DIFF_2}, // Easy
    {3, 2, LedColors::Yellow,  MenuId::DIFF_3}, // Intermediate
    {3, 3, LedColors::Orange,  MenuId::DIFF_4}, // Medium
    {3, 4, LedColors::Red,     MenuId::DIFF_5}, // Advanced
    {3, 5, LedColors::Crimson, MenuId::DIFF_6}, // Hard
    {3, 6, LedColors::Purple,  MenuId::DIFF_7}, // Expert
    {3, 7, LedColors::Blue,    MenuId::DIFF_8}, // Master
};

static constexpr MenuItem botColorItems[] = {
    {3, 3, LedColors::White,  MenuId::PLAY_WHITE},
  {3, 4, LedColors::scaleColor(LedColors::White, 40.0f / 255.0f), MenuId::PLAY_BLACK},  // Scaled white = black side
    {3, 5, LedColors::Yellow, MenuId::PLAY_RANDOM},
};

// ---------------------------
// Menu Instances & Navigator
// ---------------------------
// Declared extern here, defined in config.cpp.
// Menus are file-scoped statics with no heap allocation.

extern BoardMenu gameMenu;
extern BoardMenu botDifficultyMenu;
extern BoardMenu botColorMenu;
extern MenuNavigator navigator;

/// Initialize all menus (set items, back buttons). Call once in setup().
void initMenus(Board* board);

#endif // BOARD_CONFIG_H
