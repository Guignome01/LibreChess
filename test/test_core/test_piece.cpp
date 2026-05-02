#include <unity.h>

#include <bitboard.h>
#include <piece.h>

#include "../test_helpers.h"

using namespace LibreChess::piece;

// ===========================================================================
// Piece — bit extraction
// ===========================================================================

static void test_piece_type_extraction(void) {
  TEST_ASSERT_ENUM_EQ(PieceType::PAWN,   pieceType(Piece::W_PAWN));
  TEST_ASSERT_ENUM_EQ(PieceType::KNIGHT, pieceType(Piece::W_KNIGHT));
  TEST_ASSERT_ENUM_EQ(PieceType::BISHOP, pieceType(Piece::W_BISHOP));
  TEST_ASSERT_ENUM_EQ(PieceType::ROOK,   pieceType(Piece::W_ROOK));
  TEST_ASSERT_ENUM_EQ(PieceType::QUEEN,  pieceType(Piece::W_QUEEN));
  TEST_ASSERT_ENUM_EQ(PieceType::KING,   pieceType(Piece::W_KING));
  TEST_ASSERT_ENUM_EQ(PieceType::PAWN,   pieceType(Piece::B_PAWN));
  TEST_ASSERT_ENUM_EQ(PieceType::KNIGHT, pieceType(Piece::B_KNIGHT));
  TEST_ASSERT_ENUM_EQ(PieceType::BISHOP, pieceType(Piece::B_BISHOP));
  TEST_ASSERT_ENUM_EQ(PieceType::ROOK,   pieceType(Piece::B_ROOK));
  TEST_ASSERT_ENUM_EQ(PieceType::QUEEN,  pieceType(Piece::B_QUEEN));
  TEST_ASSERT_ENUM_EQ(PieceType::KING,   pieceType(Piece::B_KING));
  TEST_ASSERT_ENUM_EQ(PieceType::NONE,   pieceType(Piece::NONE));
}

static void test_piece_color_extraction(void) {
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pieceColor(Piece::W_PAWN));
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pieceColor(Piece::W_KING));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pieceColor(Piece::B_PAWN));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pieceColor(Piece::B_KING));
}

static void test_make_piece(void) {
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN,   makePiece(Color::WHITE, PieceType::PAWN));
  TEST_ASSERT_ENUM_EQ(Piece::W_KING,   makePiece(Color::WHITE, PieceType::KING));
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN,   makePiece(Color::BLACK, PieceType::PAWN));
  TEST_ASSERT_ENUM_EQ(Piece::B_QUEEN,  makePiece(Color::BLACK, PieceType::QUEEN));
  TEST_ASSERT_ENUM_EQ(Piece::NONE,     makePiece(Color::WHITE, PieceType::NONE));
}

// ===========================================================================
// Piece — predicates
// ===========================================================================

static void test_piece_predicates(void) {
  TEST_ASSERT_TRUE(isEmpty(Piece::NONE));
  TEST_ASSERT_FALSE(isEmpty(Piece::W_PAWN));
}

// ===========================================================================
// Piece — color flip
// ===========================================================================

static void test_color_flip(void) {
  TEST_ASSERT_ENUM_EQ(Color::BLACK, ~Color::WHITE);
  TEST_ASSERT_ENUM_EQ(Color::WHITE, ~Color::BLACK);
}

static void test_piece_color_flip(void) {
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN,   ~Piece::W_PAWN);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN,   ~Piece::B_PAWN);
  TEST_ASSERT_ENUM_EQ(Piece::B_KING,   ~Piece::W_KING);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN,  ~Piece::B_QUEEN);
  TEST_ASSERT_ENUM_EQ(Piece::B_KNIGHT, ~Piece::W_KNIGHT);
}

// ===========================================================================
// Piece — FEN char round-trip
// ===========================================================================

static void test_char_to_piece_roundtrip(void) {
  const char chars[] = "PNBRQKpnbrqk";
  for (int i = 0; chars[i]; i++) {
    Piece p = charToPiece(chars[i]);
    TEST_ASSERT_FALSE(isEmpty(p));
    TEST_ASSERT_EQUAL_CHAR(chars[i], pieceToChar(p));
  }
}

static void test_char_to_piece_invalid(void) {
  TEST_ASSERT_ENUM_EQ(Piece::NONE, charToPiece(' '));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, charToPiece('x'));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, charToPiece('0'));
}

static void test_piece_to_char_none(void) {
  TEST_ASSERT_EQUAL_CHAR(' ', pieceToChar(Piece::NONE));
}

static void test_char_to_piece_type(void) {
  TEST_ASSERT_ENUM_EQ(PieceType::PAWN,   charToPieceType('P'));
  TEST_ASSERT_ENUM_EQ(PieceType::PAWN,   charToPieceType('p'));
  TEST_ASSERT_ENUM_EQ(PieceType::KNIGHT, charToPieceType('N'));
  TEST_ASSERT_ENUM_EQ(PieceType::KNIGHT, charToPieceType('n'));
  TEST_ASSERT_ENUM_EQ(PieceType::QUEEN,  charToPieceType('Q'));
  TEST_ASSERT_ENUM_EQ(PieceType::NONE,   charToPieceType('x'));
}

static void test_piece_type_to_char(void) {
  TEST_ASSERT_EQUAL_CHAR('P', pieceTypeToChar(PieceType::PAWN));
  TEST_ASSERT_EQUAL_CHAR('N', pieceTypeToChar(PieceType::KNIGHT));
  TEST_ASSERT_EQUAL_CHAR('Q', pieceTypeToChar(PieceType::QUEEN));
  TEST_ASSERT_EQUAL_CHAR('K', pieceTypeToChar(PieceType::KING));
  TEST_ASSERT_EQUAL_CHAR(' ', pieceTypeToChar(PieceType::NONE));
}

static void test_charToPiece_all_valid(void) {
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN,   charToPiece('P'));
  TEST_ASSERT_ENUM_EQ(Piece::W_KNIGHT, charToPiece('N'));
  TEST_ASSERT_ENUM_EQ(Piece::W_BISHOP, charToPiece('B'));
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK,   charToPiece('R'));
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN,  charToPiece('Q'));
  TEST_ASSERT_ENUM_EQ(Piece::W_KING,   charToPiece('K'));
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN,   charToPiece('p'));
  TEST_ASSERT_ENUM_EQ(Piece::B_KNIGHT, charToPiece('n'));
  TEST_ASSERT_ENUM_EQ(Piece::B_BISHOP, charToPiece('b'));
  TEST_ASSERT_ENUM_EQ(Piece::B_ROOK,   charToPiece('r'));
  TEST_ASSERT_ENUM_EQ(Piece::B_QUEEN,  charToPiece('q'));
  TEST_ASSERT_ENUM_EQ(Piece::B_KING,   charToPiece('k'));
}

// ===========================================================================
// Piece — piece index
// ===========================================================================

static void test_piece_index_from_piece(void) {
  TEST_ASSERT_EQUAL_INT(0, pieceIndex(Piece::W_PAWN));
  TEST_ASSERT_EQUAL_INT(1, pieceIndex(Piece::W_KNIGHT));
  TEST_ASSERT_EQUAL_INT(5, pieceIndex(Piece::W_KING));
  TEST_ASSERT_EQUAL_INT(6,  pieceIndex(Piece::B_PAWN));
  TEST_ASSERT_EQUAL_INT(7,  pieceIndex(Piece::B_KNIGHT));
  TEST_ASSERT_EQUAL_INT(11, pieceIndex(Piece::B_KING));
  TEST_ASSERT_EQUAL_INT(-1, pieceIndex(Piece::NONE));
}

static void test_piece_index_from_char(void) {
  // White pieces (uppercase FEN chars).
  TEST_ASSERT_EQUAL_INT(0, pieceIndex('P'));
  TEST_ASSERT_EQUAL_INT(1, pieceIndex('N'));
  TEST_ASSERT_EQUAL_INT(2, pieceIndex('B'));
  TEST_ASSERT_EQUAL_INT(3, pieceIndex('R'));
  TEST_ASSERT_EQUAL_INT(4, pieceIndex('Q'));
  TEST_ASSERT_EQUAL_INT(5, pieceIndex('K'));

  // Black pieces (lowercase FEN chars).
  TEST_ASSERT_EQUAL_INT(6,  pieceIndex('p'));
  TEST_ASSERT_EQUAL_INT(7,  pieceIndex('n'));
  TEST_ASSERT_EQUAL_INT(8,  pieceIndex('b'));
  TEST_ASSERT_EQUAL_INT(9,  pieceIndex('r'));
  TEST_ASSERT_EQUAL_INT(10, pieceIndex('q'));
  TEST_ASSERT_EQUAL_INT(11, pieceIndex('k'));

  // Invalid char.
  TEST_ASSERT_EQUAL_INT(PIECE_IDX_NONE, pieceIndex('x'));
  TEST_ASSERT_EQUAL_INT(PIECE_IDX_NONE, pieceIndex('1'));
}

static void test_piece_index_constants(void) {
  // Named constant matches raw sentinel value.
  TEST_ASSERT_EQUAL_INT(-1, PIECE_IDX_NONE);

  // isValidPieceIndex: valid range [0, 11].
  TEST_ASSERT_TRUE(isValidPieceIndex(0));
  TEST_ASSERT_TRUE(isValidPieceIndex(5));
  TEST_ASSERT_TRUE(isValidPieceIndex(11));

  // isValidPieceIndex: invalid values.
  TEST_ASSERT_FALSE(isValidPieceIndex(-1));
  TEST_ASSERT_FALSE(isValidPieceIndex(12));
  TEST_ASSERT_FALSE(isValidPieceIndex(PIECE_IDX_NONE));
}

static void test_piece_index_consistency(void) {
  // All three overloads must return the same value for the same piece.
  TEST_ASSERT_EQUAL_INT(pieceIndex(Color::WHITE, PieceType::PAWN), pieceIndex(Piece::W_PAWN));
  TEST_ASSERT_EQUAL_INT(pieceIndex(Color::WHITE, PieceType::PAWN), pieceIndex('P'));
  TEST_ASSERT_EQUAL_INT(pieceIndex(Color::BLACK, PieceType::KING), pieceIndex(Piece::B_KING));
  TEST_ASSERT_EQUAL_INT(pieceIndex(Color::BLACK, PieceType::KING), pieceIndex('k'));
}

// ===========================================================================
// Piece — color helpers
// ===========================================================================

static void test_color_helpers(void) {
  // LERF-native helpers
  TEST_ASSERT_EQUAL_INT(8,  pawnForward(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(-8, pawnForward(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(0,  homeRank(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(7,  homeRank(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(7,  promotionRank(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(0,  promotionRank(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(1,  pawnStartRank(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(6,  pawnStartRank(Color::BLACK));
}

static void test_color_name(void) {
  TEST_ASSERT_EQUAL_STRING("White", colorName(Color::WHITE));
  TEST_ASSERT_EQUAL_STRING("Black", colorName(Color::BLACK));
}

static void test_color_char_conversion(void) {
  TEST_ASSERT_ENUM_EQ(Color::WHITE, charToColor('w'));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, charToColor('b'));
  TEST_ASSERT_EQUAL_CHAR('w', colorToChar(Color::WHITE));
  TEST_ASSERT_EQUAL_CHAR('b', colorToChar(Color::BLACK));
}

// ===========================================================================
// Piece — zero-initialization safety
// ===========================================================================

static void test_piece_none_is_zero(void) {
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(Piece::NONE));
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(PieceType::NONE));
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(Color::WHITE));
}

// ===========================================================================
// Piece — pieceColor (from utils)
// ===========================================================================

static void test_getPieceColor(void) {
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pieceColor(Piece::W_KING));
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pieceColor(Piece::W_PAWN));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pieceColor(Piece::B_KING));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pieceColor(Piece::B_PAWN));
  // pieceColor on NONE is undefined behavior — only check valid pieces
}

static void test_colorName_explicit(void) {
  TEST_ASSERT_EQUAL_STRING("White", colorName(Color::WHITE));
  TEST_ASSERT_EQUAL_STRING("Black", colorName(Color::BLACK));
}

// ===========================================================================
// Piece — opponent color
// ===========================================================================

static void test_opponentColor_white(void) {
  TEST_ASSERT_EQUAL_STRING("Black", colorName(~Color::WHITE));
}

static void test_opponentColor_black(void) {
  TEST_ASSERT_EQUAL_STRING("White", colorName(~Color::BLACK));
}

// ===========================================================================
// Piece — pawnForward / homeRank
// ===========================================================================

static void test_pawnForward_white(void) {
  TEST_ASSERT_EQUAL_INT(8, pawnForward(Color::WHITE));
}

static void test_pawnForward_black(void) {
  TEST_ASSERT_EQUAL_INT(-8, pawnForward(Color::BLACK));
}

static void test_homeRank_white(void) {
  TEST_ASSERT_EQUAL_INT(0, homeRank(Color::WHITE));
}

static void test_homeRank_black(void) {
  TEST_ASSERT_EQUAL_INT(7, homeRank(Color::BLACK));
}

// ===========================================================================
// Registration
// ===========================================================================

void register_piece_tests() {
  // Piece — bit extraction and construction
  RUN_TEST(test_piece_type_extraction);
  RUN_TEST(test_piece_color_extraction);
  RUN_TEST(test_make_piece);
  RUN_TEST(test_piece_predicates);
  RUN_TEST(test_color_flip);
  RUN_TEST(test_piece_color_flip);

  // Piece — FEN char conversion
  RUN_TEST(test_char_to_piece_roundtrip);
  RUN_TEST(test_char_to_piece_invalid);
  RUN_TEST(test_piece_to_char_none);
  RUN_TEST(test_char_to_piece_type);
  RUN_TEST(test_piece_type_to_char);
  RUN_TEST(test_charToPiece_all_valid);

  // Piece — piece index
  RUN_TEST(test_piece_index_from_piece);
  RUN_TEST(test_piece_index_from_char);
  RUN_TEST(test_piece_index_constants);
  RUN_TEST(test_piece_index_consistency);

  // Piece — color helpers
  RUN_TEST(test_color_helpers);
  RUN_TEST(test_color_name);
  RUN_TEST(test_color_char_conversion);

  // Piece — zero-init
  RUN_TEST(test_piece_none_is_zero);

  // Piece — pieceColor
  RUN_TEST(test_getPieceColor);
  RUN_TEST(test_colorName_explicit);

  // Piece — opponent color
  RUN_TEST(test_opponentColor_white);
  RUN_TEST(test_opponentColor_black);

  // Piece — pawnForward / homeRank
  RUN_TEST(test_pawnForward_white);
  RUN_TEST(test_pawnForward_black);
  RUN_TEST(test_homeRank_white);
  RUN_TEST(test_homeRank_black);
}
