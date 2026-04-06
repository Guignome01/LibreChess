#include "position.h"

#include <cstring>

#include "evaluation.h"
#include "fen.h"
#include "movegen.h"

namespace LibreChess {

// ---------------------------------------------------------------------------
// Initial board layout
// ---------------------------------------------------------------------------

// Flat LERF-indexed array: index 0 = a1, index 63 = h8.
// Reference: https://www.chessprogramming.org/Square_Mapping_Considerations
const Piece Position::INITIAL_BOARD[64] = {
    // rank 1 (a1–h1)
    Piece::W_ROOK, Piece::W_KNIGHT, Piece::W_BISHOP, Piece::W_QUEEN, Piece::W_KING, Piece::W_BISHOP, Piece::W_KNIGHT, Piece::W_ROOK,
    // rank 2 (a2–h2)
    Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN,
    // ranks 3–6 (empty)
    Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE,
    Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE,
    Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE,
    Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE,
    // rank 7 (a7–h7)
    Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN,
    // rank 8 (a8–h8)
    Piece::B_ROOK, Piece::B_KNIGHT, Piece::B_BISHOP, Piece::B_QUEEN, Piece::B_KING, Piece::B_BISHOP, Piece::B_KNIGHT, Piece::B_ROOK
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

// Shared board initialization — populates bb_, mailbox_, kingSquare_[],
// and material+PST accumulators from INITIAL_BOARD.
void Position::initializeBoard() {
  bb_.clear();
  memset(mailbox_, 0, sizeof(mailbox_));
  for (Square sq = 0; sq < 64; ++sq) {
    Piece p = INITIAL_BOARD[sq];
    if (p != Piece::NONE) {
      bb_.setPiece(sq, p);
      mailbox_[sq] = p;
    }
  }
  kingSquare_[0] = SQ_E1;
  kingSquare_[1] = SQ_E8;
  auto pst = eval::computeMaterialPST(bb_);
  mgPST_ = pst.mg;
  egPST_ = pst.eg;
  material_ = eval::computeMaterial(bb_);
  phase_ = eval::computeGamePhase(bb_);
}

Position::Position()
    : currentTurn_(Color::WHITE),
      hash_(0),
      mgPST_(0),
      egPST_(0),
      material_(0),
      phase_(0) {
  attacks::init();
  initializeBoard();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Position::newGame() {
  initializeBoard();
  currentTurn_ = Color::WHITE;
  state_ = PositionState::initial();
  hashHistory_.count = 0;
  epIsLegal_ = false;  // starting position has no EP
  hash_ = zobrist::computeHash(bb_, mailbox_, currentTurn_, state_, false);
  recordPosition();
}

bool Position::loadFEN(const std::string& fen) {
  if (!fen::validateFEN(fen)) return false;

  // Save state so we can restore on semantic rejection (e.g. missing king).
  BitboardSet savedBB = bb_;
  Piece savedMailbox[64];
  std::memcpy(savedMailbox, mailbox_, sizeof(mailbox_));
  Color savedTurn = currentTurn_;
  PositionState savedState = state_;

  fen::fenToBoard(fen, bb_, mailbox_, currentTurn_, &state_);
  hashHistory_.count = 0;

  // Locate king positions from bitboards
  int wkIdx = piece::pieceIndex('K');
  int bkIdx = piece::pieceIndex('k');
  kingSquare_[0] = bb_.byPiece[wkIdx] ? lsb(bb_.byPiece[wkIdx]) : SQ_NONE;
  kingSquare_[1] = bb_.byPiece[bkIdx] ? lsb(bb_.byPiece[bkIdx]) : SQ_NONE;

  // Both kings are mandatory — a FEN without either king is invalid.
  if (kingSquare_[0] == SQ_NONE || kingSquare_[1] == SQ_NONE) {
    bb_ = savedBB;
    std::memcpy(mailbox_, savedMailbox, sizeof(mailbox_));
    currentTurn_ = savedTurn;
    state_ = savedState;
    return false;
  }

  recomputeDerived();
  recordPosition();
  return true;
}

// ---------------------------------------------------------------------------
// make() / makeMove() sub-operations
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Remove Capture — handles both en passant and normal captures.
//
// En passant: the captured pawn is on a different square than the move's
// destination (one rank behind the landing square in the pawn's direction).
// Normal capture: the captured piece is at the destination square.
//
// EP path safety contract: mailbox_[epSq] is guaranteed to hold a valid
// enemy pawn (never NONE). This is enforced by the caller chain:
//   • Game::makeMove() → Position::makeMove() → movegen::isValidMove()
//   • Search: isMoveValid() reconstructs flags from current position
//
// Reference: https://www.chessprogramming.org/Make_Move#Captures
// ---------------------------------------------------------------------------
Piece Position::removeCapture(Piece piece, Square to, bool isEP,
                              UndoInfo& undo) {
  if (isEP) {
    // Captured pawn is one rank behind the EP target.
    // White captures northward, so captured pawn is one rank south of 'to'.
    // Black captures southward, so captured pawn is one rank north of 'to'.
    int dir = (piece::pieceColor(piece) == Color::WHITE) ? SOUTH : NORTH;
    Square epSq = to + dir;
    Piece capturedPiece = mailbox_[epSq];
    undo.captured = capturedPiece;
    undo.capturedSquare = epSq;
    bb_.removePiece(epSq, capturedPiece);
    mailbox_[epSq] = Piece::NONE;
    return capturedPiece;
  }

  Piece capturedPiece = mailbox_[to];
  if (capturedPiece != Piece::NONE) {
    undo.captured = capturedPiece;
    bb_.removePiece(to, capturedPiece);
  }
  return capturedPiece;
}

// ---------------------------------------------------------------------------
// Castling Rook — moves the rook from its corner to the castling destination.
//
// Kingside:  rook h1/h8 (col 7) → f1/f8 (col 5)
// Queenside: rook a1/a8 (col 0) → d1/d8 (col 3)
//
// The king has already been moved by the caller; this handles only the rook.
// Updates bitboards, mailbox, and Zobrist hash incrementally.
//
// Reference: https://www.chessprogramming.org/Castling
// ---------------------------------------------------------------------------
void Position::moveCastlingRook(Color color, Square kingFrom, Square kingTo) {
  // Kingside: rook h-file (7) → f-file (5)
  // Queenside: rook a-file (0) → d-file (3)
  int rank = rankOf(kingFrom);
  bool kingSide = fileOf(kingTo) > fileOf(kingFrom);
  Square rookFrom = makeSquare(rank, kingSide ? 7 : 0);
  Square rookTo   = makeSquare(rank, kingSide ? 5 : 3);
  Piece rook = piece::makePiece(color, PieceType::ROOK);

  bb_.movePiece(rookFrom, rookTo, rook);
  mailbox_[rookFrom] = Piece::NONE;
  mailbox_[rookTo]   = rook;

  int rookZIdx = piece::pieceIndex(rook);
  hash_ ^= zobrist::KEYS.pieces[rookZIdx][rookFrom];
  hash_ ^= zobrist::KEYS.pieces[rookZIdx][rookTo];
}

// ---------------------------------------------------------------------------
// Promotion — replaces the pawn at the destination with the promoted piece.
//
// The promoted piece type is encoded in the Move flags (2-bit index).
// Updates bitboards, mailbox, and Zobrist hash.  Returns the new piece.
//
// Reference: https://www.chessprogramming.org/Promotions
// ---------------------------------------------------------------------------
Piece Position::applyPromotion(Move m, Piece pawn, Square to) {
  PieceType promoType = Move::promoTypeFromIndex(m.promoIndex());
  Piece promoted = piece::makePiece(piece::pieceColor(pawn), promoType);
  bb_.removePiece(to, pawn);
  bb_.setPiece(to, promoted);
  mailbox_[to] = promoted;

  int pawnZIdx  = piece::pieceIndex(pawn);
  int promoZIdx = piece::pieceIndex(promoted);
  hash_ ^= zobrist::KEYS.pieces[pawnZIdx][to];
  hash_ ^= zobrist::KEYS.pieces[promoZIdx][to];

  return promoted;
}

// ---------------------------------------------------------------------------
// Unmake Castling Rook — reverse the castling rook during unmake().
//
// Symmetric counterpart to moveCastlingRook(): moves the rook from its
// castling destination back to its original corner square.  Updates bitboards
// and mailbox only (Zobrist hash is restored from UndoInfo, not recomputed).
//
// Reference: https://www.chessprogramming.org/Castling
// ---------------------------------------------------------------------------
void Position::unmakeCastlingRook(Piece king, Square kingFrom, Square kingTo) {
  int rank = rankOf(kingFrom);
  bool kingSide = fileOf(kingTo) > fileOf(kingFrom);
  Square rookFrom = makeSquare(rank, kingSide ? 7 : 0);
  Square rookTo   = makeSquare(rank, kingSide ? 5 : 3);
  Piece rook = piece::makePiece(piece::pieceColor(king), PieceType::ROOK);
  bb_.movePiece(rookTo, rookFrom, rook);
  mailbox_[rookFrom] = rook;
  mailbox_[rookTo] = Piece::NONE;
}

// ---------------------------------------------------------------------------
// Build Move Flags — derives a Move's flag byte from board coordinates.
//
// Inspects the mailbox to determine captures, detects en passant (pawn
// capturing diagonally to an empty square), castling (king moving 2 files),
// and promotion (pawn reaching the back rank).
//
// Reference: https://www.chessprogramming.org/Encoding_Moves
// ---------------------------------------------------------------------------
uint8_t Position::buildMoveFlags(Piece piece, Square from, Square to,
                                 char promotion) const {
  uint8_t flags = 0;
  Piece target = mailbox_[to];
  bool isPawn = piece::pieceType(piece) == PieceType::PAWN;

  if (target != Piece::NONE) {
    flags |= MOVE_CAPTURE;
  } else if (isPawn && fileOf(from) != fileOf(to)) {
    // Pawn captures diagonally to an empty square → en passant
    flags |= MOVE_CAPTURE | MOVE_EP;
  }

  if (piece::pieceType(piece) == PieceType::KING && abs(fileOf(to) - fileOf(from)) == 2)
    flags |= MOVE_CASTLING;

  if (isPawn && rankOf(to) == piece::promotionRank(piece::pieceColor(piece))) {
    PieceType promoType = (promotion != ' ' && promotion != '\0')
        ? piece::charToPieceType(promotion) : PieceType::QUEEN;
    flags |= Move::promoFlags(Move::promoIndexFromType(promoType));
  }

  return flags;
}

// ---------------------------------------------------------------------------
// Game-End Detection — determines the game outcome after a move.
//
// Checks for checkmate, stalemate, threefold repetition, fifty-move rule,
// and insufficient material.  Also detects check (without mate) for UI
// feedback.  Called from makeMove() after the position has been updated.
//
// Reference: https://www.chessprogramming.org/Chess#702
// ---------------------------------------------------------------------------
void Position::detectGameEnd(MoveResult& result) const {
  char winner = ' ';
  GameResult endResult = isGameOver(
      bb_, mailbox_, currentTurn_, state_, hashHistory_, winner);
  result.gameResult = endResult;
  result.winnerColor = winner;

  result.flags &= ~MR_CHECK;
  if (endResult == GameResult::CHECKMATE) {
    result.flags |= MR_CHECK;
  } else if (endResult == GameResult::IN_PROGRESS &&
             attacks::isSquareUnderAttack(
                 bb_, kingSquare_[piece::raw(currentTurn_)], currentTurn_)) {
    result.flags |= MR_CHECK;
  }
}

// ---------------------------------------------------------------------------
// Build MoveResult — translates internal Move flags into UI-facing metadata.
//
// Called by makeMove() after the board mutation is complete.  Reads the Move's
// packed flags (capture, EP, castling, promotion) and derives the original
// piece's color to compute EP captured row and promoted piece type.
//
// Reference: https://www.chessprogramming.org/Encoding_Moves
// ---------------------------------------------------------------------------
MoveResult Position::buildMoveResult(Move m, Piece piece, Square to) const {
  MoveResult result;
  result.flags = MR_VALID;
  if (m.isCapture())   result.flags |= MR_CAPTURE;
  if (m.isEP())        result.flags |= MR_EP;
  if (m.isCastling())  result.flags |= MR_CASTLING;
  if (m.isPromotion()) result.flags |= MR_PROMOTION;
  if (m.isEP()) {
    int dir = (piece::pieceColor(piece) == Color::WHITE) ? SOUTH : NORTH;
    result.epCapturedSq = to + dir;
  } else {
    result.epCapturedSq = SQ_NONE;
  }
  result.promotedTo = m.isPromotion()
      ? piece::makePiece(piece::pieceColor(piece),
                         Move::promoTypeFromIndex(m.promoIndex()))
      : Piece::NONE;
  return result;
}

// ---------------------------------------------------------------------------
// Validated move (game play)
// ---------------------------------------------------------------------------

MoveResult Position::makeMove(Square from, Square to, char promotion) {
  if (from >= 64 || to >= 64)
    return invalidMoveResult();

  Piece piece = mailbox_[from];
  if (piece == Piece::NONE) return invalidMoveResult();
  if (piece::pieceColor(piece) != currentTurn_) return invalidMoveResult();

  if (!movegen::isValidMove(bb_, mailbox_, from, to, state_, kingSquare_[piece::raw(currentTurn_)]))
    return invalidMoveResult();

  // --- Build Move from coordinates ---
  uint8_t flags = buildMoveFlags(piece, from, to, promotion);
  Move m(from, to, flags);

  // --- Delegate board mutation to make(), cache for reverseMove() ---
  UndoInfo undo = make(m);
  undoCache_.move = m;
  undoCache_.undo = undo;
  undoCache_.postHash = hash_;
  undoCache_.valid = true;

  // --- Build MoveResult + detect game end ---
  MoveResult result = buildMoveResult(m, piece, to);
  detectGameEnd(result);

  return result;
}

// ---------------------------------------------------------------------------
// Undo / Redo (for Game history navigation)
// ---------------------------------------------------------------------------

void Position::reverseMove(const MoveEntry& entry) {
  // 1-deep undo cache: if the cache is from this exact forward move
  // (validated by matching post-move hash), delegate to unmake() for O(1)
  // restoration.  Otherwise fall back to manual board reversal + full
  // recomputation (e.g. multi-undo or FEN reload).
  if (undoCache_.valid && hash_ == undoCache_.postHash) {
    unmake(undoCache_.move, undoCache_.undo);
    undoCache_.valid = false;
    return;
  }

  // --- Cache-miss fallback: manual board reversal ---
  Square from = entry.from;
  Square to = entry.to;

  Piece currentAtTo = mailbox_[to];
  bb_.removePiece(to, currentAtTo);
  mailbox_[to] = Piece::NONE;

  bb_.setPiece(from, entry.piece);
  mailbox_[from] = entry.piece;

  if (entry.isEnPassant()) {
    bb_.setPiece(entry.epCapturedSq, entry.captured);
    mailbox_[entry.epCapturedSq] = entry.captured;
  } else if (entry.isCapture()) {
    bb_.setPiece(to, entry.captured);
    mailbox_[to] = entry.captured;
  }

  if (entry.isCastling())
    unmakeCastlingRook(entry.piece, from, to);

  state_ = entry.prevState;
  currentTurn_ = piece::pieceColor(entry.piece);

  if (piece::pieceType(entry.piece) == PieceType::KING)
    kingSquare_[piece::raw(currentTurn_)] = from;

  if (hashHistory_.count > 0)
    hashHistory_.count--;

  recomputeDerived();
}

MoveResult Position::applyMoveEntry(const MoveEntry& entry) {
  return makeMove(entry.from, entry.to,
                  entry.isPromotion() ? piece::pieceToChar(entry.promotion) : ' ');
}

// ---------------------------------------------------------------------------
// Recompute all derived state (hash, PST accumulators, material) from scratch.
// Used after bulk board mutations where incremental tracking is not possible:
// FEN load, reverseMove() cache-miss fallback.
// ---------------------------------------------------------------------------

void Position::recomputeDerived() {
  epIsLegal_ = state_.epSquare != SQ_NONE &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, currentTurn_, state_);
  hash_ = zobrist::computeHash(bb_, mailbox_, currentTurn_, state_, epIsLegal_);
  auto pst = eval::computeMaterialPST(bb_);
  mgPST_ = pst.mg;
  egPST_ = pst.eg;
  material_ = eval::computeMaterial(bb_);
  phase_ = eval::computeGamePhase(bb_);
}

// ---------------------------------------------------------------------------
// Shared accumulator update — capture removal, piece movement, castling rook,
// and promotion deltas for mgPST_, egPST_, material_.  Called by make() after
// all board/hash/state changes are complete.
//
// Parameters:
//   piece       — moving piece (pawn before promotion)
//   from, to    — origin and destination squares
//   captured    — captured piece (Piece::NONE if no capture)
//   capturedSq  — square of captured piece (EP: pawn square; normal: to)
//   isCastling  — whether this is a castling move
//   promotedTo  — promoted piece (Piece::NONE if no promotion)
//
// Reference: https://www.chessprogramming.org/Incremental_Updates
// ---------------------------------------------------------------------------

void Position::updateAccumulators(Piece piece, Square from, Square to,
                                  Piece captured, Square capturedSq,
                                  bool isCastling, Piece promotedTo) {
  // Capture: remove captured piece's contribution.
  if (captured != Piece::NONE) {
    int capIdx = piece::pieceIndex(captured);
    auto cap = eval::pieceSquareMGEG(capIdx, capturedSq);
    mgPST_ -= cap.mg;
    egPST_ -= cap.eg;
    Color capColor = piece::pieceColor(captured);
    int capSign = (capColor == Color::WHITE) ? 1 : -1;
    material_ -= eval::materialValue(piece::pieceType(captured)) * capSign;
    phase_ -= eval::PHASE_WEIGHT[piece::raw(piece::pieceType(captured))];
  }

  // Movement: piece from → to.
  int pieceZIdx = piece::pieceIndex(piece);
  auto pFrom = eval::pieceSquareMGEG(pieceZIdx, from);
  auto pTo = eval::pieceSquareMGEG(pieceZIdx, to);
  mgPST_ += pTo.mg - pFrom.mg;
  egPST_ += pTo.eg - pFrom.eg;

  // Castling rook: derive rook squares from king movement.
  if (isCastling) {
    int rank = rankOf(from);
    bool kingSide = fileOf(to) > fileOf(from);
    Square rookFromSq = makeSquare(rank, kingSide ? 7 : 0);
    Square rookToSq   = makeSquare(rank, kingSide ? 5 : 3);
    Piece rook = piece::makePiece(piece::pieceColor(piece), PieceType::ROOK);
    int rookZIdx = piece::pieceIndex(rook);
    auto rF = eval::pieceSquareMGEG(rookZIdx, rookFromSq);
    auto rT = eval::pieceSquareMGEG(rookZIdx, rookToSq);
    mgPST_ += rT.mg - rF.mg;
    egPST_ += rT.eg - rF.eg;
  }

  // Promotion: swap pawn PST for promoted piece PST + material delta.
  if (promotedTo != Piece::NONE) {
    int promoIdx = piece::pieceIndex(promotedTo);
    auto promoPST = eval::pieceSquareMGEG(promoIdx, to);
    mgPST_ += promoPST.mg - pTo.mg;
    egPST_ += promoPST.eg - pTo.eg;
    int colorSign = (piece::pieceColor(piece) == Color::WHITE) ? 1 : -1;
    material_ += (eval::materialValue(piece::pieceType(promotedTo))
                - eval::materialValue(PieceType::PAWN)) * colorSign;
    phase_ += eval::PHASE_WEIGHT[piece::raw(piece::pieceType(promotedTo))];
  }
}

// ---------------------------------------------------------------------------
// Raw make/unmake (search)
// ---------------------------------------------------------------------------

UndoInfo Position::make(Move m) {
  Square from = m.from;
  Square to = m.to;
  Piece piece = mailbox_[from];

  // --- Save undo state ---
  UndoInfo undo;
  undo.state = state_;
  undo.hash = hash_;
  undo.captured = Piece::NONE;
  undo.capturedSquare = to;
  undo.historyCount = hashHistory_.count;
  undo.mgPST = mgPST_;
  undo.egPST = egPST_;
  undo.material = material_;
  undo.epIsLegal = epIsLegal_;
  undo.phase = phase_;

  // Analyze EP / Castling from flags and mailbox
  bool isEP = m.isEP();
  bool isCastle = m.isCastling();
  bool isPromo = m.isPromotion();

  // --- Hash: remove old state keys ---
  hash_ ^= zobrist::KEYS.castling[state_.castlingRights];
  if (epIsLegal_)
    hash_ ^= zobrist::KEYS.enPassant[fileOf(state_.epSquare)];

  // --- Determine and remove captured piece ---
  Piece capturedPiece = removeCapture(piece, to, isEP, undo);

  // --- Move the piece ---
  bb_.movePiece(from, to, piece);
  mailbox_[from] = Piece::NONE;
  mailbox_[to] = piece;

  // --- Castling rook ---
  if (isCastle)
    moveCastlingRook(piece::pieceColor(piece), from, to);

  // --- Update EP state ---
  bool isPawn = piece::pieceType(piece) == PieceType::PAWN;
  if (isPawn && abs(rankOf(to) - rankOf(from)) == 2) {
    // EP target is the square the pawn skipped over
    state_.epSquare = makeSquare((rankOf(from) + rankOf(to)) / 2, fileOf(from));
  } else {
    state_.epSquare = SQ_NONE;
  }

  // --- Halfmove clock ---
  if (isPawn || capturedPiece != Piece::NONE)
    state_.halfmoveClock = 0;
  else
    state_.halfmoveClock++;

  // --- Hash: piece movements ---
  int pieceZIdx = piece::pieceIndex(piece);
  hash_ ^= zobrist::KEYS.pieces[pieceZIdx][from];
  hash_ ^= zobrist::KEYS.pieces[pieceZIdx][to];

  if (capturedPiece != Piece::NONE) {
    int capIdx = piece::pieceIndex(capturedPiece);
    hash_ ^= zobrist::KEYS.pieces[capIdx][undo.capturedSquare];
  }

  // --- Update castling rights ---
  state_.castlingRights = utils::updateCastlingRights(
      state_.castlingRights, from, to);

  // --- Promotion ---
  Piece promotedTo = isPromo ? applyPromotion(m, piece, to) : Piece::NONE;

  // --- Hash: add new state keys ---
  hash_ ^= zobrist::KEYS.castling[state_.castlingRights];
  hash_ ^= zobrist::KEYS.sideToMove;

  // New EP key (checking with the next side to move = opponent)
  Color nextSide = ~currentTurn_;
  epIsLegal_ = state_.epSquare != SQ_NONE &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, nextSide, state_);
  if (epIsLegal_)
    hash_ ^= zobrist::KEYS.enPassant[fileOf(state_.epSquare)];

  // --- Update king cache ---
  if (piece::pieceType(piece) == PieceType::KING)
    kingSquare_[piece::raw(piece::pieceColor(piece))] = to;

  // --- Incremental material+PST accumulators ---
  updateAccumulators(piece, from, to, capturedPiece, undo.capturedSquare,
                     isCastle, promotedTo);

  // --- Advance turn ---
  if (currentTurn_ == Color::BLACK)
    state_.fullmoveClock++;
  currentTurn_ = nextSide;

  // --- Record position for threefold detection ---
  recordPosition();

  return undo;
}

void Position::unmake(Move m, const UndoInfo& undo) {
  Square from = m.from;
  Square to = m.to;

  // Restore turn first (needed to identify piece color)
  currentTurn_ = ~currentTurn_;
  if (currentTurn_ == Color::BLACK)
    state_.fullmoveClock--;

  // Figure out what's at the destination (may be promoted piece)
  Piece currentAtTo = mailbox_[to];
  Piece originalPiece = currentAtTo;

  // If promoted, the original piece was a pawn of the same color
  if (m.isPromotion())
    originalPiece = piece::makePiece(piece::pieceColor(currentAtTo), PieceType::PAWN);

  // Move piece back
  bb_.removePiece(to, currentAtTo);
  bb_.setPiece(from, originalPiece);
  mailbox_[to] = Piece::NONE;
  mailbox_[from] = originalPiece;

  // Restore captured piece
  if (undo.captured != Piece::NONE) {
    bb_.setPiece(undo.capturedSquare, undo.captured);
    mailbox_[undo.capturedSquare] = undo.captured;
  }

  // Reverse castling rook
  if (m.isCastling())
    unmakeCastlingRook(originalPiece, from, to);

  // Restore king cache
  if (piece::pieceType(originalPiece) == PieceType::KING)
    kingSquare_[piece::raw(currentTurn_)] = from;

  // Restore state and hash from undo (no recomputation!)
  state_ = undo.state;
  hash_ = undo.hash;
  mgPST_ = undo.mgPST;
  egPST_ = undo.egPST;
  material_ = undo.material;
  phase_ = undo.phase;
  epIsLegal_ = undo.epIsLegal;
  hashHistory_.count = undo.historyCount;
}

// ---------------------------------------------------------------------------
// Null move — pass the turn without moving any piece.
//
// Used by null-move pruning in the search.  Flips side-to-move, clears EP
// state, and records the new hash for repetition detection.  No piece state
// is modified, so unmake just restores from the saved UndoInfo.
//
// Reference: https://www.chessprogramming.org/Null_Move
// ---------------------------------------------------------------------------

UndoInfo Position::makeNullMove() {
  UndoInfo undo;
  undo.state = state_;
  undo.hash = hash_;
  undo.captured = Piece::NONE;
  undo.capturedSquare = SQ_NONE;
  undo.mgPST = mgPST_;
  undo.egPST = egPST_;
  undo.material = material_;
  undo.epIsLegal = epIsLegal_;
  undo.phase = phase_;
  undo.historyCount = hashHistory_.count;

  // Remove old EP key (using cached legality — no recomputation needed)
  if (epIsLegal_)
    hash_ ^= zobrist::KEYS.enPassant[fileOf(state_.epSquare)];

  // Clear EP — null move resets en passant opportunity
  state_.epSquare = SQ_NONE;
  epIsLegal_ = false;

  // Flip side to move
  currentTurn_ = ~currentTurn_;
  hash_ ^= zobrist::KEYS.sideToMove;

  // Increment halfmove clock (null move is not a pawn move or capture)
  state_.halfmoveClock++;

  recordPosition();
  return undo;
}

void Position::unmakeNullMove(const UndoInfo& undo) {
  currentTurn_ = ~currentTurn_;
  state_ = undo.state;
  hash_ = undo.hash;
  mgPST_ = undo.mgPST;
  egPST_ = undo.egPST;
  material_ = undo.material;
  phase_ = undo.phase;
  epIsLegal_ = undo.epIsLegal;
  hashHistory_.count = undo.historyCount;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::string Position::getFen() const {
  return fen::boardToFEN(mailbox_, currentTurn_, &state_);
}

std::string Position::boardToText() const {
  std::string result = "====== BOARD ======\n";
  for (int rank = 7; rank >= 0; --rank) {
    result += utils::rankCharFromRank(rank);
    result += ' ';
    for (int file = 0; file < 8; ++file) {
      char c = piece::pieceToChar(mailbox_[makeSquare(rank, file)]);
      result += (c == ' ') ? '.' : c;
      result += ' ';
    }
    result += ' ';
    result += utils::rankCharFromRank(rank);
    result += '\n';
  }
  result += "  a b c d e f g h\n";
  result += "===================";
  return result;
}

// ---------------------------------------------------------------------------
// Instance wrapper methods — delegate to static methods / movegen:: / attacks::.
// Declared in position.h, defined here to avoid pulling movegen.h / attacks.h
// into the header.
// ---------------------------------------------------------------------------

void Position::getPossibleMoves(Square sq, MoveList& moves) const {
  movegen::getPossibleMoves(bb_, mailbox_, sq, state_, moves);
}

bool Position::inCheck() const {
  return attacks::isSquareUnderAttack(bb_, kingSquare_[piece::raw(currentTurn_)], currentTurn_);
}

bool Position::isCheckmate() const {
  return isCheckmate(bb_, mailbox_, currentTurn_, state_);
}

bool Position::isFiftyMoves() const {
  return isFiftyMoveRule(state_);
}

bool Position::isDraw() const {
  return isDraw(bb_, mailbox_, currentTurn_, state_, hashHistory_);
}

bool Position::isRepetition() const {
  return isThreefoldRepetition(hashHistory_);
}

// ---------------------------------------------------------------------------
// Static game-state detection — check, checkmate, stalemate, draw conditions.
//
// Raw bitboard + mailbox interface, testable without constructing a Position.
// Previously in the rules:: namespace; merged here because all callers are
// Position methods and the functions operate on Position's own data types.
//
// Delegates to movegen::hasAnyLegalMove for checkmate/stalemate detection.
// Delegates to attacks::isSquareUnderAttack for check detection.
// ---------------------------------------------------------------------------

bool Position::isCheck(const BitboardSet& bb, Color kingColor) {
  int kidx = piece::pieceIndex(piece::makePiece(kingColor, PieceType::KING));
  Bitboard kingBB = bb.byPiece[kidx];
  if (!kingBB) return false;
  return attacks::isSquareUnderAttack(bb, lsb(kingBB), kingColor);
}

bool Position::isCheckmate(const BitboardSet& bb, const Piece mailbox[],
                           Color kingColor, const PositionState& state) {
  return isCheck(bb, kingColor) && !movegen::hasAnyLegalMove(bb, mailbox, kingColor, state);
}

bool Position::isStalemate(const BitboardSet& bb, const Piece mailbox[],
                           Color colorToMove, const PositionState& state) {
  return !isCheck(bb, colorToMove) && !movegen::hasAnyLegalMove(bb, mailbox, colorToMove, state);
}

bool Position::isInsufficientMaterial(const BitboardSet& bb) {
  // Any pawns, rooks, or queens → sufficient material
  if (bb.byPiece[piece::pieceIndex('P')]
    | bb.byPiece[piece::pieceIndex('p')]) return false;

  if (bb.byPiece[piece::pieceIndex('R')]
    | bb.byPiece[piece::pieceIndex('r')]) return false;

  if (bb.byPiece[piece::pieceIndex('Q')]
    | bb.byPiece[piece::pieceIndex('q')]) return false;

  int wKnightIdx = piece::pieceIndex('N');
  int wBishopIdx = piece::pieceIndex('B');
  int bKnightIdx = piece::pieceIndex('n');
  int bBishopIdx = piece::pieceIndex('b');

  int whiteMinors = popcount(bb.byPiece[wKnightIdx]) + popcount(bb.byPiece[wBishopIdx]);
  int blackMinors = popcount(bb.byPiece[bKnightIdx]) + popcount(bb.byPiece[bBishopIdx]);

  if (whiteMinors > 1 || blackMinors > 1) return false;

  // K vs K
  if (whiteMinors == 0 && blackMinors == 0) return true;

  // K+minor vs K
  if ((whiteMinors == 1 && blackMinors == 0) ||
      (whiteMinors == 0 && blackMinors == 1))
    return true;

  // K+B vs K+B with same-color bishops → insufficient.
  // K+N vs K+B or K+B vs K+N → sufficient (falls through to false).
  if (whiteMinors == 1 && blackMinors == 1) {
    Bitboard wb = bb.byPiece[wBishopIdx];
    Bitboard bBish = bb.byPiece[bBishopIdx];
    if (wb && bBish) {
      bool wOnDark = (wb & DARK_SQUARES) != 0;
      bool bOnDark = (bBish & DARK_SQUARES) != 0;
      return wOnDark == bOnDark;
    }
  }

  return false;
}

bool Position::isThreefoldRepetition(const HashHistory& hashes) {
  if (hashes.count < 5) return false;

  uint64_t current = hashes.keys[hashes.count - 1];
  int count = 1;

  for (int i = hashes.count - 3; i >= 0; i -= 2) {
    if (hashes.keys[i] == current) {
      count++;
      if (count >= 3) return true;
    }
  }
  return false;
}

bool Position::isFiftyMoveRule(const PositionState& state) {
  return state.halfmoveClock >= 100;
}

bool Position::isDraw(const BitboardSet& bb, const Piece mailbox[], Color colorToMove,
                      const PositionState& state, const HashHistory& hashes) {
  return isStalemate(bb, mailbox, colorToMove, state)
      || isFiftyMoveRule(state)
      || isInsufficientMaterial(bb)
      || isThreefoldRepetition(hashes);
}

GameResult Position::isGameOver(const BitboardSet& bb, const Piece mailbox[],
                                Color colorToMove, const PositionState& state,
                                const HashHistory& hashes, char& winner) {
  int kidx = piece::pieceIndex(piece::makePiece(colorToMove, PieceType::KING));
  Bitboard kingBB = bb.byPiece[kidx];
  if (!kingBB) {
    winner = ' ';
    return GameResult::IN_PROGRESS;
  }
  Square kingSq = lsb(kingBB);

  bool inCheck = attacks::isSquareUnderAttack(bb, kingSq, colorToMove);
  bool hasLegal = movegen::hasAnyLegalMove(bb, mailbox, colorToMove, state);

  if (!hasLegal) {
    if (inCheck) {
      winner = piece::colorToChar(~colorToMove);
      return GameResult::CHECKMATE;
    } else {
      winner = 'd';
      return GameResult::STALEMATE;
    }
  }
  if (isFiftyMoveRule(state)) {
    winner = 'd';
    return GameResult::DRAW_50;
  }
  if (isInsufficientMaterial(bb)) {
    winner = 'd';
    return GameResult::DRAW_INSUFFICIENT;
  }
  if (isThreefoldRepetition(hashes)) {
    winner = 'd';
    return GameResult::DRAW_3FOLD;
  }
  winner = ' ';
  return GameResult::IN_PROGRESS;
}

// ---------------------------------------------------------------------------
// En passant / castling analysis — thin wrappers over utils:: free functions.
// ---------------------------------------------------------------------------

EnPassantInfo Position::checkEnPassant(Square from, Square to) const {
  return utils::checkEnPassant(mailbox_, from, to);
}

CastlingInfo Position::checkCastling(Square from, Square to) const {
  return utils::checkCastling(mailbox_, from, to);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void Position::recordPosition() {
  if (state_.halfmoveClock == 0 && hashHistory_.count > 0)
    hashHistory_.count = 0;

  if (hashHistory_.count < HashHistory::MAX_SIZE)
    hashHistory_.keys[hashHistory_.count++] = hash_;
}

}  // namespace LibreChess
