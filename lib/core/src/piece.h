#ifndef LIBRECHESS_PIECE_H
#define LIBRECHESS_PIECE_H

// Piece operations for the LibreChess chess library.
//
// All functions in LibreChess::piece are constexpr, O(1), and operate on
// the bit-packed Piece = (Color << 3) | PieceType encoding defined in
// types.h. Provides extraction (pieceType, pieceColor), construction
// (makePiece), predicates (isEmpty, isWhite, isBlack, isColor), color
// helpers (pawnDirection, homeRow, promotionRow), FEN boundary conversion
// (charToPiece, pieceToChar), and Zobrist indexing (pieceZobristIndex).

#include "types.h"

namespace LibreChess {
namespace piece {

using LibreChess::raw;  // expose raw() overloads as piece::raw()

// ---------------------------------------------------------------------------
// Bit extraction (constexpr, O(1))
// ---------------------------------------------------------------------------

constexpr PieceType pieceType(Piece p) {
  return static_cast<PieceType>(raw(p) & 0x07);
}

// Only valid when p != Piece::NONE.
constexpr Color pieceColor(Piece p) {
  return static_cast<Color>(raw(p) >> 3);
}

constexpr Piece makePiece(Color c, PieceType t) {
  return static_cast<Piece>((raw(c) << 3) | raw(t));
}

// ---------------------------------------------------------------------------
// Predicates
// ---------------------------------------------------------------------------

constexpr bool isEmpty(Piece p) { return p == Piece::NONE; }

constexpr bool isWhite(Piece p) {
  return !isEmpty(p) && pieceColor(p) == Color::WHITE;
}

constexpr bool isBlack(Piece p) {
  return !isEmpty(p) && pieceColor(p) == Color::BLACK;
}

constexpr bool isColor(Piece p, Color c) {
  return !isEmpty(p) && pieceColor(p) == c;
}

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------

constexpr int pawnDirection(Color c) {
  return c == Color::WHITE ? -1 : 1;
}

constexpr int homeRow(Color c) {
  return c == Color::WHITE ? 7 : 0;
}

constexpr int promotionRow(Color c) {
  return c == Color::WHITE ? 0 : 7;
}

constexpr bool isPromotion(Piece piece, int targetRow) {
  return pieceType(piece) == PieceType::PAWN &&
         targetRow == promotionRow(pieceColor(piece));
}

inline const char* colorName(Color c) {
  return c == Color::WHITE ? "White" : "Black";
}

// ---------------------------------------------------------------------------
// FEN boundary conversion — lookup tables for char <-> Piece
// ---------------------------------------------------------------------------

constexpr Piece charToPiece(char c) {
  switch (c) {
    case 'P': return Piece::W_PAWN;
    case 'N': return Piece::W_KNIGHT;
    case 'B': return Piece::W_BISHOP;
    case 'R': return Piece::W_ROOK;
    case 'Q': return Piece::W_QUEEN;
    case 'K': return Piece::W_KING;
    case 'p': return Piece::B_PAWN;
    case 'n': return Piece::B_KNIGHT;
    case 'b': return Piece::B_BISHOP;
    case 'r': return Piece::B_ROOK;
    case 'q': return Piece::B_QUEEN;
    case 'k': return Piece::B_KING;
    default:  return Piece::NONE;
  }
}

constexpr char pieceToChar(Piece p) {
  constexpr char TABLE[] = {
      ' ',  // 0  NONE
      'P',  // 1  W_PAWN
      'N',  // 2  W_KNIGHT
      'B',  // 3  W_BISHOP
      'R',  // 4  W_ROOK
      'Q',  // 5  W_QUEEN
      'K',  // 6  W_KING
      '?',  // 7  (unused)
      '?',  // 8  (unused)
      'p',  // 9  B_PAWN
      'n',  // 10 B_KNIGHT
      'b',  // 11 B_BISHOP
      'r',  // 12 B_ROOK
      'q',  // 13 B_QUEEN
      'k',  // 14 B_KING
      '?',  // 15 (unused)
  };
  return (raw(p) < sizeof(TABLE)) ? TABLE[raw(p)] : '?';
}

// Char to PieceType (case-insensitive: 'Q' -> QUEEN, 'n' -> KNIGHT).
constexpr PieceType charToPieceType(char c) {
  switch (c >= 'a' ? (c - 32) : c) {  // toupper equivalent
    case 'P': return PieceType::PAWN;
    case 'N': return PieceType::KNIGHT;
    case 'B': return PieceType::BISHOP;
    case 'R': return PieceType::ROOK;
    case 'Q': return PieceType::QUEEN;
    case 'K': return PieceType::KING;
    default:  return PieceType::NONE;
  }
}

constexpr char pieceTypeToChar(PieceType t) {
  constexpr char TABLE[] = {' ', 'P', 'N', 'B', 'R', 'Q', 'K'};
  return (raw(t) < sizeof(TABLE)) ? TABLE[raw(t)] : '?';
}

// Is char a valid promotion piece letter? (case-insensitive: q, r, b, n)
constexpr bool isValidPromotionChar(char c) {
  return c == 'q' || c == 'Q' || c == 'r' || c == 'R' ||
         c == 'b' || c == 'B' || c == 'n' || c == 'N';
}

// ---------------------------------------------------------------------------
// Material values — indexed by PieceType (color-independent).
// ---------------------------------------------------------------------------

constexpr float pieceTypeValue(PieceType t) {
  constexpr float V[] = {0, 1, 3, 3, 5, 9, 0};  // NONE..KING
  return (raw(t) < sizeof(V) / sizeof(V[0])) ? V[raw(t)] : 0;
}

constexpr float pieceValue(Piece p) { return pieceTypeValue(pieceType(p)); }

// ---------------------------------------------------------------------------
// Zobrist index — maps Piece to 0..11 for the Zobrist key table.
//
// pieceZobristIndex() returns 0–11 for valid pieces, ZOBRIST_IDX_NONE
// for Piece::NONE.  Callers that use the result as an array index MUST
// check with isValidZobristIndex() first — otherwise bb.byPiece[-1] or
// similar OOB access can occur.
//
// Internal hot-path callers (BitboardSet mutators, zobrist::computeHash)
// are exempt — they operate only on non-NONE pieces by design.
// ---------------------------------------------------------------------------

static constexpr int ZOBRIST_IDX_NONE = -1;

constexpr bool isValidZobristIndex(int idx) {
  return idx >= 0 && idx < 12;
}

constexpr int pieceZobristIndex(Piece p) {
  // White pieces: PAWN=0 KNIGHT=1 BISHOP=2 ROOK=3 QUEEN=4 KING=5
  // Black pieces: PAWN=6 KNIGHT=7 BISHOP=8 ROOK=9 QUEEN=10 KING=11
  if (isEmpty(p)) return ZOBRIST_IDX_NONE;
  int typeIdx = raw(pieceType(p)) - 1;  // PAWN=0..KING=5
  return (pieceColor(p) == Color::BLACK) ? typeIdx + 6 : typeIdx;
}

// ---------------------------------------------------------------------------
// Color conversion helpers
// ---------------------------------------------------------------------------

constexpr Color charToColor(char c) {
  return (c == 'b' || c == 'B') ? Color::BLACK : Color::WHITE;
}

constexpr char colorToChar(Color c) {
  return c == Color::WHITE ? 'w' : 'b';
}

}  // namespace piece
}  // namespace LibreChess

#endif  // LIBRECHESS_PIECE_H
