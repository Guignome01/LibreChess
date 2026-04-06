#ifndef LIBRECHESS_MOVEGEN_H
#define LIBRECHESS_MOVEGEN_H

// ---------------------------------------------------------------------------
// Legal move generation — pin+check mask filtered.
//
// All functions are stateless: board representation (BitboardSet + mailbox)
// and position state are passed in as parameters.
//
// Three generation modes:
//   • Per-piece: getPossibleMoves() — legal moves for one piece (LED hints).
//   • Bulk: generateAllMoves() / generateCaptures() — full position
//     enumeration for search and game-end detection.
//   • Staged: buildLegalityContext() once, then generateCaptures() and
//     generateQuiets() reusing the same context (avoids double pin/check
//     computation in staged move pickers).
//
// Also provides single-move validation (isValidMove) and the EP legality
// query used by Zobrist hashing (hasLegalEnPassantCapture).
//
// Reference: https://www.chessprogramming.org/Move_Generation#Staged_move_generation
// ---------------------------------------------------------------------------

#include "attacks.h"
#include "bitboard.h"
#include "move.h"
#include "types.h"

namespace LibreChess {
namespace movegen {

// ---------------------------------------------------------------------------
// Filter mode — selects which move categories to emit.
//
// Used by internal move enumeration to support staged generation (captures
// first, then quiets) without duplicating filtering logic.
// ---------------------------------------------------------------------------

enum class FilterMode {
  ALL             = 0,  // All legal moves
  CAPTURES_PROMOS = 1,  // Captures and promotions only (quiescence)
  QUIETS          = 2   // Quiet non-capture non-promotion moves only
};

// ---------------------------------------------------------------------------
// Legality context — pin + check data built once per position.
//
// Shared across staged generation phases (captures, then quiets) so the
// expensive pin detection and check mask computation happens only once.
// ---------------------------------------------------------------------------

// Up to 8 absolute pins possible (4 orthogonal + 4 diagonal rays from king).
struct PinData {
  Bitboard pinned;      // bitset of all pinned friendly piece squares
  Bitboard pinRay[8];   // legal-move mask per pinned piece (king→pinner ray, inclusive)
  Square pinnedSq[8];   // the pinned piece square corresponding to pinRay[i]
  int count;            // number of recorded pins (≤ 8)
};

// Pre-computed legality context: checker info + pin data + check mask.
// Built once per position, shared by staged capture/quiet generation.
struct LegalityContext {
  Square kingSq;
  Bitboard checkMask;   // ~0ULL if not in check, between(king,checker)|checker if single check
  PinData pinData;
  int checkerCount;     // 0 = not in check, 1 = single check, 2+ = double check
};

// Build the legality context for `color` in the given position.
LegalityContext buildLegalityContext(const BitboardSet& bb, Color color,
                                    Square kingSq);

// ---------------------------------------------------------------------------
// Per-piece legal move generation
// ---------------------------------------------------------------------------

// Returns only legal moves for the piece at the given square.
// Uses pin+check mask filtering with copy-make fallback for king and EP moves.
void getPossibleMoves(const BitboardSet& bb, const Piece mailbox[],
                      Square sq, const PositionState& state,
                      MoveList& moves);

// ---------------------------------------------------------------------------
// Bulk legal move generation
// ---------------------------------------------------------------------------

// All legal moves for `color`.  Computes pin+check masks once, then iterates
// all friendly pieces via bitboard serialization.
void generateAllMoves(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      MoveList& moves);

// Captures and capture-promotions only (for quiescence search).
// Self-contained: builds its own LegalityContext internally.
void generateCaptures(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      MoveList& moves);

// ---------------------------------------------------------------------------
// Staged move generation (reuses pre-built LegalityContext)
// ---------------------------------------------------------------------------

// Captures and capture-promotions only, using a pre-built context.
void generateCaptures(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      const LegalityContext& ctx, MoveList& moves);

// Quiet (non-capture) moves only, using a pre-built context.
// Excludes captures and capture-promotions — only quiet moves and
// underpromotions.
void generateQuiets(const BitboardSet& bb, const Piece mailbox[],
                    Color color, const PositionState& state,
                    const LegalityContext& ctx, MoveList& moves);

// ---------------------------------------------------------------------------
// Single-move validation
// ---------------------------------------------------------------------------

// Validate that a move from `from` to `to` is legal.
// Finds king square internally.
bool isValidMove(const BitboardSet& bb, const Piece mailbox[],
                 Square from, Square to,
                 const PositionState& state);

// Validate with pre-found king square (avoids redundant king search).
bool isValidMove(const BitboardSet& bb, const Piece mailbox[],
                 Square from, Square to,
                 const PositionState& state, Square kingSq);

// ---------------------------------------------------------------------------
// Utility queries
// ---------------------------------------------------------------------------

// Does `color` have at least one legal move?
// Used by Position::isCheckmate / Position::isStalemate.
bool hasAnyLegalMove(const BitboardSet& bb, const Piece mailbox[],
                     Color color, const PositionState& state);

// Does `sideToMove` have a legal EP capture?
// Used by Zobrist hashing — only hash the EP file when the capture is legal.
bool hasLegalEnPassantCapture(const BitboardSet& bb, const Piece mailbox[],
                              Color sideToMove, const PositionState& state);

}  // namespace movegen
}  // namespace LibreChess

#endif  // LIBRECHESS_MOVEGEN_H
