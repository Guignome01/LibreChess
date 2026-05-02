#ifndef LIBRECHESS_ATTACKS_H
#define LIBRECHESS_ATTACKS_H

// ---------------------------------------------------------------------------
// Precomputed attack tables and slider attack functions.
//
// Leaper attacks (knight, king, pawn) are stored in const lookup tables —
// one Bitboard per square, computed at compile time via constexpr builders
// and placed in .rodata (Flash on ESP32) instead of .bss (RAM).
//
// Slider attacks (rook, bishop, queen) use two O(1) techniques:
//   - Rank attacks: first-rank lookup table (512 bytes, indexed by
//     file + 6-bit inner occupancy).
//   - File/diagonal/anti-diagonal attacks: Hyperbola Quintessence —
//     branchless o^(o-2r) subtraction trick with byte-swap for the
//     negative ray direction. Diagonal masks (DIAG[15],
//     ANTI_DIAG[15]) are precomputed at compile time.
//
// References:
//   https://www.chessprogramming.org/Bitboards
//   https://www.chessprogramming.org/Hyperbola_Quintessence
// ---------------------------------------------------------------------------

#include "bitboard.h"
#include "move.h"

namespace LibreChess {
namespace attacks {

// ---------------------------------------------------------------------------
// Constexpr table wrapper structs — support operator[] for transparent
// array-like access while allowing extern const definitions in one TU.
// ---------------------------------------------------------------------------

struct Table64 {
  Bitboard data[64] = {};
  constexpr const Bitboard& operator[](int i) const { return data[i]; }
};

struct PawnTable {
  Bitboard data[2][64] = {};
  constexpr const Bitboard* operator[](int i) const { return data[i]; }
};

// ---------------------------------------------------------------------------
// Precomputed leaper tables (const, placed in .rodata / Flash)
//
// Defined once in attacks.cpp via constexpr builders.
// Access: KNIGHT[sq], KING[sq], PAWN[color][sq] — same API as raw arrays.
// ---------------------------------------------------------------------------

extern const Table64   KNIGHT;
extern const Table64   KING;
extern const PawnTable PAWN;

// No-op retained for backward compatibility (tables are const-initialized).
inline void init() {}

// ---------------------------------------------------------------------------
// Slider attack functions
// ---------------------------------------------------------------------------

// Rook attacks from `sq` given `occupied` — walks N, S, E, W rays.
Bitboard rook(Square sq, Bitboard occupied);

// Bishop attacks from `sq` given `occupied` — walks NE, NW, SE, SW rays.
Bitboard bishop(Square sq, Bitboard occupied);

// Queen attacks = rook + bishop combined.
inline Bitboard queen(Square sq, Bitboard occupied) {
  return rook(sq, occupied) | bishop(sq, occupied);
}

// ---------------------------------------------------------------------------
// X-ray attack functions (for pin detection)
// ---------------------------------------------------------------------------

// Rook attacks from `sq` shooting through exactly one layer of friendly
// pieces. Returns the extended attack set after removing friendly blockers
// from occupancy. ANDing with enemy rook/queen bitboards gives potential
// pinners.
Bitboard xrayRook(Bitboard occupied, Bitboard friendly, Square sq);

// Bishop attacks from `sq` shooting through exactly one layer of friendly
// pieces.
Bitboard xrayBishop(Bitboard occupied, Bitboard friendly, Square sq);

// ---------------------------------------------------------------------------
// Attacked-by bitboards (on-demand evaluation helper)
// ---------------------------------------------------------------------------
// Computes per-piece-type and per-color attack maps from scratch.
// Called once per evaluation, not per move.
//
// PieceType::NONE (index 0) slots are unused; indices 1–6 for PAWN..KING.

struct AttackInfo {
  Bitboard byPiece[2][7];  // [color][pieceType]
  Bitboard byColor[2];     // [color] — union of all piece attacks
  Bitboard allAttacks;     // union of both colors
};

// Build full attack maps for both colors from the given position.
// Requires init() to have been called.
AttackInfo computeAll(const LibreChess::BitboardSet& bb);

// ---------------------------------------------------------------------------
// Ray geometry functions
// ---------------------------------------------------------------------------

// Squares strictly between s1 and s2 on the same rank, file, or diagonal
// (exclusive of both endpoints). Returns 0 if not colinear.
Bitboard between(Square s1, Square s2);

// ---------------------------------------------------------------------------
// Square attack detection
// ---------------------------------------------------------------------------

// Returns a bitboard of ALL squares from which `attackingColor` pieces attack
// `sq`.  Uses precomputed leaper tables and slider ray functions.  Useful for
// check detection (popcount gives checker count, lsb gives checker square).
// Reference: https://www.chessprogramming.org/Square_Attacked_By
Bitboard attackersOfSquare(const LibreChess::BitboardSet& bb, Square sq,
                           Color attackingColor);

// Test whether any enemy piece attacks `sq`.  Thin wrapper around
// attackersOfSquare() — returns true when the attacker set is non-empty.
// `defendingColor` is the color of the piece ON the square (the "defender");
// the attacker is the opposite color.
bool isSquareUnderAttack(const LibreChess::BitboardSet& bb, Square sq,
                         Color defendingColor);

// Occupancy-aware variant of isSquareUnderAttack.  Slider queries use the
// provided `occupancy` instead of bb.occupied, allowing callers to model
// board changes (e.g. removing the king from its origin square) without
// copying the entire BitboardSet.  Leaper checks are unaffected by occupancy.
// Used by movegen's king-move legality fast path to avoid a 120-byte
// BitboardSet copy per candidate king move.
bool isSquareUnderAttackOcc(const LibreChess::BitboardSet& bb, Square sq,
                            Color defendingColor, Bitboard occupancy);

// ---------------------------------------------------------------------------
// Static Exchange Evaluation (SEE)
//
// Determines whether a capture on a target square results in a material
// gain or loss after all recaptures are resolved.  Uses the swap algorithm:
// each side captures with its least valuable attacker, building a gain list,
// then walks the list backwards with negamax logic to determine the final
// value.
//
// Returns a score in centipawns (positive = capturing side gains material).
// The move must be a capture (asserted by the caller).
//
// Used for:
//   (a) Pruning losing captures in quiescence search.
//   (b) Demoting losing captures below quiet moves in move ordering.
//
// Reference: https://www.chessprogramming.org/Static_Exchange_Evaluation
// ---------------------------------------------------------------------------

int see(const BitboardSet& bb, const Piece mailbox[], Move m);

}  // namespace attacks
}  // namespace LibreChess

#endif  // CORE_ATTACKS_H
