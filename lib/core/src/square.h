#ifndef LIBRECHESS_SQUARE_H
#define LIBRECHESS_SQUARE_H

// Square type and coordinate conversion for the LibreChess chess library.
//
// Uses Little-Endian Rank-File (LERF) mapping — the standard convention
// from the Chess Programming Wiki:
//   a1 = 0, b1 = 1, ..., h1 = 7,
//   a2 = 8, ..., h8 = 63.
//
// Row/col convention: row 0 = rank 8 (black back rank), col 0 = file a.
// Conversion: squareOf(row, col) = (7 - row) * 8 + col.
//
// Reference: https://www.chessprogramming.org/Bitboards

#include <cstdint>

namespace LibreChess {

// ---------------------------------------------------------------------------
// Square type
// ---------------------------------------------------------------------------

using Square = int;  // 0-63 (LERF), or SQ_NONE

constexpr Square SQ_A1 = 0;
constexpr Square SQ_H1 = 7;
constexpr Square SQ_A8 = 56;
constexpr Square SQ_H8 = 63;
constexpr Square SQ_NONE = -1;

// ---------------------------------------------------------------------------
// Square <-> row/col conversion
// ---------------------------------------------------------------------------
// Row 0 = rank 8 (black back rank), col 0 = file a.
// This matches the existing coordinate system used throughout the project.

constexpr Square squareOf(int row, int col) {
  return (7 - row) * 8 + col;
}

constexpr int rowOf(Square sq) {
  return 7 - (sq >> 3);  // sq / 8 gives rank index; 7 - rank = row
}

constexpr int colOf(Square sq) {
  return sq & 7;  // sq % 8
}

}  // namespace LibreChess

#endif  // LIBRECHESS_SQUARE_H
