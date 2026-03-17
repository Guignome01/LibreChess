#include <unity.h>

#include "../test_helpers.h"

// ===========================================================================
// isCheck
// ===========================================================================

static void test_king_not_in_check_initial(void) {
  setupInitialBoard(bb, mailbox);
  TEST_ASSERT_FALSE(rules::isCheck(bb, Color::WHITE));
  TEST_ASSERT_FALSE(rules::isCheck(bb, Color::BLACK));
}

static void test_king_in_check_by_rook(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // rook on same file
  TEST_ASSERT_TRUE(rules::isCheck(bb, Color::WHITE));
}

static void test_king_in_check_by_bishop(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_BISHOP, "h4"); // bishop on diagonal
  TEST_ASSERT_TRUE(rules::isCheck(bb, Color::WHITE));
}

static void test_king_in_check_by_knight(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "f3"); // knight checks
  TEST_ASSERT_TRUE(rules::isCheck(bb, Color::WHITE));
}

static void test_king_in_check_by_pawn(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e4");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5"); // black pawn attacks e4 from d5
  TEST_ASSERT_TRUE(rules::isCheck(bb, Color::WHITE));
}

static void test_king_in_check_by_queen(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_QUEEN, "e8"); // queen on same file
  TEST_ASSERT_TRUE(rules::isCheck(bb, Color::WHITE));
}

static void test_king_not_in_check_blocked(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e2"); // own pawn blocks rook
  TEST_ASSERT_FALSE(rules::isCheck(bb, Color::WHITE));
}

static void test_black_king_in_check(void) {
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_ROOK, "e1"); // white rook attacks
  TEST_ASSERT_TRUE(rules::isCheck(bb, Color::BLACK));
}

// ===========================================================================
// isCheckmate
// ===========================================================================

static void test_back_rank_mate(void) {
  // Classic back-rank mate: Black king on g8, pawns on f7/g7/h7, White rook on e8
  placePiece(bb, mailbox, Piece::B_KING, "g8");
  placePiece(bb, mailbox, Piece::B_PAWN, "f7");
  placePiece(bb, mailbox, Piece::B_PAWN, "g7");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  placePiece(bb, mailbox, Piece::W_ROOK, "e8"); // delivers mate
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_TRUE(rules::isCheckmate(bb, mailbox, Color::BLACK, flags));
}

static void test_scholars_mate(void) {
  // Scholar's mate position: White queen on f7 delivers checkmate
  PositionState state;
  Color turn;
  fen::fenToBoard("r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b - - 0 1", bb, mailbox, turn, &state);
  TEST_ASSERT_TRUE(rules::isCheckmate(bb, mailbox, Color::BLACK, state));
}

static void test_not_checkmate_can_block(void) {
  // King in check but can block
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "d1"); // own rook can block/interpose
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // attacking rook
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_FALSE(rules::isCheckmate(bb, mailbox, Color::WHITE, flags));
}

static void test_not_checkmate_can_escape(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // rook checks
  // King can escape to d1, d2, f1, f2
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_FALSE(rules::isCheckmate(bb, mailbox, Color::WHITE, flags));
}

static void test_not_checkmate_can_capture_attacker(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e2"); // rook checks from e2
  // King can capture the rook (assuming no support)
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_FALSE(rules::isCheckmate(bb, mailbox, Color::WHITE, flags));
}

static void test_smothered_mate(void) {
  // Philidor's smothered mate: Kh8, Rg8, g7/h7 pawns, white Nf7#
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::B_ROOK, "g8"); // own rook blocks g8
  placePiece(bb, mailbox, Piece::B_PAWN, "g7");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "f7"); // knight checks h8, blocks via g8/g7/h7
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_TRUE(rules::isCheck(bb, Color::BLACK));
  TEST_ASSERT_TRUE(rules::isCheckmate(bb, mailbox, Color::BLACK, flags));
}

// ===========================================================================
// isStalemate
// ===========================================================================

static void test_stalemate_king_only(void) {
  // Classic stalemate: Black king on a8, White queen on b6, White king on c6
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_QUEEN, "b6");
  placePiece(bb, mailbox, Piece::W_KING, "c6");
  PositionState flags{0x00, -1, -1, 0, 1};
  // Black to move — king has no legal moves, not in check
  TEST_ASSERT_FALSE(rules::isCheck(bb, Color::BLACK));
  TEST_ASSERT_TRUE(rules::isStalemate(bb, mailbox, Color::BLACK, flags));
}

static void test_not_stalemate_has_move(void) {
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_KING, "c6");
  PositionState flags{0x00, -1, -1, 0, 1};
  // Black king can move to b8, b7, a7
  TEST_ASSERT_FALSE(rules::isStalemate(bb, mailbox, Color::BLACK, flags));
}

static void test_stalemate_with_blocked_pawns(void) {
  // Black king on a8, black pawn on a7 blocked by white pawn on a6.
  // White king on c7 controls b8, b7, c8, d8, d7.
  // Black has no legal moves: king surrounded, pawn blocked.
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_PAWN, "a7");
  placePiece(bb, mailbox, Piece::W_PAWN, "a6"); // blocks the pawn
  placePiece(bb, mailbox, Piece::W_KING, "c7"); // controls b8, b7, c8, d8, d7
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_FALSE(rules::isCheck(bb, Color::BLACK));
  TEST_ASSERT_TRUE(rules::isStalemate(bb, mailbox, Color::BLACK, flags));
}

// ===========================================================================
// Move legality (can't move into check, pins)
// ===========================================================================

static void test_king_cannot_move_into_check(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "f8"); // rook controls f-file
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("f1", tr, tc);
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, r, c, tr, tc, flags));
}

static void test_pinned_piece_cannot_move(void) {
  // Bishop pinned to its own king
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "e2"); // bishop on same file, between king and rook
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // enemy rook pins bishop
  int r, c;
  sq("e2", r, c);
  int tr, tc;
  sq("d3", tr, tc);
  PositionState flags{0x00, -1, -1, 0, 1};
  // Moving bishop exposes king to check → illegal
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, r, c, tr, tc, flags));
}

static void test_pinned_piece_can_move_along_pin(void) {
  // Rook pinned along file — can move along that file
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "e4"); // own rook on same file
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // enemy rook pins
  int r, c;
  sq("e4", r, c);
  int tr, tc;
  sq("e8", tr, tc); // capture the pinning rook
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_TRUE(movegen::isValidMove(bb, mailbox, r, c, tr, tc, flags));
}

static void test_diagonal_pin(void) {
  // Black pawn on d4 pinned to black king on g7 by white bishop on a1
  placePiece(bb, mailbox, Piece::B_KING, "g7");
  placePiece(bb, mailbox, Piece::W_KING, "a8");
  placePiece(bb, mailbox, Piece::B_PAWN, "d4");
  placePiece(bb, mailbox, Piece::W_BISHOP, "a1"); // pins d4 to g7 along diagonal
  int r, c;
  sq("d4", r, c);
  PositionState flags{0x00, -1, -1, 0, 1};
  // Pawn should have 0 legal moves (pinned diagonally, can't move along pin)
  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, flags, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

static void test_discovered_check(void) {
  // White bishop on c1 blocks white rook on a1 from checking black king on h1.
  // Move the bishop away to reveal the rook check.
  placePiece(bb, mailbox, Piece::B_KING, "h1");
  placePiece(bb, mailbox, Piece::W_KING, "a8");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1"); // rook on a1
  placePiece(bb, mailbox, Piece::W_BISHOP, "d1"); // bishop blocks rank 1
  // After bishop moves to e2 (off rank 1), rook gives check along rank 1
  // Verify the bishop CAN move (it would reveal check on opponent's king)
  int r, c;
  sq("d1", r, c);
  int tr, tc;
  sq("e2", tr, tc);
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_TRUE(movegen::isValidMove(bb, mailbox, r, c, tr, tc, flags));
}

static void test_double_check_only_king_can_move(void) {
  // Black king in double check from white rook and bishop.
  // Only king moves should be legal — no blocks or captures by other pieces.
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "e1"); // rook checks along e-file
  placePiece(bb, mailbox, Piece::W_BISHOP, "b5"); // bishop checks along b5-e8 diagonal
  placePiece(bb, mailbox, Piece::B_KNIGHT, "d6"); // black knight could theoretically block/capture

  PositionState flags{0x00, -1, -1, 0, 1};
  // King is in check
  TEST_ASSERT_TRUE(rules::isCheck(bb, Color::BLACK));
  // Knight on d6 cannot resolve double check (even though it attacks both e4 and b5)
  MoveList moves;
  int r, c;
  sq("d6", r, c);
  movegen::getPossibleMoves(bb, mailbox, r, c, flags, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

// ===========================================================================
// Pin-aware generation — checkMask and pin detection scenarios
// ===========================================================================

static void test_single_check_slider_can_block(void) {
  // White king e1 in check from black rook e8.
  // White rook on d4 (not pinned) can interpose at e4 but cannot make unrelated moves.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // checks king on e-file
  placePiece(bb, mailbox, Piece::W_ROOK, "d4"); // can block at e4
  PositionState flags{0x00, -1, -1, 0, 1};
  // Moving to e4 interposes the check — legal
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, 4, 3, 4, 4, flags));
  // Moving to d5 does not address the check — illegal
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, 4, 3, 3, 3, flags));
}

static void test_knight_check_no_blocking(void) {
  // White king e1 in check from black knight f3.
  // White bishop d4 cannot block (non-colinear) and cannot capture f3 (not on diagonal).
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "f3"); // knight check — cannot be blocked
  placePiece(bb, mailbox, Piece::W_BISHOP, "d4"); // not in position to capture f3
  PositionState flags{0x00, -1, -1, 0, 1};
  MoveList moves;
  int r, c;
  sq("d4", r, c);
  movegen::getPossibleMoves(bb, mailbox, r, c, flags, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

static void test_two_friendly_shielding_king_not_pinned(void) {
  // White king a1, white rook c1, white knight f1, black rook h1.
  // Two friendlies on rank 1 between king and enemy rook — neither is pinned.
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_ROOK, "c1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "f1");
  placePiece(bb, mailbox, Piece::B_ROOK, "h1"); // blocked by two friendlies
  PositionState flags{0x00, -1, -1, 0, 1};
  // Knight is NOT pinned — it has its 4 normal moves (d2, e3, g3, h2)
  MoveList moves;
  int r, c;
  sq("f1", r, c);
  movegen::getPossibleMoves(bb, mailbox, r, c, flags, moves);
  TEST_ASSERT_EQUAL_INT(4, moves.count);
}

static void test_ep_horizontal_pin_illegal(void) {
  // White king a5, white pawn d5, black pawn e5 (just moved), black rook h5.
  // After EP capture dxe6, both d5 and e5 are cleared — king on a5 is exposed to rook on h5.
  placePiece(bb, mailbox, Piece::W_KING, "a5");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "d5");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5"); // last moved from e7
  placePiece(bb, mailbox, Piece::B_ROOK, "h5"); // would give check after EP
  int epR, epC;
  sq("e6", epR, epC); // EP target square
  PositionState flags{0x00, epR, epC, 0, 1};
  TEST_ASSERT_FALSE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::WHITE, flags));
}

static void test_getPossibleMoves_idempotent(void) {
  // Calling getPossibleMoves twice on the same position must return identical
  // results (verifies no internal state leaks between calls).
  placePiece(bb, mailbox, Piece::W_KING, "e4");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h5"); // limits some king moves
  PositionState flags{0x00, -1, -1, 0, 1};
  int r, c;
  sq("e4", r, c);
  MoveList moves1, moves2;
  movegen::getPossibleMoves(bb, mailbox, r, c, flags, moves1);
  movegen::getPossibleMoves(bb, mailbox, r, c, flags, moves2);
  TEST_ASSERT_EQUAL_INT(moves1.count, moves2.count);
}

// ===========================================================================
// hasLegalEnPassantCapture (direct tests)
// ===========================================================================

static void test_hasLegalEnPassantCapture_true(void) {
  // White pawn on e5, black pawn on d5, EP target d6
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  int epR, epC;
  sq("d6", epR, epC);
  PositionState flags{0x00, epR, epC, 0, 1};
  TEST_ASSERT_TRUE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::WHITE, flags));
}

static void test_hasLegalEnPassantCapture_false_no_target(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  PositionState flags{0x00, -1, -1, 0, 1}; // no EP target
  TEST_ASSERT_FALSE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::WHITE, flags));
}

// ===========================================================================
// isSquareUnderAttack (direct tests)
// ===========================================================================

static void test_isSquareUnderAttack_by_pawn(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  int r, c;
  sq("e4", r, c);
  // e4 is attacked by black pawn from d5 (defending color = white → attacker = black)
  TEST_ASSERT_TRUE(attacks::isSquareUnderAttack(bb, r, c, Color::WHITE));
}

static void test_isSquareUnderAttack_by_knight(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "f3");
  int r, c;
  sq("e1", r, c);
  TEST_ASSERT_TRUE(attacks::isSquareUnderAttack(bb, r, c, Color::WHITE));
}

static void test_isSquareUnderAttack_not_attacked(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "f3");
  int r, c;
  sq("a4", r, c);
  TEST_ASSERT_FALSE(attacks::isSquareUnderAttack(bb, r, c, Color::WHITE));
}

// ===========================================================================
// isCheck — 4-param overload (explicit king position)
// ===========================================================================

static void test_isCheck_explicit_king_position(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8");
  // Using explicit king position
  TEST_ASSERT_TRUE(rules::isCheck(bb, squareOf(7, 4), Color::WHITE));
}

static void test_isCheck_explicit_wrong_position(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8");
  // Pass wrong position — king is not really at d1, so check should not be detected there
  TEST_ASSERT_FALSE(rules::isCheck(bb, squareOf(7, 3), Color::WHITE));
}

// ===========================================================================
// isValidMove — direct tests
// ===========================================================================

static void test_isValidMove_basic_valid(void) {
  setupInitialBoard(bb, mailbox);
  PositionState flags = PositionState::initial();
  // e2e4 is valid
  TEST_ASSERT_TRUE(movegen::isValidMove(bb, mailbox, 6, 4, 4, 4, flags));
}

static void test_isValidMove_illegal_destination(void) {
  setupInitialBoard(bb, mailbox);
  PositionState flags = PositionState::initial();
  // e2e5 (3 squares) is illegal for a pawn
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, 6, 4, 3, 4, flags));
}

static void test_isValidMove_empty_source(void) {
  PositionState flags{0x00, -1, -1, 0, 1};
  placePiece(bb, mailbox, Piece::W_KING, "h1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  // e4 is empty — moving from empty square is invalid
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, 4, 4, 3, 4, flags));
}

// ===========================================================================
// isDraw — direct static tests
// ===========================================================================

static void test_rules_isDraw_insufficient(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  PositionState st{0x00, -1, -1, 0, 1};
  HashHistory hh{};
  TEST_ASSERT_TRUE(rules::isDraw(bb, mailbox, Color::WHITE, st, hh));
}

static void test_rules_isDraw_fifty_move(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");  // sufficient material
  PositionState st{0x00, -1, -1, 100, 50};
  HashHistory hh{};
  TEST_ASSERT_TRUE(rules::isDraw(bb, mailbox, Color::WHITE, st, hh));
}

static void test_rules_isDraw_false(void) {
  setupInitialBoard(bb, mailbox);
  PositionState st = PositionState::initial();
  HashHistory hh{};
  TEST_ASSERT_FALSE(rules::isDraw(bb, mailbox, Color::WHITE, st, hh));
}

// ===========================================================================
// isGameOver — direct static tests
// ===========================================================================

static void test_rules_isGameOver_checkmate(void) {
  // Back rank mate
  placePiece(bb, mailbox, Piece::B_KING, "g8");
  placePiece(bb, mailbox, Piece::B_PAWN, "f7");
  placePiece(bb, mailbox, Piece::B_PAWN, "g7");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  placePiece(bb, mailbox, Piece::W_ROOK, "e8");
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  PositionState st{0x00, -1, -1, 0, 1};
  HashHistory hh{};
  char winner = ' ';
  GameResult result = rules::isGameOver(bb, mailbox, Color::BLACK, st, hh, winner);
  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, result);
  TEST_ASSERT_EQUAL_CHAR('w', winner);
}

static void test_rules_isGameOver_stalemate(void) {
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_QUEEN, "b6");
  placePiece(bb, mailbox, Piece::W_KING, "c6");
  PositionState st{0x00, -1, -1, 0, 1};
  HashHistory hh{};
  char winner = ' ';
  GameResult result = rules::isGameOver(bb, mailbox, Color::BLACK, st, hh, winner);
  TEST_ASSERT_ENUM_EQ(GameResult::STALEMATE, result);
}

static void test_rules_isGameOver_in_progress(void) {
  setupInitialBoard(bb, mailbox);
  PositionState st = PositionState::initial();
  HashHistory hh{};
  char winner = ' ';
  GameResult result = rules::isGameOver(bb, mailbox, Color::WHITE, st, hh, winner);
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, result);
}

// ===========================================================================
// isThreefoldRepetition
// ===========================================================================

static void test_rules_isThreefoldRepetition_direct(void) {
  // Fabricate a HashHistory with 3 identical hashes
  HashHistory hh{};
  hh.keys[0] = 0xABCD;
  hh.keys[1] = 0x1234;
  hh.keys[2] = 0xABCD;
  hh.keys[3] = 0x5678;
  hh.keys[4] = 0xABCD;
  hh.count = 5;
  TEST_ASSERT_TRUE(rules::isThreefoldRepetition(hh));
}

static void test_rules_isThreefoldRepetition_not_reached(void) {
  HashHistory hh{};
  hh.keys[0] = 0xABCD;
  hh.keys[1] = 0x1234;
  hh.keys[2] = 0xABCD;
  hh.count = 3;
  TEST_ASSERT_FALSE(rules::isThreefoldRepetition(hh));
}

// ===========================================================================
// Castling
// ===========================================================================

static void test_white_kingside_castle_available(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  PositionState flags{0x0F, -1, -1}; // all rights
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_white_queenside_castle_available(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  PositionState flags{0x0F, -1, -1};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("c1", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_black_kingside_castle_available(void) {
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  PositionState flags{0x0F, -1, -1};
  int r, c;
  sq("e8", r, c);
  int tr, tc;
  sq("g8", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_black_queenside_castle_available(void) {
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "a8");
  PositionState flags{0x0F, -1, -1};
  int r, c;
  sq("e8", r, c);
  int tr, tc;
  sq("c8", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_castle_blocked_by_piece(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "g1"); // knight blocks
  PositionState flags{0x0F, -1, -1};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_castle_through_check_forbidden(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::B_ROOK, "f8"); // rook controls f1 — king passes through check
  PositionState flags{0x0F, -1, -1};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, r, c, tr, tc, flags));
}

static void test_castle_while_in_check_forbidden(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // rook gives check on e-file
  PositionState flags{0x0F, -1, -1};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, r, c, tr, tc, flags));
}

static void test_castle_destination_under_attack(void) {
  // g1 attacked by black rook on g8, but f1 is safe
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::B_ROOK, "g8"); // attacks g1 (destination)
  PositionState flags{0x0F, -1, -1};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, r, c, tr, tc, flags));
}

static void test_no_castle_right_revoked(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  PositionState flags{0x00, -1, -1, 0, 1}; // no rights
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_queenside_blocked_b1(void) {
  // b1 must be empty (rook passes through it)
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "b1");
  PositionState flags{0x0F, -1, -1};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("c1", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_partial_castling_rights_kingside_only(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  PositionState flags{0x01, -1, -1}; // only white kingside (K)
  int r, c;
  sq("e1", r, c);
  int ktr, ktc, qtr, qtc;
  sq("g1", ktr, ktc);
  sq("c1", qtr, qtc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, ktr, ktc, flags));  // kingside OK
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, qtr, qtc, flags)); // queenside NO
}

// ===========================================================================
// En passant
// ===========================================================================

static void test_en_passant_white_captures(void) {
  // White pawn on e5, Black pawn just double-pushed to d5
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  int epR, epC;
  sq("d6", epR, epC);
  PositionState flags{0x0F, epR, epC}; // d6

  int r, c;
  sq("e5", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, epR, epC, flags));
}

static void test_en_passant_black_captures(void) {
  placePiece(bb, mailbox, Piece::B_PAWN, "d4");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  int epR, epC;
  sq("e3", epR, epC);
  PositionState flags{0x0F, epR, epC}; // e3

  int r, c;
  sq("d4", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, epR, epC, flags));
}

static void test_en_passant_not_available(void) {
  // Pawns adjacent but no en passant target set
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  // Default flags — no en passant

  int r, c;
  sq("e5", r, c);
  int tr, tc;
  sq("d6", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc));
}

static void test_ep_capture_leaves_king_in_check(void) {
  // Horizontal pin: white K on a5, P on d5, black p on e5 (just double-pushed),
  // black rook on h5. EP capture d5→e6 removes e5 pawn, exposes king along rank 5.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "a5");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "d5");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_ROOK, "h5");
  int epR, epC;
  sq("e6", epR, epC);
  PositionState flags{0x00, epR, epC, 0, 1};
  int r, c;
  sq("d5", r, c);
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, r, c, epR, epC, flags));
}

static void test_ep_a_file_boundary(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "a5");
  placePiece(bb, mailbox, Piece::B_PAWN, "b5");
  int epR, epC;
  sq("b6", epR, epC);
  PositionState flags{0x0F, epR, epC};
  int r, c;
  sq("a5", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, epR, epC, flags));
}

static void test_ep_h_file_boundary(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "h5");
  placePiece(bb, mailbox, Piece::B_PAWN, "g5");
  int epR, epC;
  sq("g6", epR, epC);
  PositionState flags{0x0F, epR, epC};
  int r, c;
  sq("h5", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, epR, epC, flags));
}

// ===========================================================================
// Pawn promotion detection
// ===========================================================================

static void test_white_pawn_promotes_on_rank8(void) {
  TEST_ASSERT_TRUE(piece::isPromotion(Piece::W_PAWN, 0));  // row 0 = rank 8
}

static void test_white_pawn_no_promote_other_rank(void) {
  TEST_ASSERT_FALSE(piece::isPromotion(Piece::W_PAWN, 1)); // row 1 = rank 7
}

static void test_black_pawn_promotes_on_rank1(void) {
  TEST_ASSERT_TRUE(piece::isPromotion(Piece::B_PAWN, 7));  // row 7 = rank 1
}

static void test_non_pawn_no_promote(void) {
  TEST_ASSERT_FALSE(piece::isPromotion(Piece::W_ROOK, 0));
}

static void test_isPromotion_black_not_rank1(void) {
  TEST_ASSERT_FALSE(piece::isPromotion(Piece::B_PAWN, 5)); // row 5 = rank 3
}

// ===========================================================================
// Helpers: isEnPassantMove, isCastlingMove, etc.
// ===========================================================================

static void test_isEnPassantMove_true(void) {
  // Pawn moves diagonally to empty square -> en passant
  auto info = utils::checkEnPassant(4, 4, 3, 3, Piece::W_PAWN, Piece::NONE);
  TEST_ASSERT_TRUE(info.isCapture);
}

static void test_isEnPassantMove_false_normal_capture(void) {
  // Pawn captures a piece diagonally -- not en passant
  auto info = utils::checkEnPassant(4, 4, 3, 3, Piece::W_PAWN, Piece::B_PAWN);
  TEST_ASSERT_FALSE(info.isCapture);
}

static void test_isEnPassantMove_non_pawn(void) {
  TEST_ASSERT_FALSE(utils::checkEnPassant(4, 4, 3, 3, Piece::W_BISHOP, Piece::NONE).isCapture);
}

static void test_isCastlingMove_true(void) {
  // King moves 2 squares
  TEST_ASSERT_TRUE(utils::checkCastling(7, 4, 7, 6, Piece::W_KING).isCastling);
  TEST_ASSERT_TRUE(utils::checkCastling(7, 4, 7, 2, Piece::W_KING).isCastling);
}

static void test_isCastlingMove_false(void) {
  // King moves 1 square
  TEST_ASSERT_FALSE(utils::checkCastling(7, 4, 7, 5, Piece::W_KING).isCastling);
}

static void test_isCastlingMove_non_king(void) {
  TEST_ASSERT_FALSE(utils::checkCastling(7, 0, 7, 2, Piece::W_ROOK).isCastling);
}

static void test_getEnPassantCapturedPawnRow(void) {
  // White captures en passant moving to row 2 (rank 6) -- captured pawn is on row 3
  auto info = utils::checkEnPassant(3, 0, 2, 1, Piece::W_PAWN, Piece::NONE);
  TEST_ASSERT_EQUAL_INT(3, info.capturedPawnRow);
  // Black captures en passant moving to row 5 (rank 3) -- captured pawn is on row 4
  auto info2 = utils::checkEnPassant(4, 1, 5, 0, Piece::B_PAWN, Piece::NONE);
  TEST_ASSERT_EQUAL_INT(4, info2.capturedPawnRow);
}

// ===========================================================================
// Registration
// ===========================================================================

void register_rules_tests() {
  needsDefaultKings = false;

  // ----- Check detection -----
  RUN_TEST(test_king_not_in_check_initial);
  RUN_TEST(test_king_in_check_by_rook);
  RUN_TEST(test_king_in_check_by_bishop);
  RUN_TEST(test_king_in_check_by_knight);
  RUN_TEST(test_king_in_check_by_pawn);
  RUN_TEST(test_king_in_check_by_queen);
  RUN_TEST(test_king_not_in_check_blocked);
  RUN_TEST(test_black_king_in_check);

  // ----- Checkmate -----
  RUN_TEST(test_back_rank_mate);
  RUN_TEST(test_scholars_mate);
  RUN_TEST(test_not_checkmate_can_block);
  RUN_TEST(test_not_checkmate_can_escape);
  RUN_TEST(test_not_checkmate_can_capture_attacker);
  RUN_TEST(test_smothered_mate);

  // ----- Stalemate -----
  RUN_TEST(test_stalemate_king_only);
  RUN_TEST(test_not_stalemate_has_move);
  RUN_TEST(test_stalemate_with_blocked_pawns);

  // ----- Move legality (pins, check evasion) -----
  RUN_TEST(test_king_cannot_move_into_check);
  RUN_TEST(test_pinned_piece_cannot_move);
  RUN_TEST(test_pinned_piece_can_move_along_pin);
  RUN_TEST(test_diagonal_pin);
  RUN_TEST(test_discovered_check);
  RUN_TEST(test_double_check_only_king_can_move);
  RUN_TEST(test_single_check_slider_can_block);
  RUN_TEST(test_knight_check_no_blocking);
  RUN_TEST(test_two_friendly_shielding_king_not_pinned);
  RUN_TEST(test_ep_horizontal_pin_illegal);
  RUN_TEST(test_getPossibleMoves_idempotent);

  // ----- En passant legality -----
  RUN_TEST(test_hasLegalEnPassantCapture_true);
  RUN_TEST(test_hasLegalEnPassantCapture_false_no_target);

  // ----- Square attack detection -----
  RUN_TEST(test_isSquareUnderAttack_by_pawn);
  RUN_TEST(test_isSquareUnderAttack_by_knight);
  RUN_TEST(test_isSquareUnderAttack_not_attacked);

  // ----- isCheck explicit king position -----
  RUN_TEST(test_isCheck_explicit_king_position);
  RUN_TEST(test_isCheck_explicit_wrong_position);

  // ----- isValidMove direct -----
  RUN_TEST(test_isValidMove_basic_valid);
  RUN_TEST(test_isValidMove_illegal_destination);
  RUN_TEST(test_isValidMove_empty_source);

  // ----- isDraw -----
  RUN_TEST(test_rules_isDraw_insufficient);
  RUN_TEST(test_rules_isDraw_fifty_move);
  RUN_TEST(test_rules_isDraw_false);

  // ----- isGameOver -----
  RUN_TEST(test_rules_isGameOver_checkmate);
  RUN_TEST(test_rules_isGameOver_stalemate);
  RUN_TEST(test_rules_isGameOver_in_progress);

  // ----- isThreefoldRepetition -----
  RUN_TEST(test_rules_isThreefoldRepetition_direct);
  RUN_TEST(test_rules_isThreefoldRepetition_not_reached);

  // ----- Castling (needsDefaultKings = true for the rest) -----
  needsDefaultKings = true;
  RUN_TEST(test_white_kingside_castle_available);
  RUN_TEST(test_white_queenside_castle_available);
  RUN_TEST(test_black_kingside_castle_available);
  RUN_TEST(test_black_queenside_castle_available);
  RUN_TEST(test_castle_blocked_by_piece);
  RUN_TEST(test_castle_through_check_forbidden);
  RUN_TEST(test_castle_while_in_check_forbidden);
  RUN_TEST(test_castle_destination_under_attack);
  RUN_TEST(test_no_castle_right_revoked);
  RUN_TEST(test_queenside_blocked_b1);
  RUN_TEST(test_partial_castling_rights_kingside_only);

  // ----- En passant -----
  RUN_TEST(test_en_passant_white_captures);
  RUN_TEST(test_en_passant_black_captures);
  RUN_TEST(test_en_passant_not_available);
  RUN_TEST(test_ep_capture_leaves_king_in_check);
  RUN_TEST(test_ep_a_file_boundary);
  RUN_TEST(test_ep_h_file_boundary);

  // ----- Promotion -----
  RUN_TEST(test_white_pawn_promotes_on_rank8);
  RUN_TEST(test_white_pawn_no_promote_other_rank);
  RUN_TEST(test_black_pawn_promotes_on_rank1);
  RUN_TEST(test_non_pawn_no_promote);
  RUN_TEST(test_isPromotion_black_not_rank1);

  // ----- Helpers -----
  RUN_TEST(test_isEnPassantMove_true);
  RUN_TEST(test_isEnPassantMove_false_normal_capture);
  RUN_TEST(test_isEnPassantMove_non_pawn);
  RUN_TEST(test_isCastlingMove_true);
  RUN_TEST(test_isCastlingMove_false);
  RUN_TEST(test_isCastlingMove_non_king);
  RUN_TEST(test_getEnPassantCapturedPawnRow);
}
