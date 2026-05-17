// Test runner for board-side primitives (canvas, input, scheduler/animations).
// Native-host tests; no Arduino / FreeRTOS dependency.
//
// Compilation strategy: this is the only TU that includes the production
// source files (`#include "*.cpp"`). All test_*.cpp files are header-only
// at link time so we don't get multiple-definition errors when PlatformIO
// links them together.

#include <unity.h>

// Include source units once for this test binary.
#include "board/runtime/canvas.cpp"            // NOLINT
#include "board/runtime/helpers.cpp"           // NOLINT
#include "board/runtime/input.cpp"             // NOLINT
#include "board/runtime/scheduler.cpp"         // NOLINT
#include "board/services/program/program.cpp"  // NOLINT
#include "board/services/visual/animations.cpp"         // NOLINT
#include "board/menus/game_selection.cpp"   // NOLINT
#include "board/menus/main.cpp"             // NOLINT
#include "board/menus/confirm.cpp"          // NOLINT

void setUp(void) {}
void tearDown(void) {}

// Registration functions defined per-translation-unit.
void register_canvas_tests();
void register_input_tests();
void register_animation_tests();
void register_board_type_tests();
void register_menu_tests();
void register_program_tests();

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  register_canvas_tests();
  register_input_tests();
  register_animation_tests();
  register_board_type_tests();
  register_menu_tests();
  register_program_tests();
  return UNITY_END();
}
