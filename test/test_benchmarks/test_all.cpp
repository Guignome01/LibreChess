// Benchmark suite entry point — timing + regression tests.

#include <unity.h>

#include "../test_helpers.h"

void setUp(void) {}
void tearDown(void) {}

// Registration functions defined in other translation units
void register_timing_tests();

int main(int argc, char** argv) {
  UNITY_BEGIN();
  register_timing_tests();
  return UNITY_END();
}
