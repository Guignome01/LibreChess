#include <unity.h>

#include "../test_helpers.h"
#include <fen.h>

// ---------------------------------------------------------------------------
// FEN round-trip
// ---------------------------------------------------------------------------

void test_fen_initial_position_roundtrip(void) {
  setupInitialBoard(bb, mailbox);
  Color turn = Color::WHITE;
  PositionState state{0x0F, SQ_NONE, 0, 1};

  std::string fen = fen::boardToFEN(mailbox, turn, &state);

  // Parse back
  BitboardSet bb2;
  Piece mailbox2[64];
  Color turn2;
  PositionState state2;
  fen::fenToBoard(fen, bb2, mailbox2, turn2, &state2);

  TEST_ASSERT_EQUAL_MEMORY(mailbox, mailbox2, 64);
  TEST_ASSERT_ENUM_EQ(Color::WHITE, turn2);
  TEST_ASSERT_EQUAL_UINT8(0x0F, state2.castlingRights);
}

void test_fen_standard_initial_string(void) {
  setupInitialBoard(bb, mailbox);
  PositionState state{0x0F, SQ_NONE, 0, 1};
  std::string fen = fen::boardToFEN(mailbox, Color::WHITE, &state);
  TEST_ASSERT_EQUAL_STRING("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", fen.c_str());
}

void test_fen_custom_position_roundtrip(void) {
  std::string inputFen = "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1";
  Color turn;
  PositionState state;
  fen::fenToBoard(inputFen, bb, mailbox, turn, &state);

  std::string outputFen = fen::boardToFEN(mailbox, turn, &state);
  TEST_ASSERT_EQUAL_STRING(inputFen.c_str(), outputFen.c_str());
}

void test_fen_en_passant_target(void) {
  std::string inputFen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
  Color turn;
  PositionState state;
  fen::fenToBoard(inputFen, bb, mailbox, turn, &state);

  TEST_ASSERT_TRUE(state.epSquare != SQ_NONE);
  TEST_ASSERT_EQUAL_INT(squareOf(5, 4), state.epSquare); // e3 = row 5, col 4

  std::string outputFen = fen::boardToFEN(mailbox, turn, &state);
  TEST_ASSERT_EQUAL_STRING(inputFen.c_str(), outputFen.c_str());
}

void test_fen_no_castling_rights(void) {
  std::string inputFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1";
  Color turn;
  PositionState state;
  fen::fenToBoard(inputFen, bb, mailbox, turn, &state);
  TEST_ASSERT_EQUAL_UINT8(0x00, state.castlingRights);
}

void test_fen_halfmove_and_fullmove(void) {
  std::string inputFen = "8/8/8/8/8/8/8/4K3 w - - 42 100";
  Color turn;
  PositionState state;
  fen::fenToBoard(inputFen, bb, mailbox, turn, &state);
  TEST_ASSERT_EQUAL_INT(42, state.halfmoveClock);
  TEST_ASSERT_EQUAL_INT(100, state.fullmoveClock);
}

void test_boardToFEN_nullptr_state(void) {
  setupInitialBoard(bb, mailbox);
  std::string fen = fen::boardToFEN(mailbox, Color::WHITE, nullptr);
  // With nullptr state, should use defaults: KQkq, -, 0, 1
  TEST_ASSERT_EQUAL_STRING("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", fen.c_str());
}

void test_fen_partial_castling_rights_roundtrip(void) {
  std::string inputFen = "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w Kk - 0 1";
  Color turn;
  PositionState state;
  fen::fenToBoard(inputFen, bb, mailbox, turn, &state);
  // Kk = white kingside + black kingside = 0x01 | 0x04 = 0x05
  TEST_ASSERT_EQUAL_UINT8(0x05, state.castlingRights);
  std::string outputFen = fen::boardToFEN(mailbox, turn, &state);
  TEST_ASSERT_EQUAL_STRING(inputFen.c_str(), outputFen.c_str());
}

// ---------------------------------------------------------------------------
// validateFEN
// ---------------------------------------------------------------------------

void test_validateFEN_standard_initial(void) {
  TEST_ASSERT_TRUE(fen::validateFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
}

void test_validateFEN_custom_position(void) {
  TEST_ASSERT_TRUE(fen::validateFEN("r3k2r/pppppppp/8/8/4P3/8/PPPP1PPP/R3K2R b Kq e3 0 1"));
}

void test_validateFEN_board_only(void) {
  TEST_ASSERT_TRUE(fen::validateFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR"));
}

void test_validateFEN_empty(void) {
  TEST_ASSERT_FALSE(fen::validateFEN(""));
}

void test_validateFEN_bad_rank_count(void) {
  // Only 7 ranks (6 slashes)
  TEST_ASSERT_FALSE(fen::validateFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1"));
}

void test_validateFEN_bad_rank_sum(void) {
  // First rank sums to 9
  TEST_ASSERT_FALSE(fen::validateFEN("rnbqkbnr1/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
}

void test_validateFEN_bad_piece_char(void) {
  // 'x' is not a valid piece
  TEST_ASSERT_FALSE(fen::validateFEN("xnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
}

void test_validateFEN_bad_turn(void) {
  TEST_ASSERT_FALSE(fen::validateFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1"));
}

void test_validateFEN_bad_castling_field(void) {
  TEST_ASSERT_FALSE(fen::validateFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w XYZ - 0 1"));
}

void test_validateFEN_bad_en_passant(void) {
  // e4 is not a valid EP square (must be rank 3 or 6)
  TEST_ASSERT_FALSE(fen::validateFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e4 0 1"));
}

void test_validateFEN_bad_halfmove(void) {
  TEST_ASSERT_FALSE(fen::validateFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - abc 1"));
}

void test_validateFEN_bad_fullmove(void) {
  TEST_ASSERT_FALSE(fen::validateFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0"));
}

void test_validateFEN_no_castling_no_ep(void) {
  TEST_ASSERT_TRUE(fen::validateFEN("8/8/8/8/8/8/8/4K3 w - - 0 1"));
}

// ---------------------------------------------------------------------------
// Edge cases (Phase 4J)
// ---------------------------------------------------------------------------

void test_fen_black_to_move(void) {
  std::string inputFen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
  Color turn;
  PositionState state;
  fen::fenToBoard(inputFen, bb, mailbox, turn, &state);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, turn);
}

void test_fen_complex_midgame_roundtrip(void) {
  // Mid-game position with partial castling, en passant, and non-trivial clocks
  std::string inputFen = "r1bq1rk1/pp2ppbp/2np1np1/8/2BPP3/2N2N2/PP3PPP/R1BQ1RK1 b - - 4 8";
  Color turn;
  PositionState state;
  fen::fenToBoard(inputFen, bb, mailbox, turn, &state);

  TEST_ASSERT_ENUM_EQ(Color::BLACK, turn);
  TEST_ASSERT_EQUAL_UINT8(0x00, state.castlingRights);
  TEST_ASSERT_EQUAL_INT(4, state.halfmoveClock);
  TEST_ASSERT_EQUAL_INT(8, state.fullmoveClock);

  std::string outputFen = fen::boardToFEN(mailbox, turn, &state);
  TEST_ASSERT_EQUAL_STRING(inputFen.c_str(), outputFen.c_str());
}

void test_fenToBoard_lenient_accepts_board_only(void) {
  // fenToBoard should accept a FEN with only the board placement (no state fields)
  BitboardSet bb2;
  Piece mailbox2[64];
  Color turn;
  PositionState state;
  fen::fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR", bb2, mailbox2, turn, &state);
  // Should parse the board without crashing — verify a known piece
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, mailbox2[squareOf(7, 0)]);
  TEST_ASSERT_ENUM_EQ(Piece::B_KING, mailbox2[squareOf(0, 4)]);
}

void register_fen_tests() {
  needsDefaultKings = false;

  // FEN round-trip
  RUN_TEST(test_fen_initial_position_roundtrip);
  RUN_TEST(test_fen_standard_initial_string);
  RUN_TEST(test_fen_custom_position_roundtrip);
  RUN_TEST(test_fen_en_passant_target);
  RUN_TEST(test_fen_no_castling_rights);
  RUN_TEST(test_fen_halfmove_and_fullmove);
  RUN_TEST(test_boardToFEN_nullptr_state);
  RUN_TEST(test_fen_partial_castling_rights_roundtrip);

  // validateFEN
  RUN_TEST(test_validateFEN_standard_initial);
  RUN_TEST(test_validateFEN_custom_position);
  RUN_TEST(test_validateFEN_board_only);
  RUN_TEST(test_validateFEN_empty);
  RUN_TEST(test_validateFEN_bad_rank_count);
  RUN_TEST(test_validateFEN_bad_rank_sum);
  RUN_TEST(test_validateFEN_bad_piece_char);
  RUN_TEST(test_validateFEN_bad_turn);
  RUN_TEST(test_validateFEN_bad_castling_field);
  RUN_TEST(test_validateFEN_bad_en_passant);
  RUN_TEST(test_validateFEN_bad_halfmove);
  RUN_TEST(test_validateFEN_bad_fullmove);
  RUN_TEST(test_validateFEN_no_castling_no_ep);

  // Edge cases (Phase 4J)
  RUN_TEST(test_fen_black_to_move);
  RUN_TEST(test_fen_complex_midgame_roundtrip);
  RUN_TEST(test_fenToBoard_lenient_accepts_board_only);
}
