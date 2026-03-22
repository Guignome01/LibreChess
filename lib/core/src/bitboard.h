#ifndef LIBRECHESS_BITBOARD_H
#define LIBRECHESS_BITBOARD_H

// Bitboard data types and operations for the LibreChess chess library.
//
// Uses Little-Endian Rank-File (LERF) mapping. Includes single-square
// bitboard construction, population count, least-significant bit extraction,
// file/rank masks, directional shifts, and the BitboardSet aggregate.
//
// Reference: https://www.chessprogramming.org/Bitboards

#include <cstdint>

#include "piece.h"
#include "square.h"

namespace LibreChess {

// ---------------------------------------------------------------------------
// Core bitboard type
// ---------------------------------------------------------------------------

using Bitboard = uint64_t;

// ---------------------------------------------------------------------------
// Single-square bitboard
// ---------------------------------------------------------------------------

constexpr Bitboard squareBB(Square sq) {
  return 1ULL << sq;
}

// ---------------------------------------------------------------------------
// Bit manipulation
// ---------------------------------------------------------------------------

// Population count — number of set bits.
inline int popcount(Bitboard bb) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(bb);
#else
  // Portable fallback (Kernighan's method)
  int count = 0;
  while (bb) { bb &= bb - 1; ++count; }
  return count;
#endif
}

// Index of least significant set bit. Undefined if bb == 0.
inline Square lsb(Bitboard bb) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(bb);
#else
  // Portable fallback (De Bruijn)
  static constexpr int DEBRUIJN_TABLE[64] = {
     0,  1, 48,  2, 57, 49, 28,  3, 61, 58, 50, 42, 38, 29, 17,  4,
    62, 55, 59, 36, 53, 51, 43, 22, 45, 39, 33, 30, 24, 18, 12,  5,
    63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21, 44, 32, 23, 11,
    46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19,  9, 13,  8,  7,  6
  };
  constexpr Bitboard DEBRUIJN = 0x03F79D71B4CB0A89ULL;
  return DEBRUIJN_TABLE[((bb & -bb) * DEBRUIJN) >> 58];
#endif
}

// Pop (return and clear) the least significant set bit.
inline Square popLsb(Bitboard& bb) {
  Square sq = lsb(bb);
  bb &= bb - 1;  // clear the LSB
  return sq;
}

// ---------------------------------------------------------------------------
// File and rank masks
// ---------------------------------------------------------------------------

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_B = FILE_A << 1;
constexpr Bitboard FILE_C = FILE_A << 2;
constexpr Bitboard FILE_D = FILE_A << 3;
constexpr Bitboard FILE_E = FILE_A << 4;
constexpr Bitboard FILE_F = FILE_A << 5;
constexpr Bitboard FILE_G = FILE_A << 6;
constexpr Bitboard FILE_H = FILE_A << 7;

constexpr Bitboard RANK_1 = 0x00000000000000FFULL;
constexpr Bitboard RANK_2 = RANK_1 << 8;
constexpr Bitboard RANK_3 = RANK_1 << 16;
constexpr Bitboard RANK_4 = RANK_1 << 24;
constexpr Bitboard RANK_5 = RANK_1 << 32;
constexpr Bitboard RANK_6 = RANK_1 << 40;
constexpr Bitboard RANK_7 = RANK_1 << 48;
constexpr Bitboard RANK_8 = RANK_1 << 56;

// Square-color masks: a1 is a dark square in chess. LERF bit 0 = a1.
constexpr Bitboard DARK_SQUARES = 0xAA55AA55AA55AA55ULL;
constexpr Bitboard LIGHT_SQUARES = ~DARK_SQUARES;

// Anti-wrapping masks for directional shifts.
constexpr Bitboard NOT_FILE_A = ~FILE_A;
constexpr Bitboard NOT_FILE_H = ~FILE_H;

// ---------------------------------------------------------------------------
// Directional shifts (compass rose convention)
// ---------------------------------------------------------------------------
// North = toward rank 8 (+8), South = toward rank 1 (-8).
// East = toward file h (+1), West = toward file a (-1).

constexpr Bitboard shiftNorth(Bitboard bb) { return bb << 8; }
constexpr Bitboard shiftSouth(Bitboard bb) { return bb >> 8; }
constexpr Bitboard shiftEast(Bitboard bb)  { return (bb & NOT_FILE_H) << 1; }
constexpr Bitboard shiftWest(Bitboard bb)  { return (bb & NOT_FILE_A) >> 1; }
constexpr Bitboard shiftNE(Bitboard bb)    { return (bb & NOT_FILE_H) << 9; }
constexpr Bitboard shiftNW(Bitboard bb)    { return (bb & NOT_FILE_A) << 7; }
constexpr Bitboard shiftSE(Bitboard bb)    { return (bb & NOT_FILE_H) >> 7; }
constexpr Bitboard shiftSW(Bitboard bb)    { return (bb & NOT_FILE_A) >> 9; }

// ---------------------------------------------------------------------------
// Rank/file mask by index
// ---------------------------------------------------------------------------

constexpr Bitboard fileBB(int file) { return FILE_A << file; }
constexpr Bitboard rankBB(int rank) { return RANK_1 << (rank * 8); }

// ---------------------------------------------------------------------------
// BitboardSet — the bitboard position representation
// ---------------------------------------------------------------------------
// Stores 12 piece bitboards (one per piece-type x color), 2 color aggregate
// bitboards, and a combined occupancy. All are kept in sync by the mutation
// helpers below.
//
// The Piece mailbox (flat 64-element array) is stored separately in
// Position, not here. BitboardSet is pure bitboard state.

static constexpr int NUM_PIECE_BOARDS = 12;

struct BitboardSet {
  Bitboard byPiece[NUM_PIECE_BOARDS] = {};  // indexed by pieceZobristIndex(piece)
  Bitboard byColor[2] = {};                 // WHITE = 0, BLACK = 1
  Bitboard occupied = 0;

  // Place a piece on an empty square.
  // Precondition: piece != NONE (idx must be 0–11).
  void setPiece(Square sq, Piece piece) {
    Bitboard bit = squareBB(sq);
    int idx = piece::pieceZobristIndex(piece);
    Color c = piece::pieceColor(piece);
    byPiece[idx] |= bit;
    byColor[raw(c)] |= bit;
    occupied |= bit;
  }

  // Remove a piece from an occupied square.
  // Precondition: piece != NONE (idx must be 0–11).
  void removePiece(Square sq, Piece piece) {
    Bitboard bit = squareBB(sq);
    int idx = piece::pieceZobristIndex(piece);
    Color c = piece::pieceColor(piece);
    byPiece[idx] ^= bit;
    byColor[raw(c)] ^= bit;
    occupied ^= bit;
  }

  // Move a piece from one square to another (both must be consistent:
  // `from` occupied by `piece`, `to` empty).
  // Precondition: piece != NONE (idx must be 0–11).
  void movePiece(Square from, Square to, Piece piece) {
    Bitboard fromTo = squareBB(from) | squareBB(to);
    int idx = piece::pieceZobristIndex(piece);
    Color c = piece::pieceColor(piece);
    byPiece[idx] ^= fromTo;
    byColor[raw(c)] ^= fromTo;
    occupied ^= fromTo;
  }

  // Identify which piece sits on a square by scanning all 12 bitboards.
  // Returns Piece::NONE if the square is empty.
  // This is O(12) — use the mailbox for hot-path lookups instead.
  Piece pieceOn(Square sq) const {
    Bitboard bit = squareBB(sq);
    if (!(occupied & bit)) return Piece::NONE;
    for (int i = 0; i < NUM_PIECE_BOARDS; ++i) {
      if (byPiece[i] & bit) {
        // Reverse the Zobrist index back to a Piece.
        // Index 0-5 = white PAWN..KING, 6-11 = black PAWN..KING.
        Color c = (i < 6) ? Color::WHITE : Color::BLACK;
        auto t = static_cast<PieceType>((i % 6) + 1);
        return piece::makePiece(c, t);
      }
    }
    return Piece::NONE;  // should not reach here if occupied is correct
  }

  // Reset all bitboards to empty.
  void clear() {
    for (int i = 0; i < NUM_PIECE_BOARDS; ++i) byPiece[i] = 0;
    byColor[0] = byColor[1] = 0;
    occupied = 0;
  }
};

}  // namespace LibreChess

#endif  // LIBRECHESS_BITBOARD_H
