// Test runner for board-side primitives (canvas, input, scheduler/animations).
// Native-host tests; no Arduino / FreeRTOS dependency.
//
// Compilation strategy: this is the only TU that includes the production
// source files (`#include "*.cpp"`). All test_*.cpp files are header-only
// at link time so we don't get multiple-definition errors when PlatformIO
// links them together.

#include <unity.h>

// Include source units once for this test binary.
#include "board/core/canvas.cpp"            // NOLINT
#include "board/core/input.cpp"             // NOLINT
#include "board/core/scheduler.cpp"         // NOLINT
#include "board/gui/animations.cpp"         // NOLINT

void setUp(void) {}
void tearDown(void) {}

// Registration functions defined per-translation-unit.
void register_canvas_tests();
void register_input_tests();
void register_animation_tests();

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  register_canvas_tests();
  register_input_tests();
  register_animation_tests();
  return UNITY_END();
}
