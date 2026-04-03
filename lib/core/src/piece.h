#ifndef LIBRECHESS_PIECE_H
#define LIBRECHESS_PIECE_H

// Piece operations for the LibreChess chess library.
//
// All functions in LibreChess::piece are constexpr, O(1), and operate on
// the bit-packed Piece = (Color << 3) | PieceType encoding defined in
// types.h. Provides extraction (pieceType, pieceColor), construction
// (makePiece), predicates (isEmpty), color
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

inline const char* colorName(Color c) {
  return c == Color::WHITE ? "White" : "Black";
}

// ---------------------------------------------------------------------------
// FEN boundary conversion — char <-> Piece
// ---------------------------------------------------------------------------

inline constexpr Piece charToPiece(char c) {
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

inline constexpr PieceType charToPieceType(char c) {
  switch (c) {
    case 'P': case 'p': return PieceType::PAWN;
    case 'N': case 'n': return PieceType::KNIGHT;
    case 'B': case 'b': return PieceType::BISHOP;
    case 'R': case 'r': return PieceType::ROOK;
    case 'Q': case 'q': return PieceType::QUEEN;
    case 'K': case 'k': return PieceType::KING;
    default:            return PieceType::NONE;
  }
}

constexpr char pieceTypeToChar(PieceType t) {
  constexpr char TABLE[] = {' ', 'P', 'N', 'B', 'R', 'Q', 'K'};
  return (raw(t) < sizeof(TABLE)) ? TABLE[raw(t)] : '?';
}

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

// Lookup table: Piece -> Zobrist index (0-11), -1 for NONE/unused.
// Indexed by raw(Piece) which spans 0-15 (Piece = (Color<<3) | PieceType).
static constexpr int ZOBRIST_IDX_TABLE[16] = {
  // 0:NONE  1:WP  2:WN  3:WB  4:WR  5:WQ  6:WK  7:(-)  8:(-)  9:BP 10:BN 11:BB 12:BR 13:BQ 14:BK 15:(-)
      -1,     0,    1,    2,    3,    4,    5,   -1,   -1,    6,    7,    8,    9,   10,   11,   -1
};

constexpr int pieceZobristIndex(Piece p) {
  return (raw(p) < 16) ? ZOBRIST_IDX_TABLE[raw(p)] : ZOBRIST_IDX_NONE;
}

// ---------------------------------------------------------------------------
// Color conversion helpers
// ---------------------------------------------------------------------------

constexpr char colorToChar(Color c) {
  return c == Color::WHITE ? 'w' : 'b';
}

}  // namespace piece
}  // namespace LibreChess

#endif  // LIBRECHESS_PIECE_H
