#ifndef LIBRECHESS_ZOBRIST_H
#define LIBRECHESS_ZOBRIST_H

// Zobrist hashing for position comparison and threefold repetition detection.
// Keys are generated at compile time via a deterministic xorshift64 PRNG.
// Square indexing uses LERF (a1=0, h8=63), matching bitboard.h.

#include <stdint.h>

#include "bitboard.h"
#include "types.h"

#ifndef PROGMEM
#define PROGMEM
#endif

namespace LibreChess {
namespace zobrist {

// ---------------------------------------------------------------------------
// Constexpr key generation (deterministic xorshift64, seed=0x12345678ABCDEF01)
// ---------------------------------------------------------------------------

constexpr uint64_t xorshift64(uint64_t s) {
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  return s;
}

struct Keys {
  uint64_t pieces[12][64]{};   // P=0 N=1 B=2 R=3 Q=4 K=5 p=6 n=7 b=8 r=9 q=10 k=11
  uint64_t castling[16]{};     // indexed by castling rights bitmask
  uint64_t enPassant[8]{};     // indexed by file (0–7)
  uint64_t sideToMove{};

  constexpr Keys() {
    uint64_t s = 0x12345678ABCDEF01ULL;
    for (int p = 0; p < 12; ++p)
      for (int sq = 0; sq < 64; ++sq) {
        s = xorshift64(s);
        pieces[p][sq] = s;
      }
    for (int i = 0; i < 16; ++i) {
      s = xorshift64(s);
      castling[i] = s;
    }
    for (int i = 0; i < 8; ++i) {
      s = xorshift64(s);
      enPassant[i] = s;
    }
    s = xorshift64(s);
    sideToMove = s;
  }
};

static constexpr Keys KEYS PROGMEM = Keys();

// ---------------------------------------------------------------------------
// Full-board hash computation (for initialization and debug verification).
// Uses BitboardSet + mailbox with LERF square indexing.
// The EP legality check (hasLegalEnPassantCapture) is defined in movegen:: —
// this function is NOT inline because it needs the full movegen:: definition.
// ---------------------------------------------------------------------------

uint64_t computeHash(const BitboardSet& bb, const Piece mailbox[],
                     Color turn, const PositionState& state);

// Compute a pawn-only Zobrist hash.  XORs piece keys for all white and black
// pawns — used as the lookup key for the pawn hash table.  Pawn structures
// change infrequently, so caching yields ~95%+ hit rate in typical searches.
uint64_t computePawnHash(const BitboardSet& bb);

}  // namespace zobrist
}  // namespace LibreChess

#endif  // LIBRECHESS_ZOBRIST_H
