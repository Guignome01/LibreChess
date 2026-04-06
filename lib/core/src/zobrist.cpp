#include "zobrist.h"
#include "piece.h"

namespace LibreChess {
namespace zobrist {

uint64_t computeHash(const BitboardSet& bb, const Piece mailbox[],
                     Color turn, const PositionState& state, bool epLegal) {
  uint64_t hash = 0;

  // Iterate occupied squares via bitboard serialization for LERF indexing.
  Bitboard occ = bb.occupied;
  while (occ) {
    Square sq = popLsb(occ);
    int idx = piece::pieceIndex(mailbox[sq]);
    hash ^= KEYS.pieces[idx][sq];
  }

  hash ^= KEYS.castling[state.castlingRights];

  if (epLegal)
    hash ^= KEYS.enPassant[fileOf(state.epSquare)];

  if (turn == Color::BLACK) hash ^= KEYS.sideToMove;

  return hash;
}

// ---------------------------------------------------------------------------
// Pawn-only Zobrist hash — XOR of piece keys for all pawns on the board.
// Used as the lookup key for the pawn hash table.
// ---------------------------------------------------------------------------

uint64_t computePawnHash(const BitboardSet& bb) {
  uint64_t hash = 0;

  Bitboard wp = bb.byPiece[piece::pieceIndex('P')];
  while (wp) {
    Square sq = popLsb(wp);
    hash ^= KEYS.pieces[piece::pieceIndex('P')][sq];
  }

  Bitboard bp = bb.byPiece[piece::pieceIndex('p')];
  while (bp) {
    Square sq = popLsb(bp);
    hash ^= KEYS.pieces[piece::pieceIndex('p')][sq];
  }

  return hash;
}

}  // namespace zobrist
}  // namespace LibreChess
