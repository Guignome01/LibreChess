#ifndef LIBRECHESS_ITERATOR_H
#define LIBRECHESS_ITERATOR_H

// Board iteration helpers — bitboard-based traversal.
//
// forEachSquare: iterates all 64 squares reading from the mailbox.
// forEachPiece:  iterates only occupied squares via popLsb serialization.
// somePiece:     like forEachPiece but with early-exit on true.
// findPiece:     locates all instances of a specific piece via its bitboard.
//
// All callbacks receive (int row, int col, Piece piece) for firmware compatibility.

#include "bitboard.h"
#include "piece.h"
#include "types.h"

namespace LibreChess {
namespace iterator {

// Iterate all 64 squares in board order (row 0 col 0 → row 7 col 7).
// Callback: fn(int row, int col, Piece piece)
template <typename Fn>
inline void forEachSquare(const Piece mailbox[], Fn&& fn) {
  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col)
      fn(row, col, mailbox[squareOf(row, col)]);
}

// Iterate only occupied squares via bitboard serialization.
// Callback: fn(int row, int col, Piece piece)
template <typename Fn>
inline void forEachPiece(const BitboardSet& bb,
                         const Piece mailbox[], Fn&& fn) {
  Bitboard occ = bb.occupied;
  while (occ) {
    Square sq = popLsb(occ);
    fn(rowOf(sq), colOf(sq), mailbox[sq]);
  }
}

// Iterate occupied squares with early exit — returns true if fn returns true.
// Callback: fn(int row, int col, Piece piece) → bool
template <typename Fn>
inline bool somePiece(const BitboardSet& bb,
                      const Piece mailbox[], Fn&& fn) {
  Bitboard occ = bb.occupied;
  while (occ) {
    Square sq = popLsb(occ);
    if (fn(rowOf(sq), colOf(sq), mailbox[sq]))
      return true;
  }
  return false;
}

// Find all instances of a specific piece.  Returns count found.
// Fills positions[][2] with [row, col] pairs, up to maxPositions.
// Returns 0 immediately if target is Piece::NONE.
inline int findPiece(const BitboardSet& bb, Piece target,
                     int positions[][2], int maxPositions) {
  int idx = piece::pieceZobristIndex(target);
  if (!piece::isValidZobristIndex(idx)) return 0;
  Bitboard pieces = bb.byPiece[idx];
  int found = 0;
  while (pieces && found < maxPositions) {
    Square sq = popLsb(pieces);
    positions[found][0] = rowOf(sq);
    positions[found][1] = colOf(sq);
    ++found;
  }
  return found;
}

}  // namespace iterator
}  // namespace LibreChess

#endif  // CORE_ITERATOR_H
