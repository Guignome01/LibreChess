#include <unity.h>

#include <bitboard.h>
#include <attacks.h>

#include "../test_helpers.h"

using namespace LibreChess;
using namespace LibreChess::attacks;
using namespace LibreChess::piece;

// ---------------------------------------------------------------------------
// Helper: build a Bitboard from a list of squares
// ---------------------------------------------------------------------------

static Bitboard bbFromSquares(const Square squares[], int count) {
  Bitboard bb = 0;
  for (int i = 0; i < count; ++i) bb |= squareBB(squares[i]);
  return bb;
}

// ---------------------------------------------------------------------------
// Knight attacks
// ---------------------------------------------------------------------------

static void test_knight_attacks_e4(void) {
  init();
  Square e4 = squareOf(4, 4);  // LERF: 28
  Bitboard attacks = KNIGHT[e4];

  // Knight on e4 should attack: d2, f2, c3, g3, c5, g5, d6, f6
  Square expected[] = {
    squareOf(6, 3),  // d2
    squareOf(6, 5),  // f2
    squareOf(5, 2),  // c3
    squareOf(5, 6),  // g3
    squareOf(3, 2),  // c5
    squareOf(3, 6),  // g5
    squareOf(2, 3),  // d6
    squareOf(2, 5),  // f6
  };
  TEST_ASSERT_EQUAL_UINT64(bbFromSquares(expected, 8), attacks);
  TEST_ASSERT_EQUAL_INT(8, popcount(attacks));
}

static void test_knight_attacks_a1_corner(void) {
  init();
  Bitboard attacks = KNIGHT[SQ_A1];

  // Knight on a1: only b3 and c2
  Square expected[] = {
    squareOf(5, 1),  // b3
    squareOf(6, 2),  // c2
  };
  TEST_ASSERT_EQUAL_UINT64(bbFromSquares(expected, 2), attacks);
  TEST_ASSERT_EQUAL_INT(2, popcount(attacks));
}

// ---------------------------------------------------------------------------
// King attacks
// ---------------------------------------------------------------------------

static void test_king_attacks_e4(void) {
  init();
  Square e4 = squareOf(4, 4);
  Bitboard attacks = KING[e4];

  // King on e4: d3, e3, f3, d4, f4, d5, e5, f5
  TEST_ASSERT_EQUAL_INT(8, popcount(attacks));
  // Verify a few specific squares
  TEST_ASSERT_TRUE((attacks & squareBB(squareOf(5, 3))) != 0);  // d3
  TEST_ASSERT_TRUE((attacks & squareBB(squareOf(3, 5))) != 0);  // f5
}

static void test_king_attacks_a1_corner(void) {
  init();
  Bitboard attacks = KING[SQ_A1];

  // King on a1: a2, b1, b2
  Square expected[] = {
    squareOf(6, 0),  // a2
    squareOf(7, 1),  // b1
    squareOf(6, 1),  // b2
  };
  TEST_ASSERT_EQUAL_UINT64(bbFromSquares(expected, 3), attacks);
}

// ---------------------------------------------------------------------------
// Pawn attacks
// ---------------------------------------------------------------------------

static void test_pawn_attacks_white_e4(void) {
  init();
  Square e4 = squareOf(4, 4);
  Bitboard attacks = PAWN[0][e4];  // WHITE

  // White pawn on e4 attacks d5 and f5
  Square expected[] = {
    squareOf(3, 3),  // d5
    squareOf(3, 5),  // f5
  };
  TEST_ASSERT_EQUAL_UINT64(bbFromSquares(expected, 2), attacks);
}

static void test_pawn_attacks_black_e5(void) {
  init();
  Square e5 = squareOf(3, 4);
  Bitboard attacks = PAWN[1][e5];  // BLACK

  // Black pawn on e5 attacks d4 and f4
  Square expected[] = {
    squareOf(4, 3),  // d4
    squareOf(4, 5),  // f4
  };
  TEST_ASSERT_EQUAL_UINT64(bbFromSquares(expected, 2), attacks);
}

static void test_pawn_attacks_a_file_no_wrap(void) {
  init();
  Square a4 = squareOf(4, 0);

  // White pawn on a4: only b5 (no wrap to h-file)
  Bitboard whiteAtk = PAWN[0][a4];
  TEST_ASSERT_EQUAL_INT(1, popcount(whiteAtk));
  TEST_ASSERT_TRUE((whiteAtk & squareBB(squareOf(3, 1))) != 0);  // b5
}

// ---------------------------------------------------------------------------
// Rook attacks (slider)
// ---------------------------------------------------------------------------

static void test_rook_attacks_e4_empty(void) {
  init();
  Square e4 = squareOf(4, 4);

  // Empty board: rook on e4 attacks full file e + full rank 4, minus e4 itself
  Bitboard attacks = rook(e4, 0);

  // Should have 14 squares: 7 on file + 7 on rank
  TEST_ASSERT_EQUAL_INT(14, popcount(attacks));

  // Self-square should NOT be attacked
  TEST_ASSERT_EQUAL_UINT64(0, attacks & squareBB(e4));

  // e1 and e8 should be attacked (end of file)
  TEST_ASSERT_TRUE((attacks & squareBB(squareOf(7, 4))) != 0);  // e1
  TEST_ASSERT_TRUE((attacks & squareBB(squareOf(0, 4))) != 0);  // e8

  // a4 and h4 should be attacked (end of rank)
  TEST_ASSERT_TRUE((attacks & squareBB(squareOf(4, 0))) != 0);  // a4
  TEST_ASSERT_TRUE((attacks & squareBB(squareOf(4, 7))) != 0);  // h4
}

static void test_rook_attacks_e4_with_blocker(void) {
  init();
  Square e4 = squareOf(4, 4);
  Square e7 = squareOf(1, 4);

  // Blocker on e7: rook on e4 should reach e7 but not e8
  Bitboard occupied = squareBB(e4) | squareBB(e7);
  Bitboard attacks = rook(e4, occupied);

  TEST_ASSERT_TRUE((attacks & squareBB(e7)) != 0);             // blocker included
  TEST_ASSERT_EQUAL_UINT64(0, attacks & squareBB(squareOf(0, 4)));  // e8 blocked
}

static void test_rook_attacks_a1_empty(void) {
  init();
  Bitboard attacks = rook(SQ_A1, 0);

  // Rook on a1, empty board: 7 (rank 1) + 7 (file a) = 14
  TEST_ASSERT_EQUAL_INT(14, popcount(attacks));
}

// ---------------------------------------------------------------------------
// Bishop attacks (slider)
// ---------------------------------------------------------------------------

static void test_bishop_attacks_e4_empty(void) {
  init();
  Square e4 = squareOf(4, 4);
  Bitboard attacks = bishop(e4, 0);

  // Bishop on e4, empty board: NE(3) + NW(4) + SE(3) + SW(3) = 13
  TEST_ASSERT_EQUAL_INT(13, popcount(attacks));

  // Self-square not attacked
  TEST_ASSERT_EQUAL_UINT64(0, attacks & squareBB(e4));
}

static void test_bishop_attacks_with_blocker(void) {
  init();
  Square e4 = squareOf(4, 4);
  Square g6 = squareOf(2, 6);

  // Blocker on g6: NE ray should hit f5, g6 but not h7
  Bitboard occupied = squareBB(e4) | squareBB(g6);
  Bitboard attacks = bishop(e4, occupied);

  Square f5 = squareOf(3, 5);
  Square h7 = squareOf(1, 7);

  TEST_ASSERT_TRUE((attacks & squareBB(f5)) != 0);   // f5 reached
  TEST_ASSERT_TRUE((attacks & squareBB(g6)) != 0);   // g6 (blocker) included
  TEST_ASSERT_EQUAL_UINT64(0, attacks & squareBB(h7));  // h7 blocked
}

static void test_bishop_attacks_a1_empty(void) {
  init();
  Bitboard attacks = bishop(SQ_A1, 0);

  // a1 corner: only NE diagonal — b2, c3, d4, e5, f6, g7, h8 = 7
  TEST_ASSERT_EQUAL_INT(7, popcount(attacks));
}

// ---------------------------------------------------------------------------
// Queen attacks (combined)
// ---------------------------------------------------------------------------

static void test_queen_attacks_e4_empty(void) {
  init();
  Square e4 = squareOf(4, 4);
  Bitboard attacks = queen(e4, 0);

  // Rook: 14 + Bishop: 13 = 27
  TEST_ASSERT_EQUAL_INT(27, popcount(attacks));
}

// ---------------------------------------------------------------------------
// Bulk slider correctness (reference cross-check)
// ---------------------------------------------------------------------------
// Verifies rook and bishop attacks for every square against a simple
// reference implementation. Catches edge-case issues in the optimized
// Hyperbola Quintessence / first-rank table implementation.

// Reference ray walk — the original classical loop for cross-checking.
static Bitboard refRay(Square sq, int step, Bitboard fileMask, Bitboard occupied) {
  Bitboard attacks = 0, current = squareBB(sq);
  while (true) {
    current &= fileMask;
    if (!current) break;
    if (step > 0) current <<= step; else current >>= (-step);
    if (!current) break;
    attacks |= current;
    if (current & occupied) break;
  }
  return attacks;
}

static Bitboard refRookAttacks(Square sq, Bitboard occ) {
  return refRay(sq, 8, ~0ULL, occ)     | refRay(sq, -8, ~0ULL, occ)
       | refRay(sq, 1, NOT_FILE_H, occ) | refRay(sq, -1, NOT_FILE_A, occ);
}

static Bitboard refBishopAttacks(Square sq, Bitboard occ) {
  return refRay(sq,  9, NOT_FILE_H, occ) | refRay(sq, 7, NOT_FILE_A, occ)
       | refRay(sq, -7, NOT_FILE_H, occ) | refRay(sq, -9, NOT_FILE_A, occ);
}

// Test rook attacks for all 64 squares with several occupancy patterns.
static void test_rook_attacks_bulk_correctness(void) {
  init();
  // Several occupancy patterns: empty, edges, random-ish, full
  Bitboard patterns[] = {
    0,
    RANK_1 | RANK_8 | FILE_A | FILE_H,
    0xAA55AA55AA55AA55ULL,
    0x00FF00FF00FF00FFULL,
    ~0ULL,
  };
  for (auto occ : patterns) {
    for (Square sq = 0; sq < 64; ++sq) {
      Bitboard expected = refRookAttacks(sq, occ);
      Bitboard actual = rook(sq, occ);
      TEST_ASSERT_EQUAL_UINT64(expected, actual);
    }
  }
}

// Test bishop attacks for all 64 squares with several occupancy patterns.
static void test_bishop_attacks_bulk_correctness(void) {
  init();
  Bitboard patterns[] = {
    0,
    RANK_1 | RANK_8 | FILE_A | FILE_H,
    0xAA55AA55AA55AA55ULL,
    0x00FF00FF00FF00FFULL,
    ~0ULL,
  };
  for (auto occ : patterns) {
    for (Square sq = 0; sq < 64; ++sq) {
      Bitboard expected = refBishopAttacks(sq, occ);
      Bitboard actual = bishop(sq, occ);
      TEST_ASSERT_EQUAL_UINT64(expected, actual);
    }
  }
}

// ---------------------------------------------------------------------------
// X-ray attacks
// ---------------------------------------------------------------------------

static void test_xray_rook_north_through_blocker(void) {
  // King on e1, friendly rook on e4, enemy rook on e8.
  // Regular rook attack from e1 stops at e4. X-ray passes through to e8.
  Square e1 = squareOf(7, 4), e4 = squareOf(4, 4), e8 = squareOf(0, 4);
  Bitboard occ = squareBB(e1) | squareBB(e4) | squareBB(e8);
  Bitboard friendly = squareBB(e4);

  Bitboard regular = rook(e1, occ);
  Bitboard xray = xrayRook(occ, friendly, e1);

  TEST_ASSERT_TRUE(regular & squareBB(e4));   // e4 visible normally
  TEST_ASSERT_FALSE(regular & squareBB(e8));  // e8 blocked by e4
  TEST_ASSERT_TRUE(xray & squareBB(e8));      // e8 visible through xray
}

static void test_xray_rook_no_friendly_blocker(void) {
  // With no friendly pieces, xray equals regular rook attacks.
  Square e4 = squareOf(4, 4);
  Bitboard occ = squareBB(e4);
  TEST_ASSERT_EQUAL_UINT64(rook(e4, occ), xrayRook(occ, 0, e4));
}

static void test_xray_bishop_diagonal_through_blocker(void) {
  // King on a1, friendly bishop on c3, enemy queen on e5.
  // Regular bishop attack from a1 stops at c3. X-ray passes through to e5.
  Square a1 = squareOf(7, 0), c3 = squareOf(5, 2), e5 = squareOf(3, 4);
  Bitboard occ = squareBB(a1) | squareBB(c3) | squareBB(e5);
  Bitboard friendly = squareBB(c3);

  Bitboard regular = bishop(a1, occ);
  Bitboard xray = xrayBishop(occ, friendly, a1);

  TEST_ASSERT_TRUE(regular & squareBB(c3));   // c3 visible normally
  TEST_ASSERT_FALSE(regular & squareBB(e5));  // e5 blocked by c3
  TEST_ASSERT_TRUE(xray & squareBB(e5));      // e5 visible through xray
}

// ---------------------------------------------------------------------------
// between (ray geometry)
// ---------------------------------------------------------------------------

static void test_ray_between_same_file(void) {
  Square e1 = squareOf(7, 4), e4 = squareOf(4, 4);
  Square e2 = squareOf(6, 4), e3 = squareOf(5, 4);
  Bitboard ray = between(e1, e4);
  TEST_ASSERT_TRUE(ray & squareBB(e2));
  TEST_ASSERT_TRUE(ray & squareBB(e3));
  TEST_ASSERT_FALSE(ray & squareBB(e1));  // endpoint excluded
  TEST_ASSERT_FALSE(ray & squareBB(e4));  // endpoint excluded
  TEST_ASSERT_EQUAL_INT(2, popcount(ray));
}

static void test_ray_between_same_rank(void) {
  Square a1 = squareOf(7, 0), d1 = squareOf(7, 3);
  Square b1 = squareOf(7, 1), c1 = squareOf(7, 2);
  Bitboard ray = between(a1, d1);
  TEST_ASSERT_TRUE(ray & squareBB(b1));
  TEST_ASSERT_TRUE(ray & squareBB(c1));
  TEST_ASSERT_EQUAL_INT(2, popcount(ray));
}

static void test_ray_between_same_diagonal(void) {
  Square a1 = squareOf(7, 0), c3 = squareOf(5, 2), b2 = squareOf(6, 1);
  Bitboard ray = between(a1, c3);
  TEST_ASSERT_TRUE(ray & squareBB(b2));
  TEST_ASSERT_EQUAL_INT(1, popcount(ray));
}

static void test_ray_between_anti_diagonal(void) {
  Square h1 = squareOf(7, 7), f3 = squareOf(5, 5), g2 = squareOf(6, 6);
  Bitboard ray = between(h1, f3);
  TEST_ASSERT_TRUE(ray & squareBB(g2));
  TEST_ASSERT_EQUAL_INT(1, popcount(ray));
}

static void test_ray_between_adjacent_returns_zero(void) {
  Square e1 = squareOf(7, 4), e2 = squareOf(6, 4);
  TEST_ASSERT_EQUAL_UINT64(0, between(e1, e2));
}

static void test_ray_between_not_colinear_returns_zero(void) {
  Square a1 = squareOf(7, 0), b3 = squareOf(5, 1);
  TEST_ASSERT_EQUAL_UINT64(0, between(a1, b3));
}

// ---------------------------------------------------------------------------
// line (full line geometry)
// ---------------------------------------------------------------------------

static void test_lineBB_same_rank(void) {
  Square a1 = squareOf(7, 0), h1 = squareOf(7, 7);
  TEST_ASSERT_EQUAL_UINT64(RANK_1, line(a1, h1));
}

static void test_lineBB_same_file(void) {
  Square a1 = squareOf(7, 0), a8 = squareOf(0, 0);
  TEST_ASSERT_EQUAL_UINT64(FILE_A, line(a1, a8));
}

static void test_lineBB_diagonal(void) {
  Square a1 = squareOf(7, 0), h8 = squareOf(0, 7);
  Bitboard expected = 0;
  for (int i = 0; i < 8; i++)
    expected |= squareBB(squareOf(7 - i, i));
  TEST_ASSERT_EQUAL_UINT64(expected, line(a1, h8));
}

static void test_lineBB_non_colinear(void) {
  Square a1 = squareOf(7, 0), b3 = squareOf(5, 1);
  TEST_ASSERT_EQUAL_UINT64(0, line(a1, b3));
}

static void test_lineBB_includes_endpoints(void) {
  Square c1 = squareOf(7, 2), f1 = squareOf(7, 5);
  Bitboard ln = line(c1, f1);
  TEST_ASSERT_NOT_EQUAL(0, ln & squareBB(c1));
  TEST_ASSERT_NOT_EQUAL(0, ln & squareBB(f1));
}

// ---------------------------------------------------------------------------
// computeAll (AttackInfo)
// ---------------------------------------------------------------------------

static void test_attackInfo_initial_position(void) {
  init();
  BitboardSet bb;
  Piece mailbox[64] = {};
  setupInitialBoard(bb, mailbox);

  AttackInfo info = computeAll(bb);

  Bitboard wnAtk = info.byPiece[0][raw(PieceType::KNIGHT)];
  TEST_ASSERT_NOT_EQUAL(0, wnAtk & squareBB(squareOf(5, 0)));  // a3
  TEST_ASSERT_NOT_EQUAL(0, wnAtk & squareBB(squareOf(5, 2)));  // c3
  TEST_ASSERT_NOT_EQUAL(0, wnAtk & squareBB(squareOf(5, 5)));  // f3
  TEST_ASSERT_NOT_EQUAL(0, wnAtk & squareBB(squareOf(5, 7)));  // h3

  Bitboard bnAtk = info.byPiece[1][raw(PieceType::KNIGHT)];
  TEST_ASSERT_NOT_EQUAL(0, bnAtk & squareBB(squareOf(2, 0)));  // a6
  TEST_ASSERT_NOT_EQUAL(0, bnAtk & squareBB(squareOf(2, 2)));  // c6
  TEST_ASSERT_NOT_EQUAL(0, bnAtk & squareBB(squareOf(2, 5)));  // f6
  TEST_ASSERT_NOT_EQUAL(0, bnAtk & squareBB(squareOf(2, 7)));  // h6
}

static void test_attackInfo_pawn_attacks_bulk(void) {
  init();
  BitboardSet bb;
  Piece mailbox[64] = {};
  setupInitialBoard(bb, mailbox);

  AttackInfo info = computeAll(bb);
  Bitboard wpAtk = info.byPiece[0][raw(PieceType::PAWN)];

  for (int col = 0; col < 8; ++col)
    TEST_ASSERT_NOT_EQUAL(0, wpAtk & squareBB(squareOf(5, col)));

  TEST_ASSERT_EQUAL_UINT64(0, wpAtk & RANK_4);
  TEST_ASSERT_EQUAL_UINT64(0, wpAtk & RANK_2);
}

static void test_attackInfo_color_union(void) {
  init();
  BitboardSet bb;
  Piece mailbox[64] = {};
  setupInitialBoard(bb, mailbox);

  AttackInfo info = computeAll(bb);

  Bitboard expected = 0;
  for (int pt = 1; pt <= 6; ++pt)
    expected |= info.byPiece[0][pt];
  TEST_ASSERT_EQUAL_UINT64(expected, info.byColor[0]);

  expected = 0;
  for (int pt = 1; pt <= 6; ++pt)
    expected |= info.byPiece[1][pt];
  TEST_ASSERT_EQUAL_UINT64(expected, info.byColor[1]);

  TEST_ASSERT_EQUAL_UINT64(info.byColor[0] | info.byColor[1], info.allAttacks);
}

static void test_attackInfo_empty_board_only_kings(void) {
  init();
  BitboardSet bb;

  Square e1 = squareOf(7, 4), e8 = squareOf(0, 4);
  bb.setPiece(e1, Piece::W_KING);
  bb.setPiece(e8, Piece::B_KING);

  AttackInfo info = computeAll(bb);

  TEST_ASSERT_EQUAL_UINT64(KING[e1], info.byPiece[0][raw(PieceType::KING)]);
  TEST_ASSERT_EQUAL_UINT64(KING[e8], info.byPiece[1][raw(PieceType::KING)]);

  for (int pt = 1; pt <= 5; ++pt) {
    TEST_ASSERT_EQUAL_UINT64(0, info.byPiece[0][pt]);
    TEST_ASSERT_EQUAL_UINT64(0, info.byPiece[1][pt]);
  }

  TEST_ASSERT_EQUAL_UINT64(KING[e1], info.byColor[0]);
  TEST_ASSERT_EQUAL_UINT64(KING[e8], info.byColor[1]);
}

// ===========================================================================
// Static Exchange Evaluation (SEE)
// ===========================================================================
// Helper: parse a FEN and build a Move from coordinate strings.
// The Position is loaded from `fen`; `from`/`to` are algebraic (e.g. "e4").
// `flags` allows setting move flag bits (e.g. MOVE_CAPTURE, MOVE_EP).
static Move makeSEEMove(Position& pos, const char* fen,
                        const char* from, const char* to, uint8_t flags) {
  pos.loadFEN(fen);
  int fc = from[0] - 'a', fr = 8 - (from[1] - '0');
  int tc = to[0]   - 'a', tr = 8 - (to[1]   - '0');
  return Move(squareOf(fr, fc), squareOf(tr, tc), flags);
}

// PxN undefended: white pawn e4 captures black knight on d5.
// Gain = +300 (knight value). No recapture.
static void test_see_pawn_takes_undefended_knight(void) {
  init();
  // White: Ke1, Pe4.  Black: Ke8, Nd5.
  const char* fen = "4k3/8/8/3n4/4P3/8/8/4K3 w - - 0 1";
  Position pos;
  Move m = makeSEEMove(pos, fen, "e4", "d5", MOVE_CAPTURE);
  int score = see(pos.bitboards(), pos.mailbox(), m);
  TEST_ASSERT_EQUAL_INT(300, score);  // knight value
}

// PxR defended by pawn: white pawn e5 captures black rook on d6,
// but d6 is defended by a black pawn on e7. PxR(+500), then pxP(-100).
// Gain = 500 - 100 = +400.
static void test_see_pawn_takes_defended_rook(void) {
  init();
  // White: Ke1, Pe5.  Black: Ke8, Rd6, Pe7.
  const char* fen = "4k3/4p3/3r4/4P3/8/8/8/4K3 w - - 0 1";
  Position pos;
  Move m = makeSEEMove(pos, fen, "e5", "d6", MOVE_CAPTURE);
  int score = see(pos.bitboards(), pos.mailbox(), m);
  // PxR=500, then pxP=100 recapture → net = 500-100 = 400.
  TEST_ASSERT_EQUAL_INT(400, score);
}

// NxP defended by pawn: white knight captures black pawn on d6,
// but d6 is defended by a black pawn on c7.
// Ne5xd6 (+100), c7xd6 recapture (-300). Net = 100-300 = -200.
static void test_see_knight_takes_defended_pawn(void) {
  init();
  // White: Ke1, Ne5.  Black: Ke8, Pd6, Pc7.
  const char* fen = "4k3/2p5/3p4/4N3/8/8/8/4K3 w - - 0 1";
  Position pos;
  Move m = makeSEEMove(pos, fen, "e5", "d6", MOVE_CAPTURE);
  int score = see(pos.bitboards(), pos.mailbox(), m);
  TEST_ASSERT_EQUAL_INT(-200, score);  // losing capture
}

// QxP defended by pawn: white queen captures a pawn defended by another pawn.
// QxP(+100), pxQ(-900). Net = 100-900 = -800. Major loss.
static void test_see_queen_takes_defended_pawn(void) {
  init();
  // White: Ke1, Qe5.  Black: Ke8, Pd6, Pc7. (c7 defends d6)
  const char* fen = "4k3/2p5/3p4/4Q3/8/8/8/4K3 w - - 0 1";
  Position pos;
  Move m = makeSEEMove(pos, fen, "e5", "d6", MOVE_CAPTURE);
  int score = see(pos.bitboards(), pos.mailbox(), m);
  TEST_ASSERT_EQUAL_INT(-800, score);  // 100 - 900
}

// RxB: white rook takes undefended black bishop. Gain = +300.
static void test_see_rook_takes_undefended_bishop(void) {
  init();
  // White: Ke1, Ra5.  Black: Ke8, Bd5.
  // Rook on a5 captures d5 along rank 5.
  const char* fen = "4k3/8/8/R2b4/8/8/8/4K3 w - - 0 1";
  Position pos;
  Move m = makeSEEMove(pos, fen, "a5", "d5", MOVE_CAPTURE);
  int score = see(pos.bitboards(), pos.mailbox(), m);
  TEST_ASSERT_EQUAL_INT(300, score);
}

// EP capture: SEE should handle en passant correctly.
static void test_see_en_passant(void) {
  init();
  // After 1.e4 d5 2.e5 f5: white can play exf6 e.p.
  // White: Ke1, Pe5. Black: Ke8, Pf5, Pd5.
  // FEN with ep square f6:
  const char* fen = "4k3/8/8/3pPp2/8/8/8/4K3 w - f6 0 1";
  Position pos;
  Move m = makeSEEMove(pos, fen, "e5", "f6",
                       MOVE_CAPTURE | MOVE_EP);
  int score = see(pos.bitboards(), pos.mailbox(), m);
  // PxP ep: gain = 100 (pawn), no defenders of f6.
  TEST_ASSERT_EQUAL_INT(100, score);
}

void register_attacks_tests() {
  RUN_TEST(test_knight_attacks_e4);
  RUN_TEST(test_knight_attacks_a1_corner);
  RUN_TEST(test_king_attacks_e4);
  RUN_TEST(test_king_attacks_a1_corner);
  RUN_TEST(test_pawn_attacks_white_e4);
  RUN_TEST(test_pawn_attacks_black_e5);
  RUN_TEST(test_pawn_attacks_a_file_no_wrap);

  RUN_TEST(test_rook_attacks_e4_empty);
  RUN_TEST(test_rook_attacks_e4_with_blocker);
  RUN_TEST(test_rook_attacks_a1_empty);
  RUN_TEST(test_bishop_attacks_e4_empty);
  RUN_TEST(test_bishop_attacks_with_blocker);
  RUN_TEST(test_bishop_attacks_a1_empty);
  RUN_TEST(test_queen_attacks_e4_empty);

  RUN_TEST(test_rook_attacks_bulk_correctness);
  RUN_TEST(test_bishop_attacks_bulk_correctness);

  RUN_TEST(test_xray_rook_north_through_blocker);
  RUN_TEST(test_xray_rook_no_friendly_blocker);
  RUN_TEST(test_xray_bishop_diagonal_through_blocker);

  RUN_TEST(test_ray_between_same_file);
  RUN_TEST(test_ray_between_same_rank);
  RUN_TEST(test_ray_between_same_diagonal);
  RUN_TEST(test_ray_between_anti_diagonal);
  RUN_TEST(test_ray_between_adjacent_returns_zero);
  RUN_TEST(test_ray_between_not_colinear_returns_zero);

  RUN_TEST(test_lineBB_same_rank);
  RUN_TEST(test_lineBB_same_file);
  RUN_TEST(test_lineBB_diagonal);
  RUN_TEST(test_lineBB_non_colinear);
  RUN_TEST(test_lineBB_includes_endpoints);

  RUN_TEST(test_attackInfo_initial_position);
  RUN_TEST(test_attackInfo_pawn_attacks_bulk);
  RUN_TEST(test_attackInfo_color_union);
  RUN_TEST(test_attackInfo_empty_board_only_kings);

  RUN_TEST(test_see_pawn_takes_undefended_knight);
  RUN_TEST(test_see_pawn_takes_defended_rook);
  RUN_TEST(test_see_knight_takes_defended_pawn);
  RUN_TEST(test_see_queen_takes_defended_pawn);
  RUN_TEST(test_see_rook_takes_undefended_bishop);
  RUN_TEST(test_see_en_passant);
}