#ifndef BOARD_SERVICES_MENU_TYPES_H
#define BOARD_SERVICES_MENU_TYPES_H

#include "board/runtime/colors.h"

#include <stdint.h>

static constexpr int MENU_RESULT_NONE = -1;
static constexpr int MENU_RESULT_BACK = -2;
static constexpr uint8_t MENU_SELECTION_OPTION_COUNT = 16;

/// A selectable LED square in a physical board menu.
/// Coordinates are authored in white-side orientation
/// (row 7 = rank 1 = white's back rank).
struct MenuOption {
  int8_t row;
  int8_t col;
  LedRGB color;
  int8_t id;
};

#endif  // BOARD_SERVICES_MENU_TYPES_H