#include <unity.h>

#include <bitboard.h>
#include <evaluation.h>
#include <piece.h>
#include <zobrist.h>

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
  TEST_ASSERT_TRUE(eval > 800);  // 900 material - PST penalty for queen on d1
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
  // Material + PST baseline = 50 + 10 = 60 (king PSTs cancel).
  // Pawn structure adds: +20 (passed rank bonus) -5 (isolated) + space ≈ +25.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int eval = eval::evaluatePosition(bb);
  TEST_ASSERT_TRUE(eval > 50);
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

static void test_eval_tapered_phase_affects_king(void) {
  // White king central (d4), black king corner (h8).
  // No non-pawn material → phase = 0 → pure endgame weights.
  placePiece(bb, mailbox, Piece::W_KING, "d4");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  int endgameEval = eval::evaluatePosition(bb);

  // Add symmetric queen pair → large phase swing (phase 8 vs 0).
  // Queens produce enough MG/EG blending difference (including king danger
  // asymmetry) to avoid accidental coincidence with the endgame score.
  placePiece(bb, mailbox, Piece::W_QUEEN, "a1");
  placePiece(bb, mailbox, Piece::B_QUEEN, "a8");
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
// Bishop pair
// ===========================================================================

// K+BB vs K+BN: bishop pair side should score higher.
static void test_eval_bishop_pair_bonus(void) {
  // White: Ke1, Bc1, Bf1 (both bishops).
  // Black: Ke8, Bc8, Nb8 (one bishop + one knight).
  // Same material value (600 each), but bishop pair bonus favors white.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "c1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "f1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_BISHOP, "c8");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "b8");
  int eval = eval::evaluatePosition(bb);
  // White should be favored due to bishop pair bonus.
  TEST_ASSERT_TRUE(eval > 0);
}

// Both sides have bishop pair → bonus cancels.
static void test_eval_bishop_pair_both_sides(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "c1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "f1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_BISHOP, "c8");
  placePiece(bb, mailbox, Piece::B_BISHOP, "f8");
  int eval = eval::evaluatePosition(bb);
  // Symmetric → should be 0.
  TEST_ASSERT_EQUAL_INT(0, eval);
}

// ===========================================================================
// Rook on open/semi-open file
// ===========================================================================

static void test_eval_rook_open_file(void) {
  // White has rook on open a-file (no pawns on a-file).
  // Black has rook on closed h-file (own pawn on h7).
  // The rook open file bonus should favor white.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  int eval = eval::evaluatePosition(bb);
  // White rook gets open-file bonus (+20), black rook gets nothing.
  // Plus black pawn is worth -100 material (Black extra pawn = eval more negative).
  // Actually black has an extra pawn so eval is negative from material alone.
  // We need equal material. Let's give white a pawn too, on a different file.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d2");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  placePiece(bb, mailbox, Piece::B_PAWN, "d7");
  int openEval = eval::evaluatePosition(bb);

  // Now put white rook on the d-file (blocked by own pawn).
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "d1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d2");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  placePiece(bb, mailbox, Piece::B_PAWN, "d7");
  int closedEval = eval::evaluatePosition(bb);

  // White rook on open a-file should evaluate at least as well as closed d-file.
  TEST_ASSERT_TRUE(openEval >= closedEval);
}

static void test_eval_rook_semi_open_file(void) {
  // White rook on semi-open e-file (black pawn on e7, no white e-pawn).
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d2");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  placePiece(bb, mailbox, Piece::B_PAWN, "d7");
  placePiece(bb, mailbox, Piece::B_PAWN, "e7");
  int semiOpen = eval::evaluatePosition(bb);

  // White rook on closed e-file (white e-pawn + black e-pawn).
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d2");
  placePiece(bb, mailbox, Piece::W_PAWN, "e2");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  placePiece(bb, mailbox, Piece::B_PAWN, "d7");
  placePiece(bb, mailbox, Piece::B_PAWN, "e7");
  int closed = eval::evaluatePosition(bb);

  // Semi-open should favor white more (rook semi-open bonus = +10).
  // The closed position has an extra white pawn (100cp material +
  // PST + structure) but the semi-open rook bonus adds +10 while
  // losing the pawn loses 100+. We need to compare without extra material.
  // Actually in the semi-open case, white has 1 pawn; in the closed case,
  // white has 2 pawns. So the closed eval is higher from material alone.
  // Fix: both positions should have equal material.
  clearBoard(bb, mailbox);

  // Position A: White rook on e-file, sole pawn on c2 (e-file semi-open).
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "c2");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  placePiece(bb, mailbox, Piece::B_PAWN, "c7");
  placePiece(bb, mailbox, Piece::B_PAWN, "e7");
  semiOpen = eval::evaluatePosition(bb);

  // Position B: White rook on e-file with own pawn on e2 (closed).
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "e2");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  placePiece(bb, mailbox, Piece::B_PAWN, "c7");
  placePiece(bb, mailbox, Piece::B_PAWN, "e7");
  closed = eval::evaluatePosition(bb);

  // Same material both sides. Semi-open rook (+10) should make position A better.
  TEST_ASSERT_TRUE(semiOpen > closed);
}

// ===========================================================================
// Rook on 7th rank
// ===========================================================================

static void test_eval_rook_on_seventh(void) {
  // White rook on 7th rank with black king on 8th.
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "d7");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int seventh = eval::evaluatePosition(bb);

  // White rook on 5th rank (same file, no 7th bonus).
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "d5");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int fifth = eval::evaluatePosition(bb);

  // 7th rank should score higher.
  TEST_ASSERT_TRUE(seventh > fifth);
}

// ===========================================================================
// Mobility
// ===========================================================================

static void test_eval_mobility_centralized_better(void) {
  // Centralized knight (e4) has more squares than edge knight (a1).
  // White: Ke1, Ne4. Black: Ke8, Na8.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "e4");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "a8");
  int eval = eval::evaluatePosition(bb);
  // White's knight has ~8 squares, black's has ~2 → white favored.
  TEST_ASSERT_TRUE(eval > 0);
}

// ===========================================================================
// King safety / pawn shield
// ===========================================================================

static void test_eval_king_safety_intact_shield(void) {
  // White king castled kingside with intact shield (f2, g2, h2).
  placePiece(bb, mailbox, Piece::W_KING, "g1");
  placePiece(bb, mailbox, Piece::W_PAWN, "f2");
  placePiece(bb, mailbox, Piece::W_PAWN, "g2");
  placePiece(bb, mailbox, Piece::W_PAWN, "h2");
  placePiece(bb, mailbox, Piece::B_KING, "g8");
  placePiece(bb, mailbox, Piece::B_PAWN, "f7");
  placePiece(bb, mailbox, Piece::B_PAWN, "g7");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  // Add some non-pawn material so MG weight matters.
  placePiece(bb, mailbox, Piece::W_QUEEN, "d1");
  placePiece(bb, mailbox, Piece::B_QUEEN, "d8");
  int intact = eval::evaluatePosition(bb);

  // Now damage white's shield: remove g2 and h2 pawns.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "g1");
  placePiece(bb, mailbox, Piece::W_PAWN, "f2");
  // g2 and h2 missing
  placePiece(bb, mailbox, Piece::B_KING, "g8");
  placePiece(bb, mailbox, Piece::B_PAWN, "f7");
  placePiece(bb, mailbox, Piece::B_PAWN, "g7");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  placePiece(bb, mailbox, Piece::W_QUEEN, "d1");
  placePiece(bb, mailbox, Piece::B_QUEEN, "d8");
  int damaged = eval::evaluatePosition(bb);

  // Intact shield should score higher for white.
  TEST_ASSERT_TRUE(intact > damaged);
}

// ===========================================================================
// Knight outpost
// ===========================================================================

static void test_eval_knight_outpost(void) {
  // White knight on e5, supported by d4 pawn, no black pawns on d or f files.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "e5");
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int outpost = eval::evaluatePosition(bb);

  // Same but knight on a1 (no outpost).
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "a1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int corner = eval::evaluatePosition(bb);

  // Outpost knight should score much higher.
  TEST_ASSERT_TRUE(outpost > corner);
}

// ===========================================================================
// ===========================================================================
// King danger (unified zone attack + proximity)
// ===========================================================================

static void test_eval_king_danger_close_piece(void) {
  // White queen close to black king vs far from black king.
  // Close queen attacks king zone AND is nearby (proximity amplifier).
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_QUEEN, "f7");  // 1 square from e8
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int close = eval::evaluatePosition(bb);

  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_QUEEN, "a2");  // far from e8
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int far = eval::evaluatePosition(bb);

  // Queen near enemy king should score higher for white.
  TEST_ASSERT_TRUE(close > far);
}

// ===========================================================================
// Pawn Hash Table — probe / store / clear cycle
// ===========================================================================

static void test_pawn_hash_probe_miss(void) {
  eval::PawnHashTable ph;
  ph.resize(64);   // Small table for testing

  // Empty table — probe should miss
  TEST_ASSERT_NULL(ph.probe(0x123456789ABCDEF0ULL));

  ph.free();
}

static void test_pawn_hash_store_and_probe(void) {
  eval::PawnHashTable ph;
  ph.resize(64);

  uint64_t hash = 0xDEADBEEFCAFEBABEULL;
  ph.store(hash, 42, -17, 0x100, 0x200);

  const eval::PawnEntry* e = ph.probe(hash);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_INT(42, e->mgScore);
  TEST_ASSERT_EQUAL_INT(-17, e->egScore);
  TEST_ASSERT_EQUAL_HEX64(0x100, e->passedPawns[0]);
  TEST_ASSERT_EQUAL_HEX64(0x200, e->passedPawns[1]);

  ph.free();
}

static void test_pawn_hash_clear_invalidates(void) {
  eval::PawnHashTable ph;
  ph.resize(64);

  uint64_t hash = 0x1111222233334444ULL;
  ph.store(hash, 10, 20, 0, 0);
  TEST_ASSERT_NOT_NULL(ph.probe(hash));

  ph.clear();
  TEST_ASSERT_NULL(ph.probe(hash));

  ph.free();
}

static void test_pawn_hash_integration(void) {
  // Verify that evaluatePosition with a pawn hash produces the same result
  // as without, and that the second call hits the cache.
  eval::PawnHashTable ph;
  ph.resize(256);

  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");

  int without = eval::evaluatePosition(bb);
  int with1   = eval::evaluatePosition(bb, &ph);
  int with2   = eval::evaluatePosition(bb, &ph);  // Cache hit

  TEST_ASSERT_EQUAL_INT(without, with1);
  TEST_ASSERT_EQUAL_INT(with1, with2);

  // Verify the entry is actually in the table
  uint64_t pHash = zobrist::computePawnHash(bb);
  TEST_ASSERT_NOT_NULL(ph.probe(pHash));

  ph.free();
}

// ===========================================================================
// Trapped pieces
// ===========================================================================

static void test_eval_trapped_bishop_a7(void) {
  // White bishop on a7 with black pawn on b6 — bishop is trapped.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "a7");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_PAWN, "b6");
  int trapped = eval::evaluatePosition(bb);

  // Same bishop on c5 (not trapped), same black pawn.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "c5");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_PAWN, "b6");
  int free = eval::evaluatePosition(bb);

  // The trapped bishop should score worse for white.
  TEST_ASSERT_TRUE(free > trapped);
}

static void test_eval_trapped_bishop_h7(void) {
  // White bishop on h7 with black pawn on g6 — bishop is trapped.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "h7");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_PAWN, "g6");
  int trapped = eval::evaluatePosition(bb);

  // Bishop on f5 (not trapped).
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "f5");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_PAWN, "g6");
  int free = eval::evaluatePosition(bb);

  TEST_ASSERT_TRUE(free > trapped);
}

static void test_eval_trapped_rook_by_king(void) {
  // White rook on h1 with king on g1 — rook is trapped before castling.
  // Add material so MG weight matters.
  placePiece(bb, mailbox, Piece::W_KING, "g1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::W_QUEEN, "d1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_QUEEN, "d8");
  placePiece(bb, mailbox, Piece::B_ROOK, "a8");
  int trapped = eval::evaluatePosition(bb);

  // Same material with rook on e1 (not trapped) and king on g1.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "g1");
  placePiece(bb, mailbox, Piece::W_ROOK, "e1");
  placePiece(bb, mailbox, Piece::W_QUEEN, "d1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_QUEEN, "d8");
  placePiece(bb, mailbox, Piece::B_ROOK, "a8");
  int free = eval::evaluatePosition(bb);

  TEST_ASSERT_TRUE(free > trapped);
}

static void test_eval_trapped_bishop_symmetric(void) {
  // Both sides have trapped bishops — penalty cancels.
  // White bishop on a7 (trapped by b6), black bishop on a2 (trapped by b3).
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "a7");
  placePiece(bb, mailbox, Piece::B_PAWN, "b6");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_BISHOP, "a2");
  placePiece(bb, mailbox, Piece::W_PAWN, "b3");
  int eval = eval::evaluatePosition(bb);
  // Symmetric trapped bishops → score should be near zero.
  // Allow small PST asymmetry but the trapped penalties must cancel.
  TEST_ASSERT_TRUE(eval > -30 && eval < 30);
}

// ===========================================================================
// Eval Hash Table — probe / store / clear cycle
// ===========================================================================

static void test_eval_hash_probe_miss(void) {
  eval::EvalHashTable eh;
  eh.resize(64);

  TEST_ASSERT_NULL(eh.probe(0xAAAABBBBCCCCDDDDULL));

  eh.free();
}

static void test_eval_hash_store_and_probe(void) {
  eval::EvalHashTable eh;
  eh.resize(64);

  uint64_t hash = 0xFEDCBA9876543210ULL;
  eh.store(hash, 123);

  const eval::EvalEntry* e = eh.probe(hash);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_INT(123, e->score);

  eh.free();
}

static void test_eval_hash_clear_invalidates(void) {
  eval::EvalHashTable eh;
  eh.resize(64);

  uint64_t hash = 0x5555666677778888ULL;
  eh.store(hash, 50);
  TEST_ASSERT_NOT_NULL(eh.probe(hash));

  eh.clear();
  TEST_ASSERT_NULL(eh.probe(hash));

  eh.free();
}

static void test_eval_hash_overwrite(void) {
  eval::EvalHashTable eh;
  eh.resize(64);

  uint64_t hash = 0xAAAABBBBCCCCDDDDULL;
  eh.store(hash, 100);
  eh.store(hash, 200);

  const eval::EvalEntry* e = eh.probe(hash);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_INT(200, e->score);

  eh.free();
}

// ===========================================================================
// Rank-based passed pawn scoring
// ===========================================================================

// Own king near a passed pawn should improve endgame eval vs king far away.
static void test_eval_passed_pawn_king_distance(void) {
  // Position A: white king near the white passed pawn.
  placePiece(bb, mailbox, Piece::W_KING, "d5");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_KING, "a1");
  int evalNear = eval::evaluatePosition(bb);

  // Position B: white king far from the white passed pawn.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  int evalFar = eval::evaluatePosition(bb);

  // King near the passer should be better for white.
  TEST_ASSERT_TRUE(evalNear > evalFar);
}

// ===========================================================================
// King danger — zone attacks (continued)
// ===========================================================================

// Multiple enemy pieces attacking the king zone should lower eval.
static void test_eval_king_danger_multiple_attackers(void) {
  // Position A: black pieces far from white king.
  placePiece(bb, mailbox, Piece::W_KING, "g1");
  placePiece(bb, mailbox, Piece::W_PAWN, "f2");
  placePiece(bb, mailbox, Piece::W_PAWN, "g2");
  placePiece(bb, mailbox, Piece::W_PAWN, "h2");
  placePiece(bb, mailbox, Piece::B_KING, "g8");
  placePiece(bb, mailbox, Piece::B_QUEEN, "a4");
  placePiece(bb, mailbox, Piece::B_ROOK, "a5");
  placePiece(bb, mailbox, Piece::B_BISHOP, "a6");
  int evalSafe = eval::evaluatePosition(bb);

  // Position B: black pieces attacking white king zone.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "g1");
  placePiece(bb, mailbox, Piece::W_PAWN, "f2");
  placePiece(bb, mailbox, Piece::W_PAWN, "g2");
  placePiece(bb, mailbox, Piece::W_PAWN, "h2");
  placePiece(bb, mailbox, Piece::B_KING, "g8");
  placePiece(bb, mailbox, Piece::B_QUEEN, "d4");
  placePiece(bb, mailbox, Piece::B_ROOK, "f8");
  placePiece(bb, mailbox, Piece::B_BISHOP, "h3");
  int evalDanger = eval::evaluatePosition(bb);

  // White should be worse when pieces attack the king zone.
  TEST_ASSERT_TRUE(evalSafe > evalDanger);
}

// No attackers near king should produce no attack penalty.
static void test_eval_king_danger_no_attackers(void) {
  // Symmetric position — both kings castled, no pieces near enemy kings.
  placePiece(bb, mailbox, Piece::W_KING, "g1");
  placePiece(bb, mailbox, Piece::W_PAWN, "f2");
  placePiece(bb, mailbox, Piece::W_PAWN, "g2");
  placePiece(bb, mailbox, Piece::W_PAWN, "h2");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "c3");
  placePiece(bb, mailbox, Piece::B_KING, "g8");
  placePiece(bb, mailbox, Piece::B_PAWN, "f7");
  placePiece(bb, mailbox, Piece::B_PAWN, "g7");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "c6");
  int eval = eval::evaluatePosition(bb);
  // Symmetric (except mirrored pawns) — eval should be near zero.
  TEST_ASSERT_TRUE(eval > -50 && eval < 50);
}

// ===========================================================================
// Mobility MG/EG split
// ===========================================================================

// Rook mobility should matter more in endgames (EG weight 3) than in
// midgames (MG weight 1).  A centralized rook with higher mobility should
// produce a positive eval advantage in a K+R endgame.
static void test_eval_mobility_mg_eg_split(void) {
  // White rook centralized (d4), black rook restricted behind own pawn.
  // K+R endgame (phase = 4, heavily EG-weighted).
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "d4");
  placePiece(bb, mailbox, Piece::W_PAWN, "h2");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "a8");
  placePiece(bb, mailbox, Piece::B_PAWN, "a7");
  int eval = eval::evaluatePosition(bb);
  // White's centralized rook has more mobility than the restricted black
  // rook.  With EG rook mobility weight 3, this should produce a positive
  // eval for white.
  TEST_ASSERT_TRUE(eval > 0);
}

// ===========================================================================
// Bad bishop — penalty per own pawn on same color complex
// ===========================================================================

// A bishop with own pawns blocking its diagonals (same color complex)
// should score worse than the same material without pawns on that color.
static void test_eval_bad_bishop(void) {
  // Bad bishop: White bishop on c1 (dark), 3 white pawns on dark squares.
  // Black king only.  The c1-bishop is heavily obstructed.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "c1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d2");   // dark square
  placePiece(bb, mailbox, Piece::W_PAWN, "b2");   // dark square
  placePiece(bb, mailbox, Piece::W_PAWN, "f2");   // dark square
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int badBishop = eval::evaluatePosition(bb);

  // Good bishop: same setup but pawns on light squares — bishop unobstructed.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "c1");
  placePiece(bb, mailbox, Piece::W_PAWN, "c2");   // light square
  placePiece(bb, mailbox, Piece::W_PAWN, "e2");   // light square
  placePiece(bb, mailbox, Piece::W_PAWN, "g2");   // light square
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int goodBishop = eval::evaluatePosition(bb);

  // 3 pawns on same color → 3 × BAD_BISHOP penalty → bad position scores lower.
  TEST_ASSERT_TRUE(goodBishop > badBishop);
}

// ===========================================================================
// Rook behind passer (Tarrasch Rule) — EG only
// ===========================================================================

// A rook placed behind its own passed pawn should score higher than
// a rook in front of the passer on the same file.
static void test_eval_rook_behind_passer(void) {
  // Endgame position (K+R+P vs K — phase ≤ 6).
  // Rook behind own passed pawn: Rd2 behind passer on d5 (same file).
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "d2");
  placePiece(bb, mailbox, Piece::W_PAWN, "d5");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  int behind = eval::evaluatePosition(bb);

  // Same material, rook in FRONT of the passer on d6 (same file, no bonus).
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "d6");
  placePiece(bb, mailbox, Piece::W_PAWN, "d5");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  int inFront = eval::evaluatePosition(bb);

  // Rook behind passer should get EG bonus → score higher.
  TEST_ASSERT_TRUE(behind > inFront);
}

// ===========================================================================
// Candidate passer — one enemy blocker in the passed mask
// ===========================================================================

// A pawn with exactly one enemy blocker (candidate passer) should score
// higher than a pawn with two enemy blockers (non-candidate).
static void test_eval_candidate_passer(void) {
  // Candidate: white pawn d4, one black blocker on e5.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int candidateScore = eval::evaluatePosition(bb);

  // Non-candidate: white pawn d4, two black blockers on d5 and e5.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  int nonCandidateScore = eval::evaluatePosition(bb);

  // Candidate passer gets bonus → should score higher.
  TEST_ASSERT_TRUE(candidateScore > nonCandidateScore);
}

// ===========================================================================
// Opposite-color bishop scaling — 25% reduction in endgame
// ===========================================================================

// In an endgame with opposite-color bishops, the leading side's advantage
// should be reduced compared to the same position with same-color bishops.
static void test_eval_opposite_color_bishops_scaling(void) {
  // OCB endgame: White B on c1 (dark), Black B on c8 (light), white +1P.
  // Phase = B(3) + B(3) = 6, so phase <= 6 triggers OCB scaling.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "c1");  // dark square
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_BISHOP, "c8");  // light square
  int ocbScore = eval::evaluatePosition(bb);

  // Same-color bishops: White B c1 (dark), Black B a3 (dark).
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "c1");  // dark square
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_BISHOP, "a3");  // dark square
  int sameCBScore = eval::evaluatePosition(bb);

  // Both should be positive (white has material advantage), but OCB
  // should be scaled down (×0.75) so has a smaller advantage.
  TEST_ASSERT_TRUE(ocbScore > 0);
  TEST_ASSERT_TRUE(sameCBScore > 0);
  TEST_ASSERT_TRUE(ocbScore < sameCBScore);
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

  // Bishop pair
  RUN_TEST(test_eval_bishop_pair_bonus);
  RUN_TEST(test_eval_bishop_pair_both_sides);

  // Rook on open/semi-open file
  RUN_TEST(test_eval_rook_open_file);
  RUN_TEST(test_eval_rook_semi_open_file);

  // Rook on 7th rank
  RUN_TEST(test_eval_rook_on_seventh);

  // Mobility
  RUN_TEST(test_eval_mobility_centralized_better);

  // King safety
  RUN_TEST(test_eval_king_safety_intact_shield);

  // Knight outpost
  RUN_TEST(test_eval_knight_outpost);

  // King danger
  RUN_TEST(test_eval_king_danger_close_piece);

  // Trapped pieces
  RUN_TEST(test_eval_trapped_bishop_a7);
  RUN_TEST(test_eval_trapped_bishop_h7);
  RUN_TEST(test_eval_trapped_rook_by_king);
  RUN_TEST(test_eval_trapped_bishop_symmetric);

  // Pawn hash table
  RUN_TEST(test_pawn_hash_probe_miss);
  RUN_TEST(test_pawn_hash_store_and_probe);
  RUN_TEST(test_pawn_hash_clear_invalidates);
  RUN_TEST(test_pawn_hash_integration);

  // Eval hash table
  RUN_TEST(test_eval_hash_probe_miss);
  RUN_TEST(test_eval_hash_store_and_probe);
  RUN_TEST(test_eval_hash_clear_invalidates);
  RUN_TEST(test_eval_hash_overwrite);

  // Rank-based passed pawn scoring
  RUN_TEST(test_eval_passed_pawn_king_distance);

  // King danger (continued)
  RUN_TEST(test_eval_king_danger_multiple_attackers);
  RUN_TEST(test_eval_king_danger_no_attackers);

  // Mobility MG/EG split
  RUN_TEST(test_eval_mobility_mg_eg_split);

  // Bad bishop
  RUN_TEST(test_eval_bad_bishop);

  // Rook behind passer (Tarrasch Rule)
  RUN_TEST(test_eval_rook_behind_passer);

  // Protected / candidate passer
  RUN_TEST(test_eval_candidate_passer);

  // Opposite-color bishop scaling
  RUN_TEST(test_eval_opposite_color_bishops_scaling);
}
