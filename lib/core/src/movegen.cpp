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

  int rookIdx   = piece::pieceZobristIndex(piece::makePiece(enemy, PieceType::ROOK));
  int queenIdx  = piece::pieceZobristIndex(piece::makePiece(enemy, PieceType::QUEEN));
  int bishopIdx = piece::pieceZobristIndex(piece::makePiece(enemy, PieceType::BISHOP));
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
    Square epSq = squareOf(ep.capturedPawnRow, colOf(to));
    Piece epPawn = piece::makePiece(~piece::pieceColor(piece), PieceType::PAWN);
    bb.removePiece(epSq, epPawn);
  } else if (capturedPiece != Piece::NONE) {
    bb.removePiece(to, capturedPiece);
  }

  bb.movePiece(from, to, piece);

  if (castle.isCastling) {
    Piece rook = piece::makePiece(piece::pieceColor(piece), PieceType::ROOK);
    Square rookFrom = squareOf(rowOf(from), castle.rookFromCol);
    Square rookTo = squareOf(rowOf(from), castle.rookToCol);
    bb.movePiece(rookFrom, rookTo, rook);
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
  int row = rowOf(sq);
  int col = colOf(sq);
  int direction = piece::pawnDirection(pieceColor);
  Bitboard friendly = bb.byColor[piece::raw(pieceColor)];
  int promoRow = piece::promotionRow(pieceColor);
  uint8_t from8 = static_cast<uint8_t>(sq);

  auto emitPawn = [&](Square to, uint8_t baseFlags) {
    uint8_t to8 = static_cast<uint8_t>(to);
    if (rowOf(to) == promoRow) {
      for (uint8_t pi = 0; pi < 4; pi++)
        moves.add(Move(from8, to8, baseFlags | Move::promoFlags(pi)));
    } else {
      moves.add(Move(from8, to8, baseFlags));
    }
  };

  int fwdRow = row + direction;
  if ((unsigned)fwdRow < 8) {
    Square fwdSq = squareOf(fwdRow, col);
    if (!(bb.occupied & squareBB(fwdSq))) {
      emitPawn(fwdSq, 0);

      int startRow = piece::homeRow(pieceColor) + direction;
      if (row == startRow) {
        int dblRow = row + 2 * direction;
        Square dblSq = squareOf(dblRow, col);
        if (!(bb.occupied & squareBB(dblSq)))
          moves.add(Move(from8, static_cast<uint8_t>(dblSq), 0));
      }
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

  if (state.epRow >= 0 && state.epCol >= 0 && row == state.epRow - direction) {
    Square epSq = squareOf(state.epRow, state.epCol);
    if (attacks::PAWN[piece::raw(pieceColor)][sq] & squareBB(epSq))
      moves.add(Move(from8, static_cast<uint8_t>(epSq), MOVE_CAPTURE | MOVE_EP));
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
  int homeRow = piece::homeRow(pieceColor);
  Piece kingPiece = piece::makePiece(pieceColor, PieceType::KING);
  Piece rookPiece = piece::makePiece(pieceColor, PieceType::ROOK);

  int row = rowOf(sq);
  int col = colOf(sq);
  if (row != homeRow || col != 4) return;
  if (mailbox[sq] != kingPiece) return;

  if (attacks::isSquareUnderAttack(bb, sq, pieceColor)) return;

  uint8_t from8 = static_cast<uint8_t>(sq);

  // King-side castling (e -> g)
  if (utils::hasCastlingRight(castlingRights, pieceColor, true)) {
    Square f = squareOf(homeRow, 5);
    Square g = squareOf(homeRow, 6);
    Square h = squareOf(homeRow, 7);
    if (mailbox[f] == Piece::NONE && mailbox[g] == Piece::NONE && mailbox[h] == rookPiece)
      if (!attacks::isSquareUnderAttack(bb, f, pieceColor) &&
          !attacks::isSquareUnderAttack(bb, g, pieceColor))
        moves.add(Move(from8, static_cast<uint8_t>(g), MOVE_CASTLING));
  }

  // Queen-side castling (e -> c)
  if (utils::hasCastlingRight(castlingRights, pieceColor, false)) {
    Square d = squareOf(homeRow, 3);
    Square c = squareOf(homeRow, 2);
    Square b = squareOf(homeRow, 1);
    Square a = squareOf(homeRow, 0);
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
// Shared impl for generateAllMoves / generateCaptures / generateQuiets.
//
// filterMode: 0 = all moves, 1 = captures+promotions only, 2 = quiets only.
// ---------------------------------------------------------------------------

static void generateMovesImpl(const BitboardSet& bb, const Piece mailbox[],
                              Color color, const PositionState& state,
                              const LegalityContext& ctx, MoveList& out,
                              int filterMode) {
  out.clear();
  Square kingSq = ctx.kingSq;

  // --- King moves (always leavesInCheck, unaffected by pin/check masks) ---
  {
    MoveList kingPseudo;
    getPseudoLegalMoves(bb, mailbox, kingSq, state, kingPseudo, true);
    for (int i = 0; i < kingPseudo.count; i++) {
      Move m = kingPseudo.moves[i];
      bool capture = m.isCapture();
      if (filterMode == 1 && !capture) continue;
      if (filterMode == 2 && capture) continue;
      if (!leavesInCheck(bb, mailbox, kingSq, static_cast<Square>(m.to), state, static_cast<Square>(m.to)))
        out.add(m);
    }
  }

  // Double check: only king can move — done above, skip non-king pieces.
  if (ctx.checkerCount >= 2) return;

  // --- Non-king pieces ---
  Bitboard friendly = bb.byColor[piece::raw(color)];
  Bitboard pieces = friendly & ~squareBB(kingSq);
  while (pieces) {
    Square sq = popLsb(pieces);
    Bitboard legalMask = pinRayFor(ctx.pinData, sq) & ctx.checkMask;

    MoveList pseudo;
    getPseudoLegalMoves(bb, mailbox, sq, state, pseudo, false);

    for (int i = 0; i < pseudo.count; i++) {
      Move m = pseudo.moves[i];
      Square target = static_cast<Square>(m.to);

      if (m.isEP()) {
        // EP is always a capture — skip in quiet mode.
        if (filterMode == 2) continue;
        if (!leavesInCheck(bb, mailbox, sq, target, state, kingSq))
          out.add(m);
        continue;
      }

      if (!(squareBB(target) & legalMask)) continue;

      bool capture = m.isCapture();
      bool promo   = m.isPromotion();
      if (filterMode == 1 && !capture && !promo) continue;
      if (filterMode == 2 && (capture || promo)) continue;
      out.add(m);
    }
  }
}

// hasAnyLegalMove with pre-found king (used by rules and isGameOver).
// Iterates friendly pieces only via byColor bitboard (mirrors
// generateMovesImpl pattern — avoids visiting ~16 enemy pieces).
static bool hasAnyLegalMoveImpl(const BitboardSet& bb, const Piece mailbox[],
                                Color color, const PositionState& state, Square kingSq) {
  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);

  // --- King moves (always leavesInCheck, unaffected by pin/check masks) ---
  {
    MoveList kingPseudo;
    getPseudoLegalMoves(bb, mailbox, kingSq, state, kingPseudo, true);
    for (int i = 0; i < kingPseudo.count; i++) {
      Square target = static_cast<Square>(kingPseudo.moves[i].to);
      if (!leavesInCheck(bb, mailbox, kingSq, target, state, target))
        return true;
    }
  }

  // Double check: only king can move — checked above.
  if (ctx.checkerCount >= 2) return false;

  // --- Non-king pieces (friendly only via bitboard serialization) ---
  Bitboard friendly = bb.byColor[piece::raw(color)];
  Bitboard pieces = friendly & ~squareBB(kingSq);
  while (pieces) {
    Square sq = popLsb(pieces);
    Bitboard legalMask = pinRayFor(ctx.pinData, sq) & ctx.checkMask;

    MoveList pseudoMoves;
    getPseudoLegalMoves(bb, mailbox, sq, state, pseudoMoves, false);

    for (int i = 0; i < pseudoMoves.count; i++) {
      Move m = pseudoMoves.moves[i];
      Square target = static_cast<Square>(m.to);

      if (m.isEP()) {
        if (!leavesInCheck(bb, mailbox, sq, target, state, kingSq)) return true;
        continue;
      }

      if (squareBB(target) & legalMask) return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void getPossibleMoves(const BitboardSet& bb, const Piece mailbox[],
                      int row, int col, const PositionState& state,
                      MoveList& moves) {
  moves.clear();
  Square sq = squareOf(row, col);
  Piece piece = mailbox[sq];
  if (piece == Piece::NONE) return;

  bool isKing = piece::pieceType(piece) == PieceType::KING;
  Color color = piece::pieceColor(piece);

  Square kingSq;
  if (isKing) {
    kingSq = sq;
  } else {
    int kidx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::KING));
    Bitboard kingBB = bb.byPiece[kidx];
    if (!kingBB) return;
    kingSq = lsb(kingBB);
  }

  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);

  if (ctx.checkerCount >= 2 && !isKing) return;

  MoveList pseudoMoves;
  getPseudoLegalMoves(bb, mailbox, sq, state, pseudoMoves, true);

  if (isKing) {
    for (int i = 0; i < pseudoMoves.count; i++) {
      Move m = pseudoMoves.moves[i];
      if (!leavesInCheck(bb, mailbox, sq, static_cast<Square>(m.to), state, static_cast<Square>(m.to)))
        moves.add(m);
    }
    return;
  }

  Bitboard legalMask = pinRayFor(ctx.pinData, sq) & ctx.checkMask;

  for (int i = 0; i < pseudoMoves.count; i++) {
    Move m = pseudoMoves.moves[i];
    Square target = static_cast<Square>(m.to);

    if (m.isEP()) {
      if (!leavesInCheck(bb, mailbox, sq, target, state, kingSq))
        moves.add(m);
      continue;
    }

    if (squareBB(target) & legalMask)
      moves.add(m);
  }
}

void generateAllMoves(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      MoveList& moves) {
  int kidx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::KING));
  Bitboard kingBB = bb.byPiece[kidx];
  if (!kingBB) { moves.clear(); return; }
  LegalityContext ctx = buildLegalityContext(bb, color, lsb(kingBB));
  generateMovesImpl(bb, mailbox, color, state, ctx, moves, 0);
}

void generateCaptures(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      MoveList& moves) {
  int kidx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::KING));
  Bitboard kingBB = bb.byPiece[kidx];
  if (!kingBB) { moves.clear(); return; }
  LegalityContext ctx = buildLegalityContext(bb, color, lsb(kingBB));
  generateMovesImpl(bb, mailbox, color, state, ctx, moves, 1);
}

// ---------------------------------------------------------------------------
// Staged API: reuse pre-built LegalityContext
// ---------------------------------------------------------------------------

void generateCaptures(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      const LegalityContext& ctx, MoveList& moves) {
  generateMovesImpl(bb, mailbox, color, state, ctx, moves, 1);
}

void generateQuiets(const BitboardSet& bb, const Piece mailbox[],
                    Color color, const PositionState& state,
                    const LegalityContext& ctx, MoveList& moves) {
  generateMovesImpl(bb, mailbox, color, state, ctx, moves, 2);
}

bool isValidMove(const BitboardSet& bb, const Piece mailbox[],
                 int fromRow, int fromCol, int toRow, int toCol,
                 const PositionState& state) {
  Square from = squareOf(fromRow, fromCol);
  Square to = squareOf(toRow, toCol);
  Piece piece = mailbox[from];
  if (piece == Piece::NONE) return false;

  Color color = piece::pieceColor(piece);
  bool isKingMove = piece::pieceType(piece) == PieceType::KING;
  Square kingSq;
  if (isKingMove) {
    kingSq = from;
  } else {
    int kidx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::KING));
    Bitboard kingBB = bb.byPiece[kidx];
    if (!kingBB) return false;
    kingSq = lsb(kingBB);
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
  int kidx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::KING));
  Bitboard kingBB = bb.byPiece[kidx];
  if (!kingBB) return false;
  return hasAnyLegalMoveImpl(bb, mailbox, color, state, lsb(kingBB));
}

bool hasLegalEnPassantCapture(const BitboardSet& bb, const Piece mailbox[],
                              Color sideToMove, const PositionState& state) {
  if (state.epRow < 0 || state.epCol < 0) return false;

  Square epSq = squareOf(state.epRow, state.epCol);
  Piece capturerPawn = piece::makePiece(sideToMove, PieceType::PAWN);

  int kidx = piece::pieceZobristIndex(piece::makePiece(sideToMove, PieceType::KING));
  Bitboard kingBB = bb.byPiece[kidx];
  if (!kingBB) return false;
  Square kingSq = lsb(kingBB);

  // Use the opponent's pawn attack table to find which friendly pawns can
  // capture on the EP square (reverse lookup: squares attacking epSq).
  // Reference: https://www.chessprogramming.org/Pawn_Attacks_(Bitboards)
  int pawnIdx = piece::pieceZobristIndex(capturerPawn);
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
