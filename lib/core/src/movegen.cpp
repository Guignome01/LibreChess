#include "movegen.h"

#include "utils.h"

namespace LibreChess {
namespace movegen {

using namespace LibreChess;

// ---------------------------------------------------------------------------
// File-local helpers for pin-aware legal move generation
// ---------------------------------------------------------------------------

namespace {

// Shared functor for move-collection call sites (collectLegalMoves,
// generateMovesAppend, getPossibleMoves).  Using one concrete type instead
// of per-site lambdas collapses three identical filterPieceMoves/
// enumerateLegalMoves template instantiations into one (~2.8 KiB flash
// savings on ESP32).
struct MoveAdder {
  Move* buf;
  int& count;
  bool operator()(Move m) { buf[count++] = m; return false; }
};

// Returns the pin-ray mask for a piece on `sq`.
// If pinned, only targets on the ray are legal. If not pinned, returns ~0ULL
// (all targets valid from a pin perspective — checkMask still applies).
static Bitboard pinRayFor(const PinData& pinData, Square sq) {
  if (!(pinData.pinned & squareBB(sq))) return ~0ULL;
  for (int i = 0; i < pinData.count; i++)
    if (pinData.pinnedSq[i] == sq) return pinData.pinRay[i];
  return ~0ULL;  // defensive fallback (should not be reached)
}



// Detects all absolute pins against `kingSq` for `sideToMove`.
static PinData computePinData(const BitboardSet& bb, Square kingSq, Color sideToMove) {
  PinData data = {};
  Bitboard friendly = bb.byColor[piece::raw(sideToMove)];
  Color enemy = ~sideToMove;

  int rookIdx   = piece::pieceIndex(enemy, PieceType::ROOK);
  int queenIdx  = piece::pieceIndex(enemy, PieceType::QUEEN);
  int bishopIdx = piece::pieceIndex(enemy, PieceType::BISHOP);
  Bitboard enemyRookQueens   = bb.byPiece[rookIdx]   | bb.byPiece[queenIdx];
  Bitboard enemyBishopQueens = bb.byPiece[bishopIdx] | bb.byPiece[queenIdx];

  Bitboard pinners =
      (attacks::xrayRook(bb.occupied, friendly, kingSq)   & enemyRookQueens)
    | (attacks::xrayBishop(bb.occupied, friendly, kingSq) & enemyBishopQueens);

  while (pinners) {
    Square pinner = popLsb(pinners);
    Bitboard ray = attacks::between(kingSq, pinner) | squareBB(pinner);
    Bitboard pinnedBit = ray & friendly;
    if (popcount(pinnedBit) != 1) continue;
    data.pinRay[data.count] = ray;
    data.pinnedSq[data.count] = lsb(pinnedBit);
    data.pinned |= pinnedBit;
    data.count++;
  }

  return data;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public: build legality context for staged generation.
// ---------------------------------------------------------------------------

LegalityContext buildLegalityContext(const BitboardSet& bb, Color color, Square kingSq) {
  LegalityContext ctx;
  ctx.kingSq = kingSq;
  Bitboard checkers = attacks::attackersOfSquare(bb, kingSq, ~color);
  ctx.checkerCount = popcount(checkers);
  ctx.checkMask = ~0ULL;
  if (ctx.checkerCount == 1) {
    Square checker = lsb(checkers);
    ctx.checkMask = attacks::between(kingSq, checker) | squareBB(checker);
  }
  ctx.pinData = computePinData(bb, kingSq, color);
  return ctx;
}

// ---------------------------------------------------------------------------
// BB-only forward move application for leavesInCheck (avoids mailbox copy).
// ---------------------------------------------------------------------------

static void applyMoveBB(BitboardSet& bb, Square from, Square to,
                        Piece piece, Piece capturedPiece,
                        const EnPassantInfo& ep,
                        const CastlingInfo& castle) {
  if (ep.isCapture) {
    bb.removePiece(ep.capturedPawnSq,
                   piece::makePiece(~piece::pieceColor(piece), PieceType::PAWN));
  } else if (capturedPiece != Piece::NONE) {
    bb.removePiece(to, capturedPiece);
  }

  bb.movePiece(from, to, piece);

  if (castle.isCastling) {
    Piece rook = piece::makePiece(piece::pieceColor(piece), PieceType::ROOK);
    bb.movePiece(castle.rookFromSq, castle.rookToSq, rook);
  }
}

// ---------------------------------------------------------------------------
// Copy-make legality check — copies BitboardSet, applies move, tests check.
//
// For non-castling king moves, an occupancy-based fast path avoids copying
// the 120-byte BitboardSet: the king is removed from occupancy at its origin,
// and slider queries use that modified occupancy to check if the destination
// is attacked.  Castling and en passant still require the full copy-make
// approach because multiple pieces change position.
// ---------------------------------------------------------------------------

static bool leavesInCheck(const BitboardSet& bb, const Piece mailbox[],
                          Square from, Square to, Square kingSq) {
  Piece movingPiece = mailbox[from];
  Color movingColor = piece::pieceColor(movingPiece);

  // King non-castling fast path: check destination with king removed from
  // occupancy.  Avoids BitboardSet copy (~120 bytes) on every king move.
  // Castling needs rook repositioned in occupancy, so it falls through.
  int delta = static_cast<int>(to) - static_cast<int>(from);
  if (from == kingSq && delta != 2 && delta != -2) {
    Bitboard occ = (bb.occupied ^ squareBB(from)) | squareBB(to);
    return attacks::isSquareUnderAttackOcc(bb, to, movingColor, occ);
  }

  Piece targetPiece = mailbox[to];
  EnPassantInfo ep = utils::checkEnPassant(mailbox, from, to);
  CastlingInfo castle = utils::checkCastling(mailbox, from, to);

  BitboardSet testBB = bb;
  applyMoveBB(testBB, from, to, movingPiece, targetPiece, ep, castle);

  return attacks::isSquareUnderAttack(testBB, kingSq, movingColor);
}

// ---------------------------------------------------------------------------
// Unified legal move enumeration — core of all legal move generators.
//
// Enumerates legal moves for a position, filtering by `filterMode`:
//   0 = all moves, 1 = captures+promotions, 2 = quiet non-promotions.
//
// The `Handler` callback receives each legal Move and returns:
//   true  → stop enumeration (move found, used by hasAnyLegalMove)
//   false → continue enumeration (collect all, used by generate*)
//
// Implements the standard pin-aware / check-mask filtered generation:
// king moves are always verified via leavesInCheck; non-king moves use
// the combined pin-ray × check-mask filter with special EP handling.
//
// Reference: https://www.chessprogramming.org/Move_Generation
// ---------------------------------------------------------------------------
// filterPieceMoves — direct-emit legality filtering for a single piece.
//
// Generates and filters legal moves inline, applying pin/check masks at the
// bitboard level (non-king, non-EP) or full leavesInCheck validation (king /
// en-passant).  Eliminates the intermediate MoveList pseudo-legal buffer:
// attack bitboards are masked with legalMask before move enumeration, so
// illegal targets are never visited.
//
// Returns true if the handler signaled early termination; false otherwise.
// Used by both enumerateLegalMoves (all-piece iteration) and
// getPossibleMoves (single-piece query).
// ---------------------------------------------------------------------------
template <typename Handler>
static bool filterPieceMoves(const BitboardSet& bb, const Piece mailbox[],
                             Square sq, const PositionState& state,
                             const LegalityContext& ctx, FilterMode filterMode,
                             Handler&& handler) {
  Piece piece = mailbox[sq];
  if (piece == Piece::NONE) return false;

  Color color  = piece::pieceColor(piece);
  PieceType pt = piece::pieceType(piece);
  uint8_t from8 = static_cast<uint8_t>(sq);
  Bitboard enemy = bb.byColor[piece::raw(~color)];

  // =======================================================================
  // King — each move validated via leavesInCheck.
  // =======================================================================
  if (sq == ctx.kingSq) {
    // Normal king moves from attack bitboard.
    Bitboard attacks = attacks::KING[sq] & ~bb.byColor[piece::raw(color)];
    while (attacks) {
      Square to = popLsb(attacks);
      bool capture = squareBB(to) & enemy;
      if (filterMode == FilterMode::CAPTURES_PROMOS && !capture) continue;
      if (filterMode == FilterMode::QUIETS && capture) continue;
      if (!leavesInCheck(bb, mailbox, sq, to, to))
        if (handler(Move(from8, static_cast<uint8_t>(to),
                         capture ? MOVE_CAPTURE : uint8_t(0))))
          return true;
    }

    // Castling — quiet moves, excluded in CAPTURES_PROMOS mode.
    if (filterMode != FilterMode::CAPTURES_PROMOS) {
      int rank = piece::homeRank(color);
      Piece kingPiece = piece::makePiece(color, PieceType::KING);
      Piece rookPiece = piece::makePiece(color, PieceType::ROOK);
      if (rankOf(sq) == rank && fileOf(sq) == piece::KING_START_FILE &&
          mailbox[sq] == kingPiece &&
          !attacks::isSquareUnderAttack(bb, sq, color)) {
        // King-side (e → g)
        if (utils::hasCastlingRight(state.castlingRights, color, true)) {
          Square f = makeSquare(rank, 5);
          Square g = makeSquare(rank, 6);
          Square h = makeSquare(rank, 7);
          if (mailbox[f] == Piece::NONE && mailbox[g] == Piece::NONE &&
              mailbox[h] == rookPiece)
            if (!attacks::isSquareUnderAttack(bb, f, color) &&
                !attacks::isSquareUnderAttack(bb, g, color))
              if (!leavesInCheck(bb, mailbox, sq, g, g))
                if (handler(Move(from8, static_cast<uint8_t>(g), MOVE_CASTLING)))
                  return true;
        }
        // Queen-side (e → c)
        if (utils::hasCastlingRight(state.castlingRights, color, false)) {
          Square d = makeSquare(rank, 3);
          Square c = makeSquare(rank, 2);
          Square b = makeSquare(rank, 1);
          Square a = makeSquare(rank, 0);
          if (mailbox[d] == Piece::NONE && mailbox[c] == Piece::NONE &&
              mailbox[b] == Piece::NONE && mailbox[a] == rookPiece)
            if (!attacks::isSquareUnderAttack(bb, d, color) &&
                !attacks::isSquareUnderAttack(bb, c, color))
              if (!leavesInCheck(bb, mailbox, sq, c, c))
                if (handler(Move(from8, static_cast<uint8_t>(c), MOVE_CASTLING)))
                  return true;
        }
      }
    }
    return false;
  }

  // =======================================================================
  // Non-king pieces — pin/check mask applied at bitboard level.
  // =======================================================================
  Bitboard legalMask = pinRayFor(ctx.pinData, sq) & ctx.checkMask;

  // --- Pawns ---
  if (pt == PieceType::PAWN) {
    int forward   = piece::pawnForward(color);
    int promoRank = piece::promotionRank(color);

    // Helper: emit pawn move with promotion expansion.
    auto emitPawn = [&](Square to, uint8_t baseFlags) -> bool {
      uint8_t to8 = static_cast<uint8_t>(to);
      if (rankOf(to) == promoRank) {
        for (uint8_t pi = 0; pi < 4; pi++)
          if (handler(Move(from8, to8, baseFlags | Move::promoFlags(pi))))
            return true;
      } else {
        if (handler(Move(from8, to8, baseFlags))) return true;
      }
      return false;
    };

    // Pushes
    Square fwdSq = sq + forward;
    bool fwdEmpty = !(bb.occupied & squareBB(fwdSq));
    if (fwdEmpty) {
      // Single push (target must also be in legalMask).
      if (squareBB(fwdSq) & legalMask) {
        bool isPromo = rankOf(fwdSq) == promoRank;
        if (filterMode == FilterMode::CAPTURES_PROMOS && !isPromo) { /* skip */ }
        else if (filterMode == FilterMode::QUIETS && isPromo) { /* skip */ }
        else if (emitPawn(fwdSq, 0)) return true;
      }
      // Double push from starting rank (never a promotion).
      if (filterMode != FilterMode::CAPTURES_PROMOS &&
          rankOf(sq) == piece::pawnStartRank(color)) {
        Square dblSq = sq + 2 * forward;
        if (!(bb.occupied & squareBB(dblSq)) && (squareBB(dblSq) & legalMask))
          if (handler(Move(from8, static_cast<uint8_t>(dblSq), 0)))
            return true;
      }
    }

    // Captures (via pawn attack table, masked with legal + enemy).
    if (filterMode != FilterMode::QUIETS) {
      Bitboard capTargets = attacks::PAWN[piece::raw(color)][sq] & enemy & legalMask;
      while (capTargets) {
        Square capSq = popLsb(capTargets);
        if (emitPawn(capSq, MOVE_CAPTURE)) return true;
      }
    }

    // En passant (validated via leavesInCheck, NOT legalMask).
    if (filterMode != FilterMode::QUIETS && state.epSquare != SQ_NONE) {
      if (rankOf(sq) == rankOf(state.epSquare) - forward / 8) {
        if (attacks::PAWN[piece::raw(color)][sq] & squareBB(state.epSquare))
          if (!leavesInCheck(bb, mailbox, sq, state.epSquare, ctx.kingSq))
            if (handler(Move(from8, static_cast<uint8_t>(state.epSquare),
                             MOVE_CAPTURE | MOVE_EP)))
              return true;
      }
    }
    return false;
  }

  // --- Sliding / leaper pieces (rook, bishop, queen, knight) ---
  Bitboard atk;
  switch (pt) {
    case PieceType::ROOK:   atk = attacks::rook(sq, bb.occupied); break;
    case PieceType::BISHOP: atk = attacks::bishop(sq, bb.occupied); break;
    case PieceType::QUEEN:  atk = attacks::queen(sq, bb.occupied); break;
    case PieceType::KNIGHT: atk = attacks::KNIGHT[sq]; break;
    default: return false;
  }
  atk &= ~bb.byColor[piece::raw(color)];  // Remove own pieces.
  atk &= legalMask;                        // Pin/check filter.

  while (atk) {
    Square to = popLsb(atk);
    bool capture = squareBB(to) & enemy;
    if (filterMode == FilterMode::CAPTURES_PROMOS && !capture) continue;
    if (filterMode == FilterMode::QUIETS && capture) continue;
    if (handler(Move(from8, static_cast<uint8_t>(to),
                     capture ? MOVE_CAPTURE : uint8_t(0))))
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------

template <typename Handler>
static bool enumerateLegalMoves(const BitboardSet& bb, const Piece mailbox[],
                                Color color, const PositionState& state,
                                const LegalityContext& ctx, FilterMode filterMode,
                                Handler&& handler) {
  // --- King moves (always leavesInCheck, unaffected by pin/check masks) ---
  if (filterPieceMoves(bb, mailbox, ctx.kingSq, state, ctx, filterMode, handler))
    return true;

  // Double check: only king can move — done above, skip non-king pieces.
  if (ctx.checkerCount >= 2) return false;

  // --- Non-king pieces ---
  Bitboard friendly = bb.byColor[piece::raw(color)];
  Bitboard pieces = friendly & ~squareBB(ctx.kingSq);
  while (pieces) {
    Square sq = popLsb(pieces);
    if (filterPieceMoves(bb, mailbox, sq, state, ctx, filterMode, handler))
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Collect legal moves into a MoveList, clearing it first.
// Requires a pre-built LegalityContext.  Used by staged API and
// generateForColor.
// ---------------------------------------------------------------------------

template <int N>
static void collectLegalMoves(const BitboardSet& bb, const Piece mailbox[],
                              Color color, const PositionState& state,
                              const LegalityContext& ctx, MoveListBase<N>& out,
                              FilterMode filterMode) {
  out.clear();
  MoveAdder adder{out.moves, out.count};
  enumerateLegalMoves(bb, mailbox, color, state, ctx, filterMode, adder);
}

// ---------------------------------------------------------------------------
// Self-contained bulk generation: resolves king, builds context, collects.
// Shared entry point for the four public bulk/QS generators.
// ---------------------------------------------------------------------------

template <int N>
static void generateForColor(const BitboardSet& bb, const Piece mailbox[],
                              Color color, const PositionState& state,
                              MoveListBase<N>& moves, FilterMode filterMode) {
  Square kingSq;
  if (!utils::resolveKingSquare(bb, color, kingSq)) { moves.clear(); return; }
  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);
  collectLegalMoves(bb, mailbox, color, state, ctx, moves, filterMode);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void getPossibleMoves(const BitboardSet& bb, const Piece mailbox[],
                      Square sq, const PositionState& state,
                      MoveList& moves) {
  moves.clear();
  Piece piece = mailbox[sq];
  if (piece == Piece::NONE) return;

  bool isKing = piece::pieceType(piece) == PieceType::KING;
  Color color = piece::pieceColor(piece);

  Square kingSq;
  if (isKing) {
    kingSq = sq;
  } else {
    if (!utils::resolveKingSquare(bb, color, kingSq)) return;
  }

  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);

  if (ctx.checkerCount >= 2 && !isKing) return;

  MoveAdder adder{moves.moves, moves.count};
  filterPieceMoves(bb, mailbox, sq, state, ctx, FilterMode::ALL, adder);
}

template <int N>
void generateMoves(const BitboardSet& bb, const Piece mailbox[],
                   Color color, const PositionState& state,
                   MoveListBase<N>& moves, FilterMode filter) {
  generateForColor(bb, mailbox, color, state, moves, filter);
}

// Explicit instantiations for MoveList (218) and QSMoveList (128).
template void generateMoves<MAX_MOVES>(const BitboardSet&, const Piece[],
    Color, const PositionState&, MoveList&, FilterMode);
template void generateMoves<QS_MAX_MOVES>(const BitboardSet&, const Piece[],
    Color, const PositionState&, QSMoveList&, FilterMode);

// ---------------------------------------------------------------------------
// Staged API: reuse pre-built LegalityContext
// ---------------------------------------------------------------------------

void generateMoves(const BitboardSet& bb, const Piece mailbox[],
                   Color color, const PositionState& state,
                   const LegalityContext& ctx, MoveList& moves,
                   FilterMode filter) {
  collectLegalMoves(bb, mailbox, color, state, ctx, moves, filter);
}

void generateMovesAppend(const BitboardSet& bb, const Piece mailbox[],
                         Color color, const PositionState& state,
                         const LegalityContext& ctx, MoveList& moves,
                         FilterMode filter) {
  MoveAdder adder{moves.moves, moves.count};
  enumerateLegalMoves(bb, mailbox, color, state, ctx, filter, adder);
}

bool isValidMove(const BitboardSet& bb, const Piece mailbox[],
                 Square from, Square to,
                 const PositionState& state) {
  Piece piece = mailbox[from];
  if (piece == Piece::NONE) return false;

  Color color = piece::pieceColor(piece);
  bool isKingMove = piece::pieceType(piece) == PieceType::KING;
  Square kingSq;
  if (isKingMove) {
    kingSq = from;
  } else {
    if (!utils::resolveKingSquare(bb, color, kingSq)) return false;
  }
  return isValidMove(bb, mailbox, from, to, state, kingSq);
}

bool isValidMove(const BitboardSet& bb, const Piece mailbox[],
                 Square from, Square to,
                 const PositionState& state, Square kingSq) {
  Piece piece = mailbox[from];
  if (piece == Piece::NONE) return false;

  Color color = piece::pieceColor(piece);
  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);

  // Double check: only king can move.
  if (ctx.checkerCount >= 2 && from != kingSq) return false;

  bool found = false;
  filterPieceMoves(bb, mailbox, from, state, ctx, FilterMode::ALL,
      [&](Move m) -> bool {
        if (static_cast<Square>(m.to) == to) { found = true; return true; }
        return false;
      });
  return found;
}

bool hasAnyLegalMove(const BitboardSet& bb, const Piece mailbox[],
                     Color color, const PositionState& state) {
  Square kingSq;
  if (!utils::resolveKingSquare(bb, color, kingSq)) return false;
  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);
  return enumerateLegalMoves(bb, mailbox, color, state, ctx, FilterMode::ALL,
      [](Move) { return true; });
}

bool hasLegalEnPassantCapture(const BitboardSet& bb, const Piece mailbox[],
                              Color sideToMove, const PositionState& state) {
  if (state.epSquare == SQ_NONE) return false;

  Square epSq = state.epSquare;

  Square kingSq;
  if (!utils::resolveKingSquare(bb, sideToMove, kingSq)) return false;

  // Use the opponent's pawn attack table to find which friendly pawns can
  // capture on the EP square (reverse lookup: squares attacking epSq).
  // Reference: https://www.chessprogramming.org/Pawn_Attacks_(Bitboards)
  int pawnIdx = piece::pieceIndex(sideToMove, PieceType::PAWN);
  Bitboard capturers = attacks::PAWN[piece::raw(~sideToMove)][epSq] & bb.byPiece[pawnIdx];
  while (capturers) {
    Square from = popLsb(capturers);
    if (!leavesInCheck(bb, mailbox, from, epSq, kingSq))
      return true;
  }
  return false;
}

}  // namespace movegen
}  // namespace LibreChess
