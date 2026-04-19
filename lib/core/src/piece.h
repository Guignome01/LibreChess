#ifndef LIBRECHESS_PIECE_H
#define LIBRECHESS_PIECE_H

// Piece operations for the LibreChess chess library.
//
// All functions in LibreChess::piece are constexpr, O(1), and operate on
// the bit-packed Piece = (Color << 3) | PieceType encoding defined in
// types.h. Provides extraction (pieceType, pieceColor), construction
// (makePiece), predicates (isEmpty), color helpers (pawnDirection, homeRow,
// promotionRow), FEN boundary conversion (charToPiece, pieceToChar), and
// piece indexing (pieceIndex overloads: Color+PieceType, Piece, FEN char).

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
// LERF-native helpers (canonical)
// rank 0 = rank 1 (white back rank), rank 7 = rank 8 (black back rank).
// Reference: https://www.chessprogramming.org/Square_Mapping_Considerations
// ---------------------------------------------------------------------------

/// Pawn push direction in LERF squares: +8 for white (NORTH), -8 for black.
constexpr int pawnForward(Color c) {
  return c == Color::WHITE ? 8 : -8;
}

/// Back rank in LERF: rank 0 for white, rank 7 for black.
constexpr int homeRank(Color c) {
  return c == Color::WHITE ? 0 : 7;
}

/// King's starting file in standard chess (e-file = index 4).
/// Used for castling eligibility — a king on any other file cannot castle.
constexpr int KING_START_FILE = 4;

/// Promotion rank in LERF: rank 7 for white, rank 0 for black.
constexpr int promotionRank(Color c) {
  return c == Color::WHITE ? 7 : 0;
}

/// Pawn starting rank in LERF: rank 1 for white, rank 6 for black.
constexpr int pawnStartRank(Color c) {
  return c == Color::WHITE ? 1 : 6;
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
// Piece index — maps a piece to its 0..11 array index.
//
// Index layout: White pieces 0–5 (P N B R Q K), Black pieces 6–11.
// Three interchangeable overloads:
//   pieceIndex(Color, PieceType) — typed enum shorthand.
//   pieceIndex(Piece)            — decompose from the bit-packed encoding.
//   pieceIndex(char)             — FEN char shorthand ('P'=0 .. 'k'=11).
//
// Used for byPiece[] indexing, Zobrist key arrays, and any per-piece
// lookup table. All overloads return the same value for the same piece.
// Use PIECE_IDX_NONE (-1) as sentinel for absent/invalid pieces.
// ---------------------------------------------------------------------------

static constexpr int PIECE_IDX_NONE = -1;

// Typed shorthand: Color + PieceType → 0..11.
// Precondition: pt != PieceType::NONE.
constexpr int pieceIndex(Color c, PieceType pt) {
  return raw(c) * 6 + (raw(pt) - 1);
}

// Decompose from the bit-packed Piece encoding.
// Returns PIECE_IDX_NONE for Piece::NONE.
constexpr int pieceIndex(Piece p) {
  return (p == Piece::NONE) ? PIECE_IDX_NONE
                            : pieceIndex(pieceColor(p), pieceType(p));
}

// FEN char shorthand: uppercase = white, lowercase = black.
// Returns PIECE_IDX_NONE for unrecognized characters.
constexpr int pieceIndex(char c) {
  return pieceIndex(charToPiece(c));
}

constexpr bool isValidPieceIndex(int idx) {
  return idx >= 0 && idx < 12;
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
