#include <unity.h>

#include <bitboard.h>

#include "../test_helpers.h"

using namespace LibreChess;
using namespace LibreChess::piece;

// ---------------------------------------------------------------------------
// Square mapping
// ---------------------------------------------------------------------------

static void test_squareOf_corners(void) {
  // Row 0, col 0 = a8 (black's QR corner)
  TEST_ASSERT_EQUAL_INT(SQ_A8, squareOf(0, 0));
  // Row 0, col 7 = h8
  TEST_ASSERT_EQUAL_INT(SQ_H8, squareOf(0, 7));
  // Row 7, col 0 = a1 (white's QR corner)
  TEST_ASSERT_EQUAL_INT(SQ_A1, squareOf(7, 0));
  // Row 7, col 7 = h1
  TEST_ASSERT_EQUAL_INT(SQ_H1, squareOf(7, 7));
}

static void test_squareOf_e4(void) {
  // e4 = row 4, col 4 in the existing system
  // LERF: e4 = rank 4, file e = (4-1)*8 + 4 = 28
  Square e4 = squareOf(4, 4);
  TEST_ASSERT_EQUAL_INT(28, e4);
}

static void test_rowOf_colOf_roundtrip(void) {
  for (int row = 0; row < 8; ++row) {
    for (int col = 0; col < 8; ++col) {
      Square sq = squareOf(row, col);
      TEST_ASSERT_EQUAL_INT(row, rowOf(sq));
      TEST_ASSERT_EQUAL_INT(col, colOf(sq));
    }
  }
}

static void test_squareOf_identity_roundtrip(void) {
  // Every square 0–63 survives the roundtrip
  for (Square sq = 0; sq < 64; ++sq) {
    TEST_ASSERT_EQUAL_INT(sq, squareOf(rowOf(sq), colOf(sq)));
  }
}

// ---------------------------------------------------------------------------
// Bit manipulation
// ---------------------------------------------------------------------------

static void test_popcount_basic(void) {
  TEST_ASSERT_EQUAL_INT(0, popcount(0));
  TEST_ASSERT_EQUAL_INT(1, popcount(1));
  TEST_ASSERT_EQUAL_INT(64, popcount(~0ULL));
  TEST_ASSERT_EQUAL_INT(8, popcount(RANK_1));
  TEST_ASSERT_EQUAL_INT(8, popcount(FILE_A));
}

static void test_lsb_basic(void) {
  TEST_ASSERT_EQUAL_INT(0, lsb(1ULL));
  TEST_ASSERT_EQUAL_INT(63, lsb(1ULL << 63));
  TEST_ASSERT_EQUAL_INT(3, lsb(0b11111000ULL));
}

static void test_popLsb_drains(void) {
  Bitboard bb = squareBB(10) | squareBB(20) | squareBB(40);
  TEST_ASSERT_EQUAL_INT(3, popcount(bb));

  Square s1 = popLsb(bb);
  TEST_ASSERT_EQUAL_INT(10, s1);
  TEST_ASSERT_EQUAL_INT(2, popcount(bb));

  Square s2 = popLsb(bb);
  TEST_ASSERT_EQUAL_INT(20, s2);

  Square s3 = popLsb(bb);
  TEST_ASSERT_EQUAL_INT(40, s3);

  TEST_ASSERT_EQUAL_UINT64(0, bb);
}

// ---------------------------------------------------------------------------
// File/rank masks
// ---------------------------------------------------------------------------

static void test_file_rank_masks(void) {
  TEST_ASSERT_EQUAL_INT(8, popcount(FILE_A));
  TEST_ASSERT_EQUAL_INT(8, popcount(FILE_H));
  TEST_ASSERT_EQUAL_INT(8, popcount(RANK_1));
  TEST_ASSERT_EQUAL_INT(8, popcount(RANK_8));
  // FILE_A and FILE_H should not overlap
  TEST_ASSERT_EQUAL_UINT64(0, FILE_A & FILE_H);
  // RANK_1 and RANK_8 should not overlap
  TEST_ASSERT_EQUAL_UINT64(0, RANK_1 & RANK_8);
}

// ---------------------------------------------------------------------------
// Directional shifts
// ---------------------------------------------------------------------------

static void test_shift_north_south(void) {
  Bitboard a1 = squareBB(SQ_A1);
  Bitboard a2 = shiftNorth(a1);
  TEST_ASSERT_EQUAL_UINT64(squareBB(squareOf(6, 0)), a2);  // a2
  TEST_ASSERT_EQUAL_UINT64(a1, shiftSouth(a2));
}

static void test_shift_east_no_wrap(void) {
  // h1 shifted east should produce 0 (not wrap to a2)
  Bitboard h1 = squareBB(SQ_H1);
  TEST_ASSERT_EQUAL_UINT64(0, shiftEast(h1));
}

static void test_shift_west_no_wrap(void) {
  // a1 shifted west should produce 0 (not wrap to h-something)
  Bitboard a1 = squareBB(SQ_A1);
  TEST_ASSERT_EQUAL_UINT64(0, shiftWest(a1));
}

// ---------------------------------------------------------------------------
// BitboardSet operations
// ---------------------------------------------------------------------------

static void test_bitboardset_setPiece_removePiece(void) {
  BitboardSet bb;
  Square e4 = squareOf(4, 4);

  bb.setPiece(e4, Piece::W_KNIGHT);
  TEST_ASSERT_EQUAL_UINT64(squareBB(e4), bb.occupied);
  TEST_ASSERT_EQUAL_UINT64(squareBB(e4), bb.byColor[0]);  // WHITE
  TEST_ASSERT_EQUAL_UINT64(0, bb.byColor[1]);              // BLACK
  TEST_ASSERT_EQUAL_INT(1, popcount(bb.occupied));

  // pieceOn should find it
  Piece found = pieceOn(bb, e4);
  TEST_ASSERT_ENUM_EQ(Piece::W_KNIGHT, found);

  bb.removePiece(e4, Piece::W_KNIGHT);
  TEST_ASSERT_EQUAL_UINT64(0, bb.occupied);
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pieceOn(bb, e4));
}

static void test_bitboardset_movePiece(void) {
  BitboardSet bb;
  Square e2 = squareOf(6, 4);  // e2
  Square e4 = squareOf(4, 4);  // e4

  bb.setPiece(e2, Piece::W_PAWN);
  bb.movePiece(e2, e4, Piece::W_PAWN);

  TEST_ASSERT_EQUAL_UINT64(squareBB(e4), bb.occupied);
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pieceOn(bb, e2));
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pieceOn(bb, e4));
}

static void test_bitboardset_multiple_pieces(void) {
  BitboardSet bb;
  Square e1 = squareOf(7, 4);
  Square d8 = squareOf(0, 3);

  bb.setPiece(e1, Piece::W_KING);
  bb.setPiece(d8, Piece::B_QUEEN);

  TEST_ASSERT_EQUAL_INT(2, popcount(bb.occupied));
  TEST_ASSERT_EQUAL_INT(1, popcount(bb.byColor[0]));  // 1 white piece
  TEST_ASSERT_EQUAL_INT(1, popcount(bb.byColor[1]));  // 1 black piece
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pieceOn(bb, e1));
  TEST_ASSERT_ENUM_EQ(Piece::B_QUEEN, pieceOn(bb, d8));
}

static void test_bitboardset_clear(void) {
  BitboardSet bb;
  bb.setPiece(squareOf(0, 0), Piece::B_ROOK);
  bb.setPiece(squareOf(7, 7), Piece::W_ROOK);
  bb.clear();
  TEST_ASSERT_EQUAL_UINT64(0, bb.occupied);
  for (int i = 0; i < 12; ++i)
    TEST_ASSERT_EQUAL_UINT64(0, bb.byPiece[i]);
}


// ---------------------------------------------------------------------------
// Square-color masks
// ---------------------------------------------------------------------------

// a1 (row 7, col 0) is a dark square.
static void test_square_color_a1_dark(void) {
  Square a1 = squareOf(7, 0);
  TEST_ASSERT_NOT_EQUAL(0, DARK_SQUARES & squareBB(a1));
  TEST_ASSERT_EQUAL_UINT64(0, LIGHT_SQUARES & squareBB(a1));
}

// b1 (row 7, col 1) is a light square.
static void test_square_color_b1_light(void) {
  Square b1 = squareOf(7, 1);
  TEST_ASSERT_NOT_EQUAL(0, LIGHT_SQUARES & squareBB(b1));
  TEST_ASSERT_EQUAL_UINT64(0, DARK_SQUARES & squareBB(b1));
}

// Exactly 32 squares of each color.
static void test_square_color_popcount(void) {
  TEST_ASSERT_EQUAL(32, popcount(DARK_SQUARES));
  TEST_ASSERT_EQUAL(32, popcount(LIGHT_SQUARES));
}

// No overlap between dark and light.
static void test_square_color_no_overlap(void) {
  TEST_ASSERT_EQUAL_UINT64(0, DARK_SQUARES & LIGHT_SQUARES);
  TEST_ASSERT_EQUAL_UINT64(~0ULL, DARK_SQUARES | LIGHT_SQUARES);
}


void register_bitboard_tests() {
  // Square mapping
  RUN_TEST(test_squareOf_corners);
  RUN_TEST(test_squareOf_e4);
  RUN_TEST(test_rowOf_colOf_roundtrip);
  RUN_TEST(test_squareOf_identity_roundtrip);

  // Bit manipulation
  RUN_TEST(test_popcount_basic);
  RUN_TEST(test_lsb_basic);
  RUN_TEST(test_popLsb_drains);

  // File/rank masks
  RUN_TEST(test_file_rank_masks);

  // Directional shifts
  RUN_TEST(test_shift_north_south);
  RUN_TEST(test_shift_east_no_wrap);
  RUN_TEST(test_shift_west_no_wrap);

  // BitboardSet
  RUN_TEST(test_bitboardset_setPiece_removePiece);
  RUN_TEST(test_bitboardset_movePiece);
  RUN_TEST(test_bitboardset_multiple_pieces);
  RUN_TEST(test_bitboardset_clear);

  // Square-color masks
  RUN_TEST(test_square_color_a1_dark);
  RUN_TEST(test_square_color_b1_light);
  RUN_TEST(test_square_color_popcount);
  RUN_TEST(test_square_color_no_overlap);
}
