#include "zobrist.h"

#include "iterator.h"
#include "movegen.h"

namespace LibreChess {
namespace zobrist {

uint64_t computeHash(const BitboardSet& bb, const Piece mailbox[],
                     Color turn, const PositionState& state) {
  uint64_t hash = 0;

  // Iterate occupied squares via bitboard serialization for LERF indexing.
  Bitboard occ = bb.occupied;
  while (occ) {
    Square sq = popLsb(occ);
    int idx = piece::pieceZobristIndex(mailbox[sq]);
    hash ^= KEYS.pieces[idx][sq];
  }

  hash ^= KEYS.castling[state.castlingRights];

  if (movegen::hasLegalEnPassantCapture(bb, mailbox, turn, state))
    hash ^= KEYS.enPassant[state.epCol];

  if (turn == Color::BLACK) hash ^= KEYS.sideToMove;

  return hash;
}

// ---------------------------------------------------------------------------
// Pawn-only Zobrist hash — XOR of piece keys for all pawns on the board.
// Used as the lookup key for the pawn hash table.
// ---------------------------------------------------------------------------

uint64_t computePawnHash(const BitboardSet& bb) {
  uint64_t hash = 0;

  Bitboard wp = bb.byPiece[0];   // White pawns (pieceZobristIndex = 0)
  while (wp) {
    Square sq = popLsb(wp);
    hash ^= KEYS.pieces[0][sq];
  }

  Bitboard bp = bb.byPiece[6];   // Black pawns (pieceZobristIndex = 6)
  while (bp) {
    Square sq = popLsb(bp);
    hash ^= KEYS.pieces[6][sq];
  }

  return hash;
}

}  // namespace zobrist
}  // namespace LibreChess
