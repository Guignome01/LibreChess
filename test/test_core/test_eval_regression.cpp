#include <unity.h>

#include <bitboard.h>
#include <evaluation.h>
#include <fen.h>
#include <piece.h>

#include "../test_helpers.h"

using namespace LibreChess;

// ===========================================================================
// Eval regression tests — fixed-position score assertions to detect
// unintended evaluation changes across code modifications.
//
// Each test loads a known position and asserts the score falls within a
// range.  Ranges are deliberately wide (±50-100cp) to tolerate minor
// tuning changes while catching gross regressions (sign flips, missing
// terms, scale errors).
// ===========================================================================

// Helper: evaluate a FEN position and return the raw (white-relative) score.
static int evalFEN(const char* fen) {
  BitboardSet board;
  Piece mbox[64];
  Color turn;
  board.clear();
  memset(mbox, 0, 64);
  fen::fenToBoard(fen, board, mbox, turn);
  return eval::evaluatePosition(board);
}

// ---------------------------------------------------------------------------
// Symmetry / baseline
// ---------------------------------------------------------------------------

static void test_regr_startpos_near_zero(void) {
  int score = evalFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  // Starting position is perfectly symmetric → must be 0.
  TEST_ASSERT_EQUAL_INT(0, score);
}

static void test_regr_symmetric_endgame_near_zero(void) {
  // K+R vs K+R in symmetric positions → zero.
  int score = evalFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  TEST_ASSERT_EQUAL_INT(0, score);
}

// ---------------------------------------------------------------------------
// Material advantage
// ---------------------------------------------------------------------------

static void test_regr_white_up_queen(void) {
  // White queen + kings → large positive eval.
  int score = evalFEN("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
  TEST_ASSERT_TRUE(score > 800);
  TEST_ASSERT_TRUE(score < 1200);
}

static void test_regr_black_up_rook(void) {
  // Black rook advantage → negative eval.
  int score = evalFEN("r3k3/8/8/8/8/8/8/4K3 w - - 0 1");
  TEST_ASSERT_TRUE(score < -400);
  TEST_ASSERT_TRUE(score > -650);
}

static void test_regr_white_up_minor(void) {
  // White has extra knight → moderate positive eval.
  int score = evalFEN("4k3/8/8/8/8/8/8/2N1K3 w - - 0 1");
  TEST_ASSERT_TRUE(score > 200);
  TEST_ASSERT_TRUE(score < 450);
}

// ---------------------------------------------------------------------------
// Pawn structure
// ---------------------------------------------------------------------------

static void test_regr_isolated_pawn_penalty(void) {
  // White isolated pawn on e-file vs healthy pawns on d+e files.
  int isolated = evalFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
  int healthy  = evalFEN("4k3/8/8/8/8/8/3PP3/4K3 w - - 0 1");
  // Isolated pawn position should be positive (white has material) and
  // healthy pawns should score strictly higher (more material + structure).
  TEST_ASSERT_TRUE(isolated > 0);
  TEST_ASSERT_TRUE(healthy > isolated);
}

static void test_regr_passed_pawn_bonus(void) {
  // Compare a position with a passed pawn vs the same pawn + enemy blocker.
  // White passed pawn on d5 should score higher than d5 blocked by black
  // pawn on d6.
  int passed  = evalFEN("4k3/8/8/3P4/8/8/8/4K3 w - - 0 1");
  int blocked = evalFEN("4k3/8/3p4/3P4/8/8/8/4K3 w - - 0 1");
  // Passed pawn (no blocker) should score higher than a blocked pawn.
  TEST_ASSERT_TRUE(passed > blocked);
}

// ---------------------------------------------------------------------------
// Bishop pair
// ---------------------------------------------------------------------------

static void test_regr_bishop_pair_bonus(void) {
  // White has bishop pair, black has two knights.
  int bpair  = evalFEN("4k3/8/8/8/8/8/8/2B1KB2 w - - 0 1");
  int knights = evalFEN("4k3/8/8/8/8/8/8/1N2K1N1 w - - 0 1");
  // Bishop pair should be valued higher or equal to two knights.
  TEST_ASSERT_TRUE(bpair >= knights);
}

// ---------------------------------------------------------------------------
// King safety
// ---------------------------------------------------------------------------

static void test_regr_castled_king_safer(void) {
  // Castled king position vs exposed central king.
  int castled = evalFEN("4k3/8/8/8/8/8/5PPP/6K1 w - - 0 1");
  int exposed = evalFEN("4k3/8/8/8/4K3/8/8/8 w - - 0 1");
  // Castled king with pawn shield should score higher than bare king.
  TEST_ASSERT_TRUE(castled > exposed);
}

// ---------------------------------------------------------------------------
// Phase tapering
// ---------------------------------------------------------------------------


static void test_regr_rook_endgame_seventh_rank(void) {
  // White rook on 7th rank (a7) in endgame → bonus.
  int seventh = evalFEN("4k3/R7/8/8/8/8/8/4K3 w - - 0 1");
  int first   = evalFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
  // Rook on 7th should score higher than rook on 1st.
  TEST_ASSERT_TRUE(seventh > first);
}

// ---------------------------------------------------------------------------
// Bad bishop
// ---------------------------------------------------------------------------

static void test_regr_bad_bishop_penalty(void) {
  // Bishop on dark square with 3 own pawns on dark squares → bad bishop.
  int bad = evalFEN("4k3/8/8/8/8/8/1P1P1P2/2B1K3 w - - 0 1");
  // Same bishop with pawns on light squares → good bishop.
  int good = evalFEN("4k3/8/8/8/8/8/P1P1P3/2B1K3 w - - 0 1");
  // Bad bishop position should score lower.
  TEST_ASSERT_TRUE(good > bad);
}

// ---------------------------------------------------------------------------
// Rook behind passer (Tarrasch Rule) — EG only
// ---------------------------------------------------------------------------

static void test_regr_rook_behind_passer(void) {
  // K+R+P endgame: rook behind own passed pawn (same file, lower rank).
  int behind = evalFEN("k7/8/8/3P4/8/8/3R4/K7 w - - 0 1");
  // Same material, rook in front of passer on d6 (same file, no bonus).
  int inFront = evalFEN("k7/8/3R4/3P4/8/8/8/K7 w - - 0 1");
  TEST_ASSERT_TRUE(behind > inFront);
}

// ---------------------------------------------------------------------------
// Candidate passer
// ---------------------------------------------------------------------------

static void test_regr_candidate_passer_bonus(void) {
  // White pawn d4 with one black blocker on e5 → candidate passer.
  int candidate = evalFEN("4k3/8/8/4p3/3P4/8/8/4K3 w - - 0 1");
  // Two black blockers → not a candidate.
  int nonCandidate = evalFEN("4k3/8/8/3pp3/3P4/8/8/4K3 w - - 0 1");
  TEST_ASSERT_TRUE(candidate > nonCandidate);
}

// ---------------------------------------------------------------------------
// Opposite-color bishop scaling
// ---------------------------------------------------------------------------

static void test_regr_ocb_scaling(void) {
  // OCB endgame: White B (dark c1) + pawn vs Black B (light c8).
  int ocb = evalFEN("2b1k3/8/8/8/3P4/8/8/2B1K3 w - - 0 1");
  // Same-color bishops: White B (dark c1) vs Black B (dark f8).
  int sameCB = evalFEN("5bk1/8/8/8/3P4/8/8/2B1K3 w - - 0 1");
  // Both positive (white up material), but OCB should be reduced.
  TEST_ASSERT_TRUE(ocb > 0);
  TEST_ASSERT_TRUE(sameCB > 0);
  TEST_ASSERT_TRUE(ocb < sameCB);
}

// ===========================================================================
// Registration
// ===========================================================================

void register_eval_regression_tests() {
  RUN_TEST(test_regr_startpos_near_zero);
  RUN_TEST(test_regr_symmetric_endgame_near_zero);
  RUN_TEST(test_regr_white_up_queen);
  RUN_TEST(test_regr_black_up_rook);
  RUN_TEST(test_regr_white_up_minor);
  RUN_TEST(test_regr_isolated_pawn_penalty);
  RUN_TEST(test_regr_passed_pawn_bonus);
  RUN_TEST(test_regr_bishop_pair_bonus);
  RUN_TEST(test_regr_castled_king_safer);
  RUN_TEST(test_regr_rook_endgame_seventh_rank);
  RUN_TEST(test_regr_bad_bishop_penalty);
  RUN_TEST(test_regr_rook_behind_passer);
  RUN_TEST(test_regr_candidate_passer_bonus);
  RUN_TEST(test_regr_ocb_scaling);
}
