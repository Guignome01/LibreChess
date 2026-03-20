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
void register_attacks_tests();
void register_bitboard_tests();
void register_epd_tests();
void register_eval_regression_tests();
void register_evaluation_tests();
void register_fen_tests();
void register_iterator_tests();
void register_movegen_tests();
void register_notation_tests();
void register_piece_tests();
void register_position_tests();
void register_rules_tests();
void register_utils_tests();
void register_zobrist_tests();

int main(int argc, char** argv) {
  UNITY_BEGIN();
  register_bitboard_tests();
  register_piece_tests();
  register_utils_tests();
  register_attacks_tests();
  register_movegen_tests();
  register_rules_tests();
  register_epd_tests();
  register_eval_regression_tests();
  register_evaluation_tests();
  register_fen_tests();
  register_notation_tests();
  register_iterator_tests();
  register_zobrist_tests();
  register_position_tests();
  return UNITY_END();
}
