#include "movegen.h"

#include "utils.h"

namespace LibreChess {
namespace movegen {

using namespace LibreChess;

// ---------------------------------------------------------------------------
// File-local helpers for pin-aware legal move generation
// ---------------------------------------------------------------------------

namespace {

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

  int rookIdx   = piece::pieceIndex(piece::makePiece(enemy, PieceType::ROOK));
  int queenIdx  = piece::pieceIndex(piece::makePiece(enemy, PieceType::QUEEN));
  int bishopIdx = piece::pieceIndex(piece::makePiece(enemy, PieceType::BISHOP));
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
// ---------------------------------------------------------------------------

static bool leavesInCheck(const BitboardSet& bb, const Piece mailbox[],
                          Square from, Square to,
                          const PositionState& state, Square kingSq) {
  Piece movingPiece = mailbox[from];
  Color movingColor = piece::pieceColor(movingPiece);
  Piece targetPiece = mailbox[to];

  EnPassantInfo ep = utils::checkEnPassant(mailbox, from, to);
  CastlingInfo castle = utils::checkCastling(mailbox, from, to);

  BitboardSet testBB = bb;
  applyMoveBB(testBB, from, to, movingPiece, targetPiece, ep, castle);

  return attacks::isSquareUnderAttack(testBB, kingSq, movingColor);
}

// ---------------------------------------------------------------------------
// Pseudo-legal move generation per piece type
// ---------------------------------------------------------------------------

static void addPawnMoves(const BitboardSet& bb, const Piece mailbox[],
                         Square sq, Color pieceColor,
                         const PositionState& state, MoveList& moves) {
  int forward = piece::pawnForward(pieceColor);
  int promoRank = piece::promotionRank(pieceColor);
  Bitboard friendly = bb.byColor[piece::raw(pieceColor)];
  uint8_t from8 = static_cast<uint8_t>(sq);

  auto emitPawn = [&](Square to, uint8_t baseFlags) {
    uint8_t to8 = static_cast<uint8_t>(to);
    if (rankOf(to) == promoRank) {
      for (uint8_t pi = 0; pi < 4; pi++)
        moves.add(Move(from8, to8, baseFlags | Move::promoFlags(pi)));
    } else {
      moves.add(Move(from8, to8, baseFlags));
    }
  };

  // Single push
  Square fwdSq = sq + forward;
  if (!(bb.occupied & squareBB(fwdSq))) {
    emitPawn(fwdSq, 0);

    // Double push from starting rank
    if (rankOf(sq) == piece::pawnStartRank(pieceColor)) {
      Square dblSq = sq + 2 * forward;
      if (!(bb.occupied & squareBB(dblSq)))
        moves.add(Move(from8, static_cast<uint8_t>(dblSq), 0));
    }
  }

  // Captures via precomputed pawn attack table (consistent with knight/
  // bishop/rook/queen which all use precomputed tables).
  // Reference: https://www.chessprogramming.org/Pawn_Attacks_(Bitboards)
  Bitboard enemy = bb.occupied & ~friendly;
  Bitboard capTargets = attacks::PAWN[piece::raw(pieceColor)][sq] & enemy;
  while (capTargets) {
    Square capSq = popLsb(capTargets);
    emitPawn(capSq, MOVE_CAPTURE);
  }

  // En passant: pawn must be on the rank adjacent to the EP target.
  // forward is ±8 (one rank); forward / 8 converts to ±1 rank direction.
  if (state.epSquare != SQ_NONE &&
      rankOf(sq) == rankOf(state.epSquare) - forward / 8) {
    if (attacks::PAWN[piece::raw(pieceColor)][sq] & squareBB(state.epSquare))
      moves.add(Move(from8, static_cast<uint8_t>(state.epSquare),
                      MOVE_CAPTURE | MOVE_EP));
  }
}

// Shared body for rook/bishop/queen/knight move emission.
// Caller provides the pre-computed attack bitboard.
static void addPieceMoves(Bitboard attacks, Square sq,
                          const BitboardSet& bb, Color pieceColor,
                          MoveList& moves) {
  attacks &= ~bb.byColor[piece::raw(pieceColor)];
  Bitboard enemy = bb.byColor[piece::raw(~pieceColor)];
  uint8_t from8 = static_cast<uint8_t>(sq);
  while (attacks) {
    Square to = popLsb(attacks);
    uint8_t flags = (squareBB(to) & enemy) ? MOVE_CAPTURE : uint8_t(0);
    moves.add(Move(from8, static_cast<uint8_t>(to), flags));
  }
}

static void addRookMoves(const BitboardSet& bb, Square sq, Color pieceColor, MoveList& moves) {
  addPieceMoves(attacks::rook(sq, bb.occupied), sq, bb, pieceColor, moves);
}

static void addBishopMoves(const BitboardSet& bb, Square sq, Color pieceColor, MoveList& moves) {
  addPieceMoves(attacks::bishop(sq, bb.occupied), sq, bb, pieceColor, moves);
}

static void addQueenMoves(const BitboardSet& bb, Square sq, Color pieceColor, MoveList& moves) {
  addPieceMoves(attacks::queen(sq, bb.occupied), sq, bb, pieceColor, moves);
}

static void addKnightMoves(const BitboardSet& bb, Square sq, Color pieceColor, MoveList& moves) {
  addPieceMoves(attacks::KNIGHT[sq], sq, bb, pieceColor, moves);
}

static void addCastlingMoves(const BitboardSet& bb, const Piece mailbox[],
                             Square sq, Color pieceColor,
                             uint8_t castlingRights, MoveList& moves) {
  int rank = piece::homeRank(pieceColor);
  Piece kingPiece = piece::makePiece(pieceColor, PieceType::KING);
  Piece rookPiece = piece::makePiece(pieceColor, PieceType::ROOK);

  if (rankOf(sq) != rank || fileOf(sq) != 4) return;
  if (mailbox[sq] != kingPiece) return;

  if (attacks::isSquareUnderAttack(bb, sq, pieceColor)) return;

  uint8_t from8 = static_cast<uint8_t>(sq);

  // King-side castling (e -> g)
  if (utils::hasCastlingRight(castlingRights, pieceColor, true)) {
    Square f = makeSquare(rank, 5);
    Square g = makeSquare(rank, 6);
    Square h = makeSquare(rank, 7);
    if (mailbox[f] == Piece::NONE && mailbox[g] == Piece::NONE && mailbox[h] == rookPiece)
      if (!attacks::isSquareUnderAttack(bb, f, pieceColor) &&
          !attacks::isSquareUnderAttack(bb, g, pieceColor))
        moves.add(Move(from8, static_cast<uint8_t>(g), MOVE_CASTLING));
  }

  // Queen-side castling (e -> c)
  if (utils::hasCastlingRight(castlingRights, pieceColor, false)) {
    Square d = makeSquare(rank, 3);
    Square c = makeSquare(rank, 2);
    Square b = makeSquare(rank, 1);
    Square a = makeSquare(rank, 0);
    if (mailbox[d] == Piece::NONE && mailbox[c] == Piece::NONE &&
        mailbox[b] == Piece::NONE && mailbox[a] == rookPiece)
      if (!attacks::isSquareUnderAttack(bb, d, pieceColor) &&
          !attacks::isSquareUnderAttack(bb, c, pieceColor))
        moves.add(Move(from8, static_cast<uint8_t>(c), MOVE_CASTLING));
  }
}

static void addKingMoves(const BitboardSet& bb, const Piece mailbox[],
                         Square sq, Color pieceColor,
                         const PositionState& state, MoveList& moves,
                         bool includeCastling) {
  Bitboard attacks = attacks::KING[sq];
  attacks &= ~bb.byColor[piece::raw(pieceColor)];
  Bitboard enemy = bb.byColor[piece::raw(~pieceColor)];
  uint8_t from8 = static_cast<uint8_t>(sq);
  while (attacks) {
    Square to = popLsb(attacks);
    uint8_t flags = (squareBB(to) & enemy) ? MOVE_CAPTURE : uint8_t(0);
    moves.add(Move(from8, static_cast<uint8_t>(to), flags));
  }

  if (includeCastling)
    addCastlingMoves(bb, mailbox, sq, pieceColor, state.castlingRights, moves);
}

// ---------------------------------------------------------------------------
// Pseudo-legal move dispatcher
// ---------------------------------------------------------------------------

static void getPseudoLegalMoves(const BitboardSet& bb, const Piece mailbox[],
                                Square sq, const PositionState& state,
                                MoveList& moves, bool includeCastling = true) {
  moves.clear();
  Piece piece = mailbox[sq];
  if (piece == Piece::NONE) return;

  Color pieceColor = piece::pieceColor(piece);

  switch (piece::pieceType(piece)) {
    case PieceType::PAWN:   addPawnMoves(bb, mailbox, sq, pieceColor, state, moves); break;
    case PieceType::ROOK:   addRookMoves(bb, sq, pieceColor, moves); break;
    case PieceType::KNIGHT: addKnightMoves(bb, sq, pieceColor, moves); break;
    case PieceType::BISHOP: addBishopMoves(bb, sq, pieceColor, moves); break;
    case PieceType::QUEEN:  addQueenMoves(bb, sq, pieceColor, moves); break;
    case PieceType::KING:   addKingMoves(bb, mailbox, sq, pieceColor, state, moves, includeCastling); break;
    default: break;
  }
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
// filterPieceMoves — applies legality filtering for a single piece.
//
// Given a piece at `sq` with a pre-computed `LegalityContext`, generates its
// pseudo-legal moves and filters them for legality using pin/check masks
// (non-king) or full leavesInCheck validation (king / en-passant).
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
  bool isKing = (sq == ctx.kingSq);

  MoveList pseudo;
  getPseudoLegalMoves(bb, mailbox, sq, state, pseudo, isKing);

  if (isKing) {
    for (int i = 0; i < pseudo.count; i++) {
      Move m = pseudo.moves[i];
      bool capture = m.isCapture();
      if (filterMode == FilterMode::CAPTURES_PROMOS && !capture) continue;
      if (filterMode == FilterMode::QUIETS && capture) continue;
      if (!leavesInCheck(bb, mailbox, sq, static_cast<Square>(m.to),
                         state, static_cast<Square>(m.to)))
        if (handler(m)) return true;
    }
    return false;
  }

  Bitboard legalMask = pinRayFor(ctx.pinData, sq) & ctx.checkMask;

  for (int i = 0; i < pseudo.count; i++) {
    Move m = pseudo.moves[i];
    Square target = static_cast<Square>(m.to);

    if (m.isEP()) {
      // EP is always a capture — skip in quiet mode.
      if (filterMode == FilterMode::QUIETS) continue;
      if (!leavesInCheck(bb, mailbox, sq, target, state, ctx.kingSq))
        if (handler(m)) return true;
      continue;
    }

    if (!(squareBB(target) & legalMask)) continue;

    bool capture = m.isCapture();
    bool promo   = m.isPromotion();
    if (filterMode == FilterMode::CAPTURES_PROMOS && !capture && !promo) continue;
    if (filterMode == FilterMode::QUIETS && (capture || promo)) continue;
    if (handler(m)) return true;
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
// Concrete wrappers over enumerateLegalMoves
// ---------------------------------------------------------------------------

static void generateMovesImpl(const BitboardSet& bb, const Piece mailbox[],
                              Color color, const PositionState& state,
                              const LegalityContext& ctx, MoveList& out,
                              FilterMode filterMode) {
  out.clear();
  enumerateLegalMoves(bb, mailbox, color, state, ctx, filterMode,
      [&](Move m) { out.add(m); return false; });
}

static bool hasAnyLegalMoveImpl(const BitboardSet& bb, const Piece mailbox[],
                                Color color, const PositionState& state, Square kingSq) {
  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);
  return enumerateLegalMoves(bb, mailbox, color, state, ctx, FilterMode::ALL,
      [](Move) { return true; });
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

  filterPieceMoves(bb, mailbox, sq, state, ctx, FilterMode::ALL,
                   [&](Move m) { moves.add(m); return false; });
}

void generateAllMoves(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      MoveList& moves) {
  Square kingSq;
  if (!utils::resolveKingSquare(bb, color, kingSq)) { moves.clear(); return; }
  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);
  generateMovesImpl(bb, mailbox, color, state, ctx, moves, FilterMode::ALL);
}

void generateCaptures(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      MoveList& moves) {
  Square kingSq;
  if (!utils::resolveKingSquare(bb, color, kingSq)) { moves.clear(); return; }
  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);
  generateMovesImpl(bb, mailbox, color, state, ctx, moves, FilterMode::CAPTURES_PROMOS);
}

// ---------------------------------------------------------------------------
// Staged API: reuse pre-built LegalityContext
// ---------------------------------------------------------------------------

void generateCaptures(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      const LegalityContext& ctx, MoveList& moves) {
  generateMovesImpl(bb, mailbox, color, state, ctx, moves, FilterMode::CAPTURES_PROMOS);
}

void generateQuiets(const BitboardSet& bb, const Piece mailbox[],
                    Color color, const PositionState& state,
                    const LegalityContext& ctx, MoveList& moves) {
  generateMovesImpl(bb, mailbox, color, state, ctx, moves, FilterMode::QUIETS);
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
  MoveList pseudoMoves;
  getPseudoLegalMoves(bb, mailbox, from, state, pseudoMoves, true);

  bool isKingMove = piece::pieceType(mailbox[from]) == PieceType::KING;
  Square checkSq = isKingMove ? to : kingSq;

  for (int i = 0; i < pseudoMoves.count; i++)
    if (static_cast<Square>(pseudoMoves.moves[i].to) == to)
      return !leavesInCheck(bb, mailbox, from, to, state, checkSq);

  return false;
}

bool hasAnyLegalMove(const BitboardSet& bb, const Piece mailbox[],
                     Color color, const PositionState& state) {
  Square kingSq;
  if (!utils::resolveKingSquare(bb, color, kingSq)) return false;
  return hasAnyLegalMoveImpl(bb, mailbox, color, state, kingSq);
}

bool hasLegalEnPassantCapture(const BitboardSet& bb, const Piece mailbox[],
                              Color sideToMove, const PositionState& state) {
  if (state.epSquare == SQ_NONE) return false;

  Square epSq = state.epSquare;
  Piece capturerPawn = piece::makePiece(sideToMove, PieceType::PAWN);

  Square kingSq;
  if (!utils::resolveKingSquare(bb, sideToMove, kingSq)) return false;

  // Use the opponent's pawn attack table to find which friendly pawns can
  // capture on the EP square (reverse lookup: squares attacking epSq).
  // Reference: https://www.chessprogramming.org/Pawn_Attacks_(Bitboards)
  int pawnIdx = piece::pieceIndex(capturerPawn);
  Bitboard capturers = attacks::PAWN[piece::raw(~sideToMove)][epSq] & bb.byPiece[pawnIdx];
  while (capturers) {
    Square from = popLsb(capturers);
    if (!leavesInCheck(bb, mailbox, from, epSq, state, kingSq))
      return true;
  }
  return false;
}

}  // namespace movegen
}  // namespace LibreChess
