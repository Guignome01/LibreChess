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

  TEST_ASSERT_TRUE(isWhite(Piece::W_QUEEN));
  TEST_ASSERT_FALSE(isWhite(Piece::B_QUEEN));
  TEST_ASSERT_FALSE(isWhite(Piece::NONE));

  TEST_ASSERT_TRUE(isBlack(Piece::B_ROOK));
  TEST_ASSERT_FALSE(isBlack(Piece::W_ROOK));
  TEST_ASSERT_FALSE(isBlack(Piece::NONE));

  TEST_ASSERT_TRUE(isColor(Piece::W_KNIGHT, Color::WHITE));
  TEST_ASSERT_FALSE(isColor(Piece::W_KNIGHT, Color::BLACK));
  TEST_ASSERT_FALSE(isColor(Piece::NONE, Color::WHITE));
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

// ===========================================================================
// Piece — material values
// ===========================================================================

static void test_piece_value(void) {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pieceValue(Piece::W_PAWN));
  TEST_ASSERT_EQUAL_FLOAT(3.0f, pieceValue(Piece::W_KNIGHT));
  TEST_ASSERT_EQUAL_FLOAT(3.0f, pieceValue(Piece::W_BISHOP));
  TEST_ASSERT_EQUAL_FLOAT(5.0f, pieceValue(Piece::W_ROOK));
  TEST_ASSERT_EQUAL_FLOAT(9.0f, pieceValue(Piece::W_QUEEN));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pieceValue(Piece::W_KING));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pieceValue(Piece::B_PAWN));
  TEST_ASSERT_EQUAL_FLOAT(9.0f, pieceValue(Piece::B_QUEEN));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pieceValue(Piece::NONE));
}

static void test_piece_value_all_black(void) {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pieceValue(Piece::B_PAWN));
  TEST_ASSERT_EQUAL_FLOAT(3.0f, pieceValue(Piece::B_KNIGHT));
  TEST_ASSERT_EQUAL_FLOAT(3.0f, pieceValue(Piece::B_BISHOP));
  TEST_ASSERT_EQUAL_FLOAT(5.0f, pieceValue(Piece::B_ROOK));
  TEST_ASSERT_EQUAL_FLOAT(9.0f, pieceValue(Piece::B_QUEEN));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pieceValue(Piece::B_KING));
}

static void test_pieceTypeValue_all(void) {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pieceTypeValue(PieceType::PAWN));
  TEST_ASSERT_EQUAL_FLOAT(3.0f, pieceTypeValue(PieceType::KNIGHT));
  TEST_ASSERT_EQUAL_FLOAT(3.0f, pieceTypeValue(PieceType::BISHOP));
  TEST_ASSERT_EQUAL_FLOAT(5.0f, pieceTypeValue(PieceType::ROOK));
  TEST_ASSERT_EQUAL_FLOAT(9.0f, pieceTypeValue(PieceType::QUEEN));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pieceTypeValue(PieceType::KING));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pieceTypeValue(PieceType::NONE));
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
// Piece — Zobrist index
// ===========================================================================

static void test_zobrist_index(void) {
  TEST_ASSERT_EQUAL_INT(0, pieceZobristIndex(Piece::W_PAWN));
  TEST_ASSERT_EQUAL_INT(1, pieceZobristIndex(Piece::W_KNIGHT));
  TEST_ASSERT_EQUAL_INT(5, pieceZobristIndex(Piece::W_KING));
  TEST_ASSERT_EQUAL_INT(6,  pieceZobristIndex(Piece::B_PAWN));
  TEST_ASSERT_EQUAL_INT(7,  pieceZobristIndex(Piece::B_KNIGHT));
  TEST_ASSERT_EQUAL_INT(11, pieceZobristIndex(Piece::B_KING));
  TEST_ASSERT_EQUAL_INT(-1, pieceZobristIndex(Piece::NONE));
}

static void test_zobrist_index_constants(void) {
  // Named constant matches raw sentinel value.
  TEST_ASSERT_EQUAL_INT(-1, ZOBRIST_IDX_NONE);

  // isValidZobristIndex: valid range [0, 11].
  TEST_ASSERT_TRUE(isValidZobristIndex(0));
  TEST_ASSERT_TRUE(isValidZobristIndex(5));
  TEST_ASSERT_TRUE(isValidZobristIndex(11));

  // isValidZobristIndex: invalid values.
  TEST_ASSERT_FALSE(isValidZobristIndex(-1));
  TEST_ASSERT_FALSE(isValidZobristIndex(12));
  TEST_ASSERT_FALSE(isValidZobristIndex(ZOBRIST_IDX_NONE));
}

// ===========================================================================
// Piece — color helpers
// ===========================================================================

static void test_color_helpers(void) {
  TEST_ASSERT_EQUAL_INT(-1, pawnDirection(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(1,  pawnDirection(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(7,  homeRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(0,  homeRow(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(0,  promotionRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(7,  promotionRow(Color::BLACK));
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
// Piece — isPromotion
// ===========================================================================

static void test_isPromotion_white_pawn_on_row_0(void) {
  TEST_ASSERT_TRUE(isPromotion(Piece::W_PAWN, 0));
}

static void test_isPromotion_white_pawn_on_other_rows(void) {
  for (int row = 1; row <= 7; row++) {
    TEST_ASSERT_FALSE(isPromotion(Piece::W_PAWN, row));
  }
}

static void test_isPromotion_black_pawn_on_row_7(void) {
  TEST_ASSERT_TRUE(isPromotion(Piece::B_PAWN, 7));
}

static void test_isPromotion_black_pawn_on_other_rows(void) {
  for (int row = 0; row <= 6; row++) {
    TEST_ASSERT_FALSE(isPromotion(Piece::B_PAWN, row));
  }
}

static void test_isPromotion_non_pawn_pieces(void) {
  Piece whitePieces[] = {Piece::W_KNIGHT, Piece::W_BISHOP, Piece::W_ROOK,
                         Piece::W_QUEEN, Piece::W_KING};
  for (auto p : whitePieces) {
    TEST_ASSERT_FALSE(isPromotion(p, 0));
  }
  Piece blackPieces[] = {Piece::B_KNIGHT, Piece::B_BISHOP, Piece::B_ROOK,
                         Piece::B_QUEEN, Piece::B_KING};
  for (auto p : blackPieces) {
    TEST_ASSERT_FALSE(isPromotion(p, 7));
  }
}

static void test_isPromotion_none(void) {
  TEST_ASSERT_FALSE(isPromotion(Piece::NONE, 0));
  TEST_ASSERT_FALSE(isPromotion(Piece::NONE, 7));
}

// ===========================================================================
// Piece — pieceColor / isWhite / isBlack (from utils)
// ===========================================================================

static void test_getPieceColor(void) {
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pieceColor(Piece::W_KING));
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pieceColor(Piece::W_PAWN));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pieceColor(Piece::B_KING));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pieceColor(Piece::B_PAWN));
  // pieceColor on NONE is undefined behavior — only check valid pieces
}

static void test_isWhitePiece(void) {
  TEST_ASSERT_TRUE(isWhite(Piece::W_KING));
  TEST_ASSERT_TRUE(isWhite(Piece::W_QUEEN));
  TEST_ASSERT_FALSE(isWhite(Piece::B_KING));
  TEST_ASSERT_FALSE(isWhite(Piece::NONE));
}

static void test_isBlackPiece(void) {
  TEST_ASSERT_TRUE(isBlack(Piece::B_KING));
  TEST_ASSERT_TRUE(isBlack(Piece::B_QUEEN));
  TEST_ASSERT_FALSE(isBlack(Piece::W_KING));
  TEST_ASSERT_FALSE(isBlack(Piece::NONE));
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
// Piece — pawnDirection / homeRow
// ===========================================================================

static void test_pawnDirection_white(void) {
  TEST_ASSERT_EQUAL_INT(-1, pawnDirection(Color::WHITE));
}

static void test_pawnDirection_black(void) {
  TEST_ASSERT_EQUAL_INT(1, pawnDirection(Color::BLACK));
}

static void test_homeRow_white(void) {
  TEST_ASSERT_EQUAL_INT(7, homeRow(Color::WHITE));
}

static void test_homeRow_black(void) {
  TEST_ASSERT_EQUAL_INT(0, homeRow(Color::BLACK));
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

  // Piece — material values
  RUN_TEST(test_piece_value);
  RUN_TEST(test_piece_value_all_black);
  RUN_TEST(test_pieceTypeValue_all);

  // Piece — Zobrist index
  RUN_TEST(test_zobrist_index);
  RUN_TEST(test_zobrist_index_constants);

  // Piece — color helpers
  RUN_TEST(test_color_helpers);
  RUN_TEST(test_color_name);
  RUN_TEST(test_color_char_conversion);

  // Piece — zero-init and isPromotion
  RUN_TEST(test_piece_none_is_zero);
  RUN_TEST(test_isPromotion_white_pawn_on_row_0);
  RUN_TEST(test_isPromotion_white_pawn_on_other_rows);
  RUN_TEST(test_isPromotion_black_pawn_on_row_7);
  RUN_TEST(test_isPromotion_black_pawn_on_other_rows);
  RUN_TEST(test_isPromotion_non_pawn_pieces);
  RUN_TEST(test_isPromotion_none);

  // Piece — pieceColor / isWhite / isBlack
  RUN_TEST(test_getPieceColor);
  RUN_TEST(test_isWhitePiece);
  RUN_TEST(test_isBlackPiece);
  RUN_TEST(test_colorName_explicit);

  // Piece — opponent color
  RUN_TEST(test_opponentColor_white);
  RUN_TEST(test_opponentColor_black);

  // Piece — pawnDirection / homeRow
  RUN_TEST(test_pawnDirection_white);
  RUN_TEST(test_pawnDirection_black);
  RUN_TEST(test_homeRow_white);
  RUN_TEST(test_homeRow_black);
}
