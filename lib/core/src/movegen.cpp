#include "movegen.h"

#include "iterator.h"
#include "utils.h"

namespace LibreChess {
namespace movegen {

using namespace LibreChess;
namespace atk = LibreChess::attacks;

// ---------------------------------------------------------------------------
// File-local helpers for pin-aware legal move generation
// ---------------------------------------------------------------------------

namespace {

// Up to 8 absolute pins possible (4 orthogonal + 4 diagonal rays from king).
struct PinData {
  Bitboard pinned;      // bitset of all pinned friendly piece squares
  Bitboard pinRay[8];   // legal-move mask per pinned piece (king→pinner ray, inclusive)
  Square pinnedSq[8];   // the pinned piece square corresponding to pinRay[i]
  int count;            // number of recorded pins (≤ 8)
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

  int rookIdx   = piece::pieceZobristIndex(piece::makePiece(enemy, PieceType::ROOK));
  int queenIdx  = piece::pieceZobristIndex(piece::makePiece(enemy, PieceType::QUEEN));
  int bishopIdx = piece::pieceZobristIndex(piece::makePiece(enemy, PieceType::BISHOP));
  Bitboard enemyRookQueens   = bb.byPiece[rookIdx]   | bb.byPiece[queenIdx];
  Bitboard enemyBishopQueens = bb.byPiece[bishopIdx] | bb.byPiece[queenIdx];

  Bitboard pinners =
      (atk::xrayRook(bb.occupied, friendly, kingSq)   & enemyRookQueens)
    | (atk::xrayBishop(bb.occupied, friendly, kingSq) & enemyBishopQueens);

  while (pinners) {
    Square pinner = popLsb(pinners);
    Bitboard ray = atk::between(kingSq, pinner) | squareBB(pinner);
    Bitboard pinnedBit = ray & friendly;
    if (popcount(pinnedBit) != 1) continue;
    data.pinRay[data.count] = ray;
    data.pinnedSq[data.count] = lsb(pinnedBit);
    data.pinned |= pinnedBit;
    data.count++;
  }

  return data;
}

// Pre-computed legality context: checker info + pin data + check mask.
// Built once per position, shared by move generation and hasAnyLegalMove.
struct LegalityContext {
  Square kingSq;
  Bitboard checkMask;   // ~0ULL if not in check, between(king,checker)|checker if single check
  PinData pinData;
  int checkerCount;     // 0 = not in check, 1 = single check, 2+ = double check
};

// Builds the legality context for `color` in the given position.
static LegalityContext buildLegalityContext(const BitboardSet& bb, Color color, Square kingSq) {
  LegalityContext ctx;
  ctx.kingSq = kingSq;
  Bitboard checkers = atk::attackersOfSquare(bb, kingSq, ~color);
  ctx.checkerCount = popcount(checkers);
  ctx.checkMask = ~0ULL;
  if (ctx.checkerCount == 1) {
    Square checker = lsb(checkers);
    ctx.checkMask = atk::between(kingSq, checker) | squareBB(checker);
  }
  ctx.pinData = computePinData(bb, kingSq, color);
  return ctx;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// BB-only forward move application for leavesInCheck (avoids mailbox copy).
// ---------------------------------------------------------------------------

static void applyMoveBB(BitboardSet& bb, Square from, Square to,
                        Piece piece, Piece capturedPiece,
                        const utils::EnPassantInfo& ep,
                        const utils::CastlingInfo& castle) {
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

  auto ep = utils::checkEnPassant(rowOf(from), colOf(from), rowOf(to), colOf(to),
                                       movingPiece, targetPiece);
  auto castle = utils::checkCastling(rowOf(from), colOf(from), rowOf(to), colOf(to),
                                          movingPiece);

  BitboardSet testBB = bb;
  applyMoveBB(testBB, from, to, movingPiece, targetPiece, ep, castle);

  return atk::isSquareUnderAttack(testBB, kingSq, movingColor);
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

  int captureRow = row + direction;
  if ((unsigned)captureRow < 8) {
    for (int dc = -1; dc <= 1; dc += 2) {
      int captureCol = col + dc;
      if ((unsigned)captureCol < 8) {
        Square capSq = squareOf(captureRow, captureCol);
        Bitboard capBit = squareBB(capSq);
        if ((bb.occupied & capBit) && !(friendly & capBit))
          emitPawn(capSq, MOVE_CAPTURE);
      }
    }
  }

  if (state.epRow >= 0 && state.epCol >= 0 && row == state.epRow - direction) {
    for (int dc = -1; dc <= 1; dc += 2) {
      if (col + dc == state.epCol) {
        Square epSq = squareOf(state.epRow, state.epCol);
        moves.add(Move(from8, static_cast<uint8_t>(epSq), MOVE_CAPTURE | MOVE_EP));
      }
    }
  }
}

static void addRookMoves(const BitboardSet& bb, Square sq, Color pieceColor, MoveList& moves) {
  Bitboard attacks = atk::rook(sq, bb.occupied);
  attacks &= ~bb.byColor[piece::raw(pieceColor)];
  Bitboard enemy = bb.byColor[piece::raw(~pieceColor)];
  uint8_t from8 = static_cast<uint8_t>(sq);
  while (attacks) {
    Square to = popLsb(attacks);
    uint8_t flags = (squareBB(to) & enemy) ? MOVE_CAPTURE : uint8_t(0);
    moves.add(Move(from8, static_cast<uint8_t>(to), flags));
  }
}

static void addBishopMoves(const BitboardSet& bb, Square sq, Color pieceColor, MoveList& moves) {
  Bitboard attacks = atk::bishop(sq, bb.occupied);
  attacks &= ~bb.byColor[piece::raw(pieceColor)];
  Bitboard enemy = bb.byColor[piece::raw(~pieceColor)];
  uint8_t from8 = static_cast<uint8_t>(sq);
  while (attacks) {
    Square to = popLsb(attacks);
    uint8_t flags = (squareBB(to) & enemy) ? MOVE_CAPTURE : uint8_t(0);
    moves.add(Move(from8, static_cast<uint8_t>(to), flags));
  }
}

static void addQueenMoves(const BitboardSet& bb, Square sq, Color pieceColor, MoveList& moves) {
  Bitboard attacks = atk::queen(sq, bb.occupied);
  attacks &= ~bb.byColor[piece::raw(pieceColor)];
  Bitboard enemy = bb.byColor[piece::raw(~pieceColor)];
  uint8_t from8 = static_cast<uint8_t>(sq);
  while (attacks) {
    Square to = popLsb(attacks);
    uint8_t flags = (squareBB(to) & enemy) ? MOVE_CAPTURE : uint8_t(0);
    moves.add(Move(from8, static_cast<uint8_t>(to), flags));
  }
}

static void addKnightMoves(const BitboardSet& bb, Square sq, Color pieceColor, MoveList& moves) {
  Bitboard attacks = atk::KNIGHT[sq];
  attacks &= ~bb.byColor[piece::raw(pieceColor)];
  Bitboard enemy = bb.byColor[piece::raw(~pieceColor)];
  uint8_t from8 = static_cast<uint8_t>(sq);
  while (attacks) {
    Square to = popLsb(attacks);
    uint8_t flags = (squareBB(to) & enemy) ? MOVE_CAPTURE : uint8_t(0);
    moves.add(Move(from8, static_cast<uint8_t>(to), flags));
  }
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

  if (atk::isSquareUnderAttack(bb, sq, pieceColor)) return;

  uint8_t from8 = static_cast<uint8_t>(sq);

  // King-side castling (e -> g)
  if (utils::hasCastlingRight(castlingRights, pieceColor, true)) {
    Square f = squareOf(homeRow, 5);
    Square g = squareOf(homeRow, 6);
    Square h = squareOf(homeRow, 7);
    if (mailbox[f] == Piece::NONE && mailbox[g] == Piece::NONE && mailbox[h] == rookPiece)
      if (!atk::isSquareUnderAttack(bb, f, pieceColor) &&
          !atk::isSquareUnderAttack(bb, g, pieceColor))
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
      if (!atk::isSquareUnderAttack(bb, d, pieceColor) &&
          !atk::isSquareUnderAttack(bb, c, pieceColor))
        moves.add(Move(from8, static_cast<uint8_t>(c), MOVE_CASTLING));
  }
}

static void addKingMoves(const BitboardSet& bb, const Piece mailbox[],
                         Square sq, Color pieceColor,
                         const PositionState& state, MoveList& moves,
                         bool includeCastling) {
  Bitboard attacks = atk::KING[sq];
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
// Shared impl for generateAllMoves / generateCaptures
// ---------------------------------------------------------------------------

static void generateMovesImpl(const BitboardSet& bb, const Piece mailbox[],
                              Color color, const PositionState& state,
                              MoveList& out, bool capturesOnly) {
  out.clear();
  Bitboard friendly = bb.byColor[piece::raw(color)];

  int kidx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::KING));
  Bitboard kingBB = bb.byPiece[kidx];
  if (!kingBB) return;

  LegalityContext ctx = buildLegalityContext(bb, color, lsb(kingBB));
  Square kingSq = ctx.kingSq;
  bool doubleCheck = (ctx.checkerCount >= 2);

  // --- King moves (always leavesInCheck, unaffected by pin/check masks) ---
  {
    MoveList kingPseudo;
    getPseudoLegalMoves(bb, mailbox, kingSq, state, kingPseudo, true);
    for (int i = 0; i < kingPseudo.count; i++) {
      Move m = kingPseudo.moves[i];
      if (capturesOnly && !m.isCapture()) continue;
      if (!leavesInCheck(bb, mailbox, kingSq, static_cast<Square>(m.to), state, static_cast<Square>(m.to)))
        out.add(m);
    }
  }

  // Double check: only king can move — done above, skip non-king pieces.
  if (doubleCheck) return;

  // --- Non-king pieces ---
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
        if (!leavesInCheck(bb, mailbox, sq, target, state, kingSq))
          out.add(m);
        continue;
      }

      if (!(squareBB(target) & legalMask)) continue;
      if (capturesOnly && !m.isCapture() && !m.isPromotion()) continue;
      out.add(m);
    }
  }
}

// hasAnyLegalMove with pre-found king (used by rules and isGameOver).
static bool hasAnyLegalMoveImpl(const BitboardSet& bb, const Piece mailbox[],
                                Color color, const PositionState& state, Square kingSq) {
  LegalityContext ctx = buildLegalityContext(bb, color, kingSq);

  return iterator::somePiece(bb, mailbox, [&](int r, int c, Piece piece) {
    if (piece::pieceColor(piece) != color) return false;
    Square sq = squareOf(r, c);
    bool isKing = piece::pieceType(piece) == PieceType::KING;

    if (ctx.checkerCount >= 2 && !isKing) return false;

    MoveList pseudoMoves;
    getPseudoLegalMoves(bb, mailbox, sq, state, pseudoMoves, true);

    if (isKing) {
      for (int i = 0; i < pseudoMoves.count; i++) {
        Square target = static_cast<Square>(pseudoMoves.moves[i].to);
        if (!leavesInCheck(bb, mailbox, sq, target, state, target))
          return true;
      }
      return false;
    }

    Bitboard legalMask = pinRayFor(ctx.pinData, sq) & ctx.checkMask;

    for (int i = 0; i < pseudoMoves.count; i++) {
      Move m = pseudoMoves.moves[i];
      Square target = static_cast<Square>(m.to);

      if (m.isEP()) {
        if (!leavesInCheck(bb, mailbox, sq, target, state, kingSq)) return true;
        continue;
      }

      if (squareBB(target) & legalMask) return true;
    }
    return false;
  });
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
  generateMovesImpl(bb, mailbox, color, state, moves, false);
}

void generateCaptures(const BitboardSet& bb, const Piece mailbox[],
                      Color color, const PositionState& state,
                      MoveList& moves) {
  generateMovesImpl(bb, mailbox, color, state, moves, true);
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
  int capturerRow = state.epRow - piece::pawnDirection(sideToMove);
  Piece capturerPawn = piece::makePiece(sideToMove, PieceType::PAWN);

  int kidx = piece::pieceZobristIndex(piece::makePiece(sideToMove, PieceType::KING));
  Bitboard kingBB = bb.byPiece[kidx];
  if (!kingBB) return false;
  Square kingSq = lsb(kingBB);

  if (state.epCol > 0) {
    Square from = squareOf(capturerRow, state.epCol - 1);
    if (mailbox[from] == capturerPawn && !leavesInCheck(bb, mailbox, from, epSq, state, kingSq))
      return true;
  }
  if (state.epCol < 7) {
    Square from = squareOf(capturerRow, state.epCol + 1);
    if (mailbox[from] == capturerPawn && !leavesInCheck(bb, mailbox, from, epSq, state, kingSq))
      return true;
  }
  return false;
}

}  // namespace movegen
}  // namespace LibreChess
