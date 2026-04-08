#ifndef LIBRECHESS_MOVE_H
#define LIBRECHESS_MOVE_H

// Move representation for the LibreChess chess library.
//
// Defines the compact Move struct (from/to/flags), move flag constants,
// ScoredMove (for move ordering), MoveList (fixed-capacity generation
// output), and MoveResult (UI feedback after a completed move).

#include <cstdint>

#include "bitboard.h"
#include "types.h"

namespace LibreChess {

// ---------------------------------------------------------------------------
// Move flag bits — packed into a single uint8_t.
// ---------------------------------------------------------------------------

static constexpr uint8_t MOVE_CAPTURE   = 1 << 0;
static constexpr uint8_t MOVE_EP        = 1 << 1;
static constexpr uint8_t MOVE_CASTLING  = 1 << 2;
static constexpr uint8_t MOVE_PROMOTION = 1 << 3;
// Bits 4-5: promotion piece type (only meaningful when MOVE_PROMOTION is set).
static constexpr int MOVE_PROMO_SHIFT = 4;

// ---------------------------------------------------------------------------
// Compact move: from-square, to-square, flags byte.  3 bytes total.
// ---------------------------------------------------------------------------

struct Move {
  uint8_t from;
  uint8_t to;
  uint8_t flags;

  constexpr Move() : from(0), to(0), flags(0) {}
  constexpr Move(uint8_t f, uint8_t t, uint8_t fl = 0) : from(f), to(t), flags(fl) {}

  constexpr bool isCapture()   const { return flags & MOVE_CAPTURE; }
  constexpr bool isEP()        const { return flags & MOVE_EP; }
  constexpr bool isCastling()  const { return flags & MOVE_CASTLING; }
  constexpr bool isPromotion() const { return flags & MOVE_PROMOTION; }

  // Promotion piece encoding: 0=Knight, 1=Bishop, 2=Rook, 3=Queen.
  constexpr uint8_t promoIndex() const { return (flags >> MOVE_PROMO_SHIFT) & 0x03; }

  // Build promotion flags: MOVE_PROMOTION | (index << MOVE_PROMO_SHIFT).
  static constexpr uint8_t promoFlags(uint8_t index) {
    return MOVE_PROMOTION | (index << MOVE_PROMO_SHIFT);
  }

  // Map PieceType (KNIGHT..QUEEN) to 2-bit promotion index.
  static constexpr uint8_t promoIndexFromType(PieceType pt) {
    return static_cast<uint8_t>(pt) - static_cast<uint8_t>(PieceType::KNIGHT);
  }

  // Map 2-bit promotion index back to PieceType.
  static constexpr PieceType promoTypeFromIndex(uint8_t idx) {
    return static_cast<PieceType>(idx + static_cast<uint8_t>(PieceType::KNIGHT));
  }

  constexpr bool operator==(const Move& o) const {
    return from == o.from && to == o.to && flags == o.flags;
  }
};

// ---------------------------------------------------------------------------
// Move with an attached score for move ordering (MVV-LVA, killers, history).
// ---------------------------------------------------------------------------

struct ScoredMove {
  Move move;
  int16_t score = 0;
};

// ---------------------------------------------------------------------------
// Unified move list — used for both per-piece and full-position generation.
//
// MoveListBase<N> is a fixed-capacity buffer of N moves.  The default
// MoveList (N = MAX_MOVES = 218) covers the theoretical maximum legal
// moves in any position.  Smaller specialisations (e.g. QS_MAX_MOVES)
// reduce per-ply stack usage in the quiescence search.
// ---------------------------------------------------------------------------

static constexpr int MAX_MOVES = 218;

template <int N = MAX_MOVES>
struct MoveListBase {
  Move moves[N];
  int count = 0;

  void add(Move m) {
    if (count < N) moves[count++] = m;
  }
  void clear() { count = 0; }
};

// Standard full-capacity move list used by movegen and game layers.
using MoveList = MoveListBase<MAX_MOVES>;

// Compact move list for quiescence search — 128 moves covers all realistic
// positions (captures, promotions, and check evasions) while saving 450
// bytes/ply vs the full 218-capacity list.  Positions with >128 legal moves
// are synthetic constructions that never arise in practical play.
static constexpr int QS_MAX_MOVES = 128;
using QSMoveList = MoveListBase<QS_MAX_MOVES>;

// ---------------------------------------------------------------------------
// MoveResult flag bits — packed into a single uint8_t.
// ---------------------------------------------------------------------------

static constexpr uint8_t MR_VALID     = 1 << 0;
static constexpr uint8_t MR_CAPTURE   = 1 << 1;
static constexpr uint8_t MR_EP        = 1 << 2;
static constexpr uint8_t MR_CASTLING  = 1 << 3;
static constexpr uint8_t MR_PROMOTION = 1 << 4;
static constexpr uint8_t MR_CHECK     = 1 << 5;

// ---------------------------------------------------------------------------
// MoveResult — carries all information from a completed move so the
// hardware/UI layer can produce LED feedback, sounds, and network updates
// without re-reading game state.
// ---------------------------------------------------------------------------

struct MoveResult {
  uint8_t flags;            // packed booleans (MR_VALID | MR_CAPTURE | ...)
  Square epCapturedSq;      // Square of captured en passant pawn (SQ_NONE if N/A)
  Piece promotedTo;         // Piece the pawn became (Piece::NONE if N/A)
  GameResult gameResult;    // GameResult::IN_PROGRESS if game continues
  char winnerColor;         // 'w', 'b', 'd' (draw), ' ' (in progress)

  constexpr bool valid()       const { return flags & MR_VALID; }
  constexpr bool isCapture()   const { return flags & MR_CAPTURE; }
  constexpr bool isEnPassant() const { return flags & MR_EP; }
  constexpr bool isCastling()  const { return flags & MR_CASTLING; }
  constexpr bool isPromotion() const { return flags & MR_PROMOTION; }
  constexpr bool isCheck()     const { return flags & MR_CHECK; }
};

// Factory for an invalid (rejected) MoveResult.
constexpr MoveResult invalidMoveResult() {
  return {0, SQ_NONE, Piece::NONE, GameResult::IN_PROGRESS, ' '};
}

// ---------------------------------------------------------------------------
// MoveEntry flag bits.
//
// MoveEntry reuses MR_* constants directly (MR_CAPTURE through MR_CHECK).
// MR_VALID (bit 0) is MoveResult-only and excluded by ME_FLAG_MASK.
// ---------------------------------------------------------------------------

// Mask for copying common flags from MoveResult to MoveEntry.
static constexpr uint8_t ME_FLAG_MASK =
    MR_CAPTURE | MR_EP | MR_CASTLING | MR_PROMOTION | MR_CHECK;

// ---------------------------------------------------------------------------
// MoveEntry — a single move record in the game history.
// Stores enough information to query the move log, support undo/redo,
// and reconstruct board state.
// ---------------------------------------------------------------------------
struct MoveEntry {
  Square from;
  Square to;
  Piece piece;           // piece that moved (original, before any promotion)
  Piece captured;        // piece captured (Piece::NONE if none)
  Piece promotion;       // piece promoted to (Piece::NONE if not a promotion)
  uint8_t flags;         // packed booleans (MR_CAPTURE | MR_EP | ...)
  Square epCapturedSq;   // en passant captured pawn square (SQ_NONE if N/A)
  PositionState prevState;  // position state before the move (enables undo)

  constexpr bool isCapture()   const { return flags & MR_CAPTURE; }
  constexpr bool isEnPassant() const { return flags & MR_EP; }
  constexpr bool isCastling()  const { return flags & MR_CASTLING; }
  constexpr bool isPromotion() const { return flags & MR_PROMOTION; }
  constexpr bool isCheck()     const { return flags & MR_CHECK; }

  // Build a MoveEntry from move squares and result.
  static MoveEntry build(Square from, Square to,
                         Piece piece, Piece targetPiece,
                         const MoveResult& result,
                         const PositionState& prevState) {
    using namespace LibreChess::piece;
    Piece captured = Piece::NONE;
    if (result.isEnPassant())
      captured = makePiece(~pieceColor(piece), PieceType::PAWN);
    else if (result.isCapture())
      captured = targetPiece;

    MoveEntry entry;
    entry.from = from;
    entry.to = to;
    entry.piece = piece;
    entry.captured = captured;
    entry.promotion = result.isPromotion() ? result.promotedTo : Piece::NONE;
    entry.flags = result.flags & ME_FLAG_MASK;
    entry.epCapturedSq = result.epCapturedSq;
    entry.prevState = prevState;
    return entry;
  }
};

}  // namespace LibreChess

#endif  // LIBRECHESS_MOVE_H
