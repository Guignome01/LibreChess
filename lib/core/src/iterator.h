#ifndef LIBRECHESS_ITERATOR_H
#define LIBRECHESS_ITERATOR_H

// Board iteration helpers — bitboard-based traversal.
//
// forEachSquare: iterates all 64 squares reading from the mailbox.
// forEachPiece:  iterates only occupied squares via popLsb serialization.
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

}  // namespace iterator
}  // namespace LibreChess

#endif  // CORE_ITERATOR_H
