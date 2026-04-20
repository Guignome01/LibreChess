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
//   • Bulk: generateMoves(filter) — full position enumeration for search
//     and game-end detection.  Template on MoveList/QSMoveList capacity.
//   • Staged: buildLegalityContext() once, then generateMoves(ctx, filter)
//     reusing the same context (avoids double pin/check computation in
//     staged move pickers).
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
//
// Parallel arrays: `pinnedSq[i]` is the pinned piece's square, `pinRay[i]`
// is its legal-move mask.  `pinned` bitboard is the union of `pinnedSq[]`
// for O(1) early-out in pinRayFor.  Linear scan over ≤8 entries is fast
// in practice because typical positions have 0–1 pins; the scan runs only
// when the early-out bitmap test already confirmed `sq` is pinned.
struct PinData {
  Bitboard pinned;      // bitset of all pinned friendly piece squares (early-out)
  Square pinnedSq[8];   // pinned piece squares
  Bitboard pinRay[8];   // legal-move mask per pinned piece (parallel to pinnedSq)
  int count;            // number of entries in pinnedSq/pinRay
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
// Bulk legal move generation (template: MoveList or QSMoveList)
//
// Self-contained: resolves king, builds legality context, collects moves.
// Template on MoveListBase<N> capacity (218 for MoveList, 128 for QSMoveList).
// ---------------------------------------------------------------------------

template <int N>
void generateMoves(const BitboardSet& bb, const Piece mailbox[],
                   Color color, const PositionState& state,
                   MoveListBase<N>& moves, FilterMode filter);

// ---------------------------------------------------------------------------
// Staged move generation (reuses pre-built LegalityContext)
// ---------------------------------------------------------------------------

// Legal moves matching `filter`, using a pre-built context (clears output).
void generateMoves(const BitboardSet& bb, const Piece mailbox[],
                   Color color, const PositionState& state,
                   const LegalityContext& ctx, MoveList& moves,
                   FilterMode filter);

// Append-mode staged generation — same as above but does NOT clear the
// output list.  Used by MovePicker to append quiet moves after captures in
// a shared MoveList, avoiding a temporary buffer + copy.
void generateMovesAppend(const BitboardSet& bb, const Piece mailbox[],
                         Color color, const PositionState& state,
                         const LegalityContext& ctx, MoveList& moves,
                         FilterMode filter);

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

// Validate with a pre-built LegalityContext (avoids rebuilding pin/check
// masks).  Used by MovePicker to share its cached context across TT/killer/
// countermove validation — each call saves one full buildLegalityContext.
bool isValidMove(const BitboardSet& bb, const Piece mailbox[],
                 Square from, Square to,
                 const PositionState& state, const LegalityContext& ctx);

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
