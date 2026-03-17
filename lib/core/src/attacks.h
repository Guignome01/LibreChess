#ifndef LIBRECHESS_ATTACKS_H
#define LIBRECHESS_ATTACKS_H

// ---------------------------------------------------------------------------
// Precomputed attack tables and slider attack functions.
//
// Leaper attacks (knight, king, pawn) are stored in lookup tables —
// one Bitboard per square, computed once at startup via init().
//
// Slider attacks (rook, bishop, queen) use two O(1) techniques:
//   - Rank attacks: first-rank lookup table (512 bytes, indexed by
//     file + 6-bit inner occupancy).
//   - File/diagonal/anti-diagonal attacks: Hyperbola Quintessence —
//     branchless o^(o-2r) subtraction trick with byte-swap for the
//     negative ray direction. Diagonal masks (DIAG_MASK[64],
//     ANTI_DIAG_MASK[64]) are precomputed at startup.
//
// References:
//   https://www.chessprogramming.org/Bitboards
//   https://www.chessprogramming.org/Hyperbola_Quintessence
// ---------------------------------------------------------------------------

#include "bitboard.h"

namespace LibreChess {
namespace attacks {

// ---------------------------------------------------------------------------
// Precomputed leaper tables (initialized by init())
// ---------------------------------------------------------------------------

extern Bitboard KNIGHT[64];
extern Bitboard KING[64];
extern Bitboard PAWN[2][64];  // [0] = WHITE, [1] = BLACK

// Must be called once before using any attack function.
// Initializes leaper tables (knight, king, pawn) and slider masks.
// Safe to call multiple times (idempotent).
void init();

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

// Full line through s1 and s2, extending to both board edges, inclusive of
// both endpoints. Returns 0 if not colinear.
Bitboard line(Square s1, Square s2);

// ---------------------------------------------------------------------------
// Square attack detection
// ---------------------------------------------------------------------------

// Test whether any enemy piece attacks `sq`.  Uses precomputed leaper tables
// and slider ray functions — early-returns on the first attacker found.
// `defendingColor` is the color of the piece ON the square (the "defender");
// the attacker is the opposite color.
bool isSquareUnderAttack(const LibreChess::BitboardSet& bb, Square sq,
                         Color defendingColor);

// Row/col overload for callers using board coordinates.
inline bool isSquareUnderAttack(const LibreChess::BitboardSet& bb,
                                int row, int col, Color defendingColor) {
  return isSquareUnderAttack(bb, LibreChess::squareOf(row, col), defendingColor);
}

}  // namespace attacks
}  // namespace LibreChess

#endif  // CORE_ATTACKS_H
