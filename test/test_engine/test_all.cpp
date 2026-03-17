// Engine library test suite entry point.

#include <unity.h>

#include "../test_helpers.h"

void setUp(void) {}
void tearDown(void) {}

// Registration functions defined in other translation units
void register_search_tests();
void register_engine_tests();

int main(int argc, char** argv) {
  UNITY_BEGIN();
  register_search_tests();
  register_engine_tests();
  return UNITY_END();
}
