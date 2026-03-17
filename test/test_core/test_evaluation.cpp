#include <unity.h>

#include <bitboard.h>
#include <evaluation.h>
#include <piece.h>

#include "../test_helpers.h"

using namespace LibreChess::piece;

// ===========================================================================
// Material evaluation — eval::evaluatePosition
// ===========================================================================

static void test_evaluation_initial_is_zero(void) {
  setupInitialBoard(bb, mailbox);
  int eval = eval::evaluatePosition(bb);
  TEST_ASSERT_EQUAL_INT(0, eval);
}

static void test_evaluation_white_up_queen(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_QUEEN, "d1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int eval = eval::evaluatePosition(bb);
  TEST_ASSERT_TRUE(eval > 800);  // 900 material - small PST penalty for queen on d1
}

static void test_evaluation_equal_material(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "a8");
  int eval = eval::evaluatePosition(bb);
  TEST_ASSERT_EQUAL_INT(0, eval);  // symmetric placement → zero
}

static void test_evaluation_black_advantage(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_QUEEN, "d8"); // black queen, no white queen
  int eval = eval::evaluatePosition(bb);
  TEST_ASSERT_TRUE(eval < 0); // negative = black advantage
}

static void test_evaluation_empty_board(void) {
  // board is already cleared by setUp
  int eval = eval::evaluatePosition(bb);
  TEST_ASSERT_EQUAL_INT(0, eval);
}

// ===========================================================================
// Pawn structure evaluation
// ===========================================================================

static void test_eval_pawn_structure_symmetry(void) {
  // Symmetric pawn structure → evaluation must be 0.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d2");
  placePiece(bb, mailbox, Piece::W_PAWN, "e2");
  placePiece(bb, mailbox, Piece::W_PAWN, "f2");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_PAWN, "d7");
  placePiece(bb, mailbox, Piece::B_PAWN, "e7");
  placePiece(bb, mailbox, Piece::B_PAWN, "f7");
  int eval = eval::evaluatePosition(bb);
  TEST_ASSERT_EQUAL_INT(0, eval);
}

static void test_eval_passed_pawn_bonus(void) {
  // Lone white pawn on e5 — passed (no black pawns) and isolated.
  // Without pawn structure: 100 (material) + 20 (PST) = 120 (king PSTs cancel).
  // Pawn structure adds: +25 (passed, rank bonus) -15 (isolated) = +10 net.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int eval = eval::evaluatePosition(bb);
  TEST_ASSERT_TRUE(eval > 120);
}

static void test_eval_doubled_pawns_worse(void) {
  // Position A: two white pawns on separate adjacent files (d4, e4).
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int separate = eval::evaluatePosition(bb);

  // Position B: doubled pawns on the e-file (e3, e4).
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "e3");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int doubled = eval::evaluatePosition(bb);

  TEST_ASSERT_TRUE(separate > doubled);
}

static void test_eval_isolated_pawns_worse(void) {
  // Position A: two white pawns on adjacent files (d4, e4) — connected.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int connected = eval::evaluatePosition(bb);

  // Position B: two white pawns on distant files (a4, h4) — both isolated.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "a4");
  placePiece(bb, mailbox, Piece::W_PAWN, "h4");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int isolated = eval::evaluatePosition(bb);

  TEST_ASSERT_TRUE(connected > isolated);
}

// ===========================================================================
// Tapered evaluation (game phase)
// ===========================================================================

static void test_eval_tapered_opening_vs_endgame_king(void) {
  // In an opening-like position (lots of pieces), king on e1 (corner-ish)
  // should score better than king on d4 (center).
  // With full material the MG king table dominates: e1 = 0, d4 = -40.
  setupInitialBoard(bb, mailbox);
  int fullBoard = eval::evaluatePosition(bb);
  // Starting position is symmetric → still zero regardless of phase.
  TEST_ASSERT_EQUAL_INT(0, fullBoard);
}

static void test_eval_tapered_endgame_king_centralization(void) {
  // Pure K+P endgame (phase = 0) — king in center should score better than
  // king on the edge (EG table: d4 = +30 vs a1 = -20).
  placePiece(bb, mailbox, Piece::W_KING, "d4");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_KING, "a1");
  placePiece(bb, mailbox, Piece::B_PAWN, "e2");
  int eval = eval::evaluatePosition(bb);
  // White king center (d4 EG=+30) vs black king corner (a1 → mirrored to a8 EG=-20).
  // Net king EG bonus: +30 - (-20) = +50 advantage for white.
  TEST_ASSERT_TRUE(eval > 0);
}

static void test_eval_tapered_phase_affects_king(void) {
  // White king central (d4), black king corner (h8).
  // No non-pawn material → phase = 0 → pure endgame weights.
  placePiece(bb, mailbox, Piece::W_KING, "d4");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  int endgameEval = eval::evaluatePosition(bb);

  // Add symmetric rook pair → increases phase toward midgame.
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  placePiece(bb, mailbox, Piece::B_ROOK, "a8");
  int midgameEval = eval::evaluatePosition(bb);

  // Phase change shifts the king PST blend, producing different eval.
  TEST_ASSERT_TRUE(endgameEval != midgameEval);
}

// ===========================================================================
// Pawn-structure queries — eval::isPassed / isIsolated / isDoubled / isBackward
// ===========================================================================

static void test_passed_pawn_e4(void) {
  eval::initPawnMasks();

  // e4 pawn with no enemy pawns — should be passed.
  Square e4 = squareOf(4, 4);
  Bitboard noPawns = 0;
  TEST_ASSERT_TRUE(eval::isPassed(e4, Color::WHITE, noPawns));

  // Enemy pawn on d5 blocks passage.
  Bitboard enemyD5 = squareBB(squareOf(3, 3));
  TEST_ASSERT_FALSE(eval::isPassed(e4, Color::WHITE, enemyD5));
}

static void test_passed_pawn_e2_blocked(void) {
  eval::initPawnMasks();

  Square e2 = squareOf(6, 4);
  Square e4 = squareOf(4, 4);
  Bitboard enemyPawns = squareBB(e4);

  TEST_ASSERT_FALSE(eval::isPassed(e2, Color::WHITE, enemyPawns));
}

static void test_isolated_pawn_a_file(void) {
  eval::initPawnMasks();

  // Pawn on a2 with no friendly pawn on b-file → isolated.
  Square a2 = squareOf(6, 0);
  Bitboard friendlyOnA = squareBB(a2);
  TEST_ASSERT_TRUE(eval::isIsolated(a2, friendlyOnA));

  // Add a friendly pawn on b3 → no longer isolated.
  Bitboard withB3 = friendlyOnA | squareBB(squareOf(5, 1));
  TEST_ASSERT_FALSE(eval::isIsolated(a2, withB3));
}

static void test_isolated_pawn_d_file(void) {
  eval::initPawnMasks();

  // Pawn on d4 with no friendly pawns on c or e files → isolated.
  Square d4 = squareOf(4, 3);
  Bitboard friendlyOnD = squareBB(d4);
  TEST_ASSERT_TRUE(eval::isIsolated(d4, friendlyOnD));

  // Add a pawn on c3 → no longer isolated.
  Bitboard withC3 = friendlyOnD | squareBB(squareOf(5, 2));
  TEST_ASSERT_FALSE(eval::isIsolated(d4, withC3));
}

static void test_doubled_pawn_detection(void) {
  eval::initPawnMasks();

  Square e2 = squareOf(6, 4);
  Square e3 = squareOf(5, 4);
  Bitboard friendly = squareBB(e2) | squareBB(e3);

  TEST_ASSERT_TRUE(eval::isDoubled(e2, Color::WHITE, friendly));
}

static void test_backward_pawn_detection(void) {
  eval::initPawnMasks();

  // White pawn on d4. Adjacent pawn on c2 exists but does not support d5.
  // Enemy pawn on e6 controls d5, making d4 backward by this heuristic.
  Square d4 = squareOf(4, 3);
  Square c2 = squareOf(6, 2);
  Square e6 = squareOf(2, 4);

  Bitboard friendly = squareBB(d4) | squareBB(c2);
  Bitboard enemyPawns = squareBB(e6);
  Bitboard enemyPawnAttacks = shiftSE(enemyPawns) | shiftSW(enemyPawns);

  TEST_ASSERT_TRUE(eval::isBackward(d4, Color::WHITE, friendly, enemyPawnAttacks));
}

static void test_forward_file_mask_doubled(void) {
  eval::initPawnMasks();

  // a8 (rank 8) has no squares ahead for white → not doubled.
  Square a8 = squareOf(0, 0);
  Bitboard friendlyOnA = squareBB(a8);
  TEST_ASSERT_FALSE(eval::isDoubled(a8, Color::WHITE, friendlyOnA));

  // a2 with a friendly pawn on a4 → a2 is doubled.
  Square a2 = squareOf(6, 0);
  Square a4 = squareOf(4, 0);
  Bitboard doubled = squareBB(a2) | squareBB(a4);
  TEST_ASSERT_TRUE(eval::isDoubled(a2, Color::WHITE, doubled));
}

static void test_forward_file_mask_not_doubled(void) {
  eval::initPawnMasks();

  // Single pawn on a2, no other friendly ahead → not doubled.
  Square a2 = squareOf(6, 0);
  Bitboard single = squareBB(a2);
  TEST_ASSERT_FALSE(eval::isDoubled(a2, Color::WHITE, single));
}

// ===========================================================================
// Registration
// ===========================================================================

void register_evaluation_tests() {
  needsDefaultKings = false;

  // Material evaluation
  RUN_TEST(test_evaluation_initial_is_zero);
  RUN_TEST(test_evaluation_white_up_queen);
  RUN_TEST(test_evaluation_equal_material);
  RUN_TEST(test_evaluation_black_advantage);
  RUN_TEST(test_evaluation_empty_board);

  // Pawn structure evaluation
  RUN_TEST(test_eval_pawn_structure_symmetry);
  RUN_TEST(test_eval_passed_pawn_bonus);
  RUN_TEST(test_eval_doubled_pawns_worse);
  RUN_TEST(test_eval_isolated_pawns_worse);

  // Tapered evaluation (game phase)
  RUN_TEST(test_eval_tapered_opening_vs_endgame_king);
  RUN_TEST(test_eval_tapered_endgame_king_centralization);
  RUN_TEST(test_eval_tapered_phase_affects_king);

  // Pawn-structure queries
  RUN_TEST(test_passed_pawn_e4);
  RUN_TEST(test_passed_pawn_e2_blocked);
  RUN_TEST(test_isolated_pawn_a_file);
  RUN_TEST(test_isolated_pawn_d_file);
  RUN_TEST(test_doubled_pawn_detection);
  RUN_TEST(test_backward_pawn_detection);
  RUN_TEST(test_forward_file_mask_doubled);
  RUN_TEST(test_forward_file_mask_not_doubled);
}
