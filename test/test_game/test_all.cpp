// Game library test suite entry point.

#include <unity.h>

#include "../test_helpers.h"

void setUp(void) {
  clearBoard(bb, mailbox);
  if (needsDefaultKings) {
    placePiece(bb, mailbox, Piece::W_KING, "h1");
    placePiece(bb, mailbox, Piece::B_KING, "h8");
  }
}

void tearDown(void) {}

// Registration functions defined in other translation units
void register_game_tests();
void register_history_tests();
void register_history_persistence_tests();

int main(int argc, char** argv) {
  UNITY_BEGIN();
  register_history_tests();
  register_history_persistence_tests();
  register_game_tests();
  return UNITY_END();
}
