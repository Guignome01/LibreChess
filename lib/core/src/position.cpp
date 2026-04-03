#include "position.h"

#include <cstring>

#include "evaluation.h"
#include "fen.h"
#include "movegen.h"

namespace LibreChess {

// ---------------------------------------------------------------------------
// Initial board layout
// ---------------------------------------------------------------------------

const Piece Position::INITIAL_BOARD[8][8] = {
    {Piece::B_ROOK, Piece::B_KNIGHT, Piece::B_BISHOP, Piece::B_QUEEN, Piece::B_KING, Piece::B_BISHOP, Piece::B_KNIGHT, Piece::B_ROOK},
    {Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN, Piece::B_PAWN},
    {Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE},
    {Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE},
    {Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE},
    {Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE, Piece::NONE},
    {Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN, Piece::W_PAWN},
    {Piece::W_ROOK, Piece::W_KNIGHT, Piece::W_BISHOP, Piece::W_QUEEN, Piece::W_KING, Piece::W_BISHOP, Piece::W_KNIGHT, Piece::W_ROOK}
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

// Shared board initialization — populates bb_, mailbox_, kingSquare_[],
// and material+PST accumulators from INITIAL_BOARD.
void Position::initializeBoard() {
  bb_.clear();
  memset(mailbox_, 0, sizeof(mailbox_));
  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col) {
      Piece p = INITIAL_BOARD[row][col];
      if (p != Piece::NONE) {
        Square sq = squareOf(row, col);
        bb_.setPiece(sq, p);
        mailbox_[sq] = p;
      }
    }
  kingSquare_[0] = squareOf(7, 4);  // White king at e1
  kingSquare_[1] = squareOf(0, 4);  // Black king at e8
  auto pst = eval::computeMaterialPST(bb_);
  mgPST_ = pst.mg;
  egPST_ = pst.eg;
  material_ = eval::computeMaterial(bb_);
}

Position::Position()
    : currentTurn_(Color::WHITE),
      hash_(0),
      mgPST_(0),
      egPST_(0),
      material_(0) {
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
  int wkIdx = piece::pieceZobristIndex(Piece::W_KING);
  int bkIdx = piece::pieceZobristIndex(Piece::B_KING);
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
// Validated move (game play)
// ---------------------------------------------------------------------------

MoveResult Position::makeMove(int fromRow, int fromCol, int toRow, int toCol, char promotion) {
  if (!utils::isValidSquare(fromRow, fromCol) || !utils::isValidSquare(toRow, toCol))
    return invalidMoveResult();

  Piece piece = getSquare(fromRow, fromCol);
  if (piece == Piece::NONE) return invalidMoveResult();
  if (piece::pieceColor(piece) != currentTurn_) return invalidMoveResult();

  Square from = squareOf(fromRow, fromCol);
  Square to = squareOf(toRow, toCol);
  if (!movegen::isValidMove(bb_, mailbox_, from, to, state_, kingSquare_[piece::raw(currentTurn_)]))
    return invalidMoveResult();

  // --- Build Move flags from coordinates ---
  uint8_t flags = 0;
  Piece target = mailbox_[to];
  bool isPawn = piece::pieceType(piece) == PieceType::PAWN;

  if (target != Piece::NONE) {
    flags |= MOVE_CAPTURE;
  } else if (isPawn && fromCol != toCol) {
    // Pawn captures diagonally to an empty square → en passant
    flags |= MOVE_CAPTURE | MOVE_EP;
  }

  if (piece::pieceType(piece) == PieceType::KING && abs(toCol - fromCol) == 2)
    flags |= MOVE_CASTLING;

  if (isPawn && toRow == piece::promotionRow(piece::pieceColor(piece))) {
    PieceType promoType = (promotion != ' ' && promotion != '\0')
        ? piece::charToPieceType(promotion) : PieceType::QUEEN;
    flags |= Move::promoFlags(Move::promoIndexFromType(promoType));
  }

  Move m(static_cast<uint8_t>(from), static_cast<uint8_t>(to), flags);

  // --- Delegate board mutation to make(), cache for reverseMove() ---
  UndoInfo undo = make(m);
  undoCache_.move = m;
  undoCache_.undo = undo;
  undoCache_.postHash = hash_;
  undoCache_.valid = true;

  // --- Build MoveResult from Move flags ---
  MoveResult result;
  result.valid = true;
  result.isCapture = m.isCapture();
  result.isEnPassant = m.isEP();
  result.epCapturedRow = m.isEP()
      ? (toRow - piece::pawnDirection(piece::pieceColor(piece))) : -1;
  result.isCastling = m.isCastling();
  result.isPromotion = m.isPromotion();
  result.promotedTo = m.isPromotion()
      ? piece::makePiece(piece::pieceColor(piece), Move::promoTypeFromIndex(m.promoIndex()))
      : Piece::NONE;

  // --- Game-end detection ---
  char winner = ' ';
  GameResult endResult = isGameOver(
      bb_, mailbox_, currentTurn_, state_, hashHistory_, winner);
  result.gameResult = endResult;
  result.winnerColor = winner;

  result.isCheck = false;
  if (endResult == GameResult::CHECKMATE) {
    result.isCheck = true;
  } else if (endResult == GameResult::IN_PROGRESS &&
             attacks::isSquareUnderAttack(bb_, kingSquare_[piece::raw(currentTurn_)], currentTurn_)) {
    result.isCheck = true;
  }

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
  Square from = squareOf(entry.fromRow, entry.fromCol);
  Square to = squareOf(entry.toRow, entry.toCol);

  Piece currentAtTo = mailbox_[to];
  bb_.removePiece(to, currentAtTo);
  mailbox_[to] = Piece::NONE;

  bb_.setPiece(from, entry.piece);
  mailbox_[from] = entry.piece;

  if (entry.isEnPassant) {
    Square epSq = squareOf(entry.epCapturedRow, entry.toCol);
    bb_.setPiece(epSq, entry.captured);
    mailbox_[epSq] = entry.captured;
  } else if (entry.isCapture) {
    bb_.setPiece(to, entry.captured);
    mailbox_[to] = entry.captured;
  }

  if (entry.isCastling) {
    auto castle = checkCastling(entry.fromRow, entry.fromCol,
                                entry.toRow, entry.toCol);
    if (castle.isCastling) {
      Square rookTo = squareOf(entry.toRow, castle.rookToCol);
      Square rookFrom = squareOf(entry.toRow, castle.rookFromCol);
      Piece rook = mailbox_[rookTo];
      bb_.movePiece(rookTo, rookFrom, rook);
      mailbox_[rookFrom] = rook;
      mailbox_[rookTo] = Piece::NONE;
    }
  }

  state_ = entry.prevState;
  currentTurn_ = piece::pieceColor(entry.piece);

  if (piece::pieceType(entry.piece) == PieceType::KING)
    kingSquare_[piece::raw(currentTurn_)] = from;

  if (hashHistory_.count > 0)
    hashHistory_.count--;

  recomputeDerived();
}

MoveResult Position::applyMoveEntry(const MoveEntry& entry) {
  return makeMove(entry.fromRow, entry.fromCol, entry.toRow, entry.toCol,
                  entry.isPromotion ? piece::pieceToChar(entry.promotion) : ' ');
}

// ---------------------------------------------------------------------------
// Recompute all derived state (hash, PST accumulators, material) from scratch.
// Used after bulk board mutations where incremental tracking is not possible:
// FEN load, reverseMove() cache-miss fallback.
// ---------------------------------------------------------------------------

void Position::recomputeDerived() {
  bool epLegal = state_.epRow >= 0 && state_.epCol >= 0 &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, currentTurn_, state_);
  hash_ = zobrist::computeHash(bb_, mailbox_, currentTurn_, state_, epLegal);
  auto pst = eval::computeMaterialPST(bb_);
  mgPST_ = pst.mg;
  egPST_ = pst.eg;
  material_ = eval::computeMaterial(bb_);
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
    int capIdx = piece::pieceZobristIndex(captured);
    auto cap = eval::pieceSquareMGEG(capIdx, capturedSq);
    mgPST_ -= cap.mg;
    egPST_ -= cap.eg;
    Color capColor = piece::pieceColor(captured);
    int capSign = (capColor == Color::WHITE) ? 1 : -1;
    material_ -= eval::materialValue(piece::pieceType(captured)) * capSign;
  }

  // Movement: piece from → to.
  int pieceZIdx = piece::pieceZobristIndex(piece);
  auto pFrom = eval::pieceSquareMGEG(pieceZIdx, from);
  auto pTo = eval::pieceSquareMGEG(pieceZIdx, to);
  mgPST_ += pTo.mg - pFrom.mg;
  egPST_ += pTo.eg - pFrom.eg;

  // Castling rook: derive rook squares from king movement.
  if (isCastling) {
    int fromCol = colOf(from);
    int toCol = colOf(to);
    int row = rowOf(from);
    int rookFromCol = (toCol > fromCol) ? 7 : 0;
    int rookToCol = (toCol > fromCol) ? 5 : 3;
    Piece rook = piece::makePiece(piece::pieceColor(piece), PieceType::ROOK);
    int rookZIdx = piece::pieceZobristIndex(rook);
    auto rF = eval::pieceSquareMGEG(rookZIdx, squareOf(row, rookFromCol));
    auto rT = eval::pieceSquareMGEG(rookZIdx, squareOf(row, rookToCol));
    mgPST_ += rT.mg - rF.mg;
    egPST_ += rT.eg - rF.eg;
  }

  // Promotion: swap pawn PST for promoted piece PST + material delta.
  if (promotedTo != Piece::NONE) {
    int promoIdx = piece::pieceZobristIndex(promotedTo);
    auto promoPST = eval::pieceSquareMGEG(promoIdx, to);
    mgPST_ += promoPST.mg - pTo.mg;
    egPST_ += promoPST.eg - pTo.eg;
    int colorSign = (piece::pieceColor(piece) == Color::WHITE) ? 1 : -1;
    material_ += (eval::materialValue(piece::pieceType(promotedTo))
                - eval::materialValue(PieceType::PAWN)) * colorSign;
  }
}

// ---------------------------------------------------------------------------
// Raw make/unmake (search)
// ---------------------------------------------------------------------------

UndoInfo Position::make(Move m) {
  Square from = static_cast<Square>(m.from);
  Square to = static_cast<Square>(m.to);
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

  int fromRow = rowOf(from), fromCol = colOf(from);
  int toRow = rowOf(to), toCol = colOf(to);

  // Analyze EP / Castling from flags and mailbox
  bool isEP = m.isEP();
  bool isCastle = m.isCastling();
  bool isPromo = m.isPromotion();

  // --- Hash: remove old state keys ---
  hash_ ^= zobrist::KEYS.castling[state_.castlingRights];
  if (state_.epRow >= 0 && state_.epCol >= 0 &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, currentTurn_, state_))
    hash_ ^= zobrist::KEYS.enPassant[state_.epCol];

  // --- Determine and remove captured piece ---
  // EP path safety contract: mailbox_[epSq] is guaranteed to hold a valid
  // enemy pawn (never NONE). This is enforced by the caller chain:
  //   • Game::makeMove() → Position::makeMove() → movegen::isValidMove()
  //   • Search: isMoveValid() reconstructs flags from current position
  // If this invariant were violated, pieceZobristIndex(NONE) would return
  // ZOBRIST_IDX_NONE (-1), causing OOB in pieceSquareMG/EG.
  Piece capturedPiece = Piece::NONE;
  if (isEP) {
    int epPawnRow = toRow - piece::pawnDirection(piece::pieceColor(piece));
    Square epSq = squareOf(epPawnRow, toCol);
    capturedPiece = mailbox_[epSq];
    undo.captured = capturedPiece;
    undo.capturedSquare = epSq;
    bb_.removePiece(epSq, capturedPiece);
    mailbox_[epSq] = Piece::NONE;
  } else {
    capturedPiece = mailbox_[to];
    if (capturedPiece != Piece::NONE) {
      undo.captured = capturedPiece;
      bb_.removePiece(to, capturedPiece);
    }
  }

  // --- Move the piece ---
  bb_.movePiece(from, to, piece);
  mailbox_[from] = Piece::NONE;
  mailbox_[to] = piece;

  // --- Castling rook ---
  if (isCastle) {
    int rookFromCol = (toCol > fromCol) ? 7 : 0;  // kingside or queenside
    int rookToCol = (toCol > fromCol) ? 5 : 3;
    Piece rook = piece::makePiece(piece::pieceColor(piece), PieceType::ROOK);
    Square rookFrom = squareOf(fromRow, rookFromCol);
    Square rookTo = squareOf(fromRow, rookToCol);
    bb_.movePiece(rookFrom, rookTo, rook);
    mailbox_[rookFrom] = Piece::NONE;
    mailbox_[rookTo] = rook;

    int rookZIdx = piece::pieceZobristIndex(rook);
    hash_ ^= zobrist::KEYS.pieces[rookZIdx][rookFrom];
    hash_ ^= zobrist::KEYS.pieces[rookZIdx][rookTo];
  }

  // --- Update EP state ---
  bool isPawn = piece::pieceType(piece) == PieceType::PAWN;
  if (isPawn && abs(toRow - fromRow) == 2) {
    state_.epRow = (fromRow + toRow) / 2;
    state_.epCol = fromCol;
  } else {
    state_.epRow = -1;
    state_.epCol = -1;
  }

  // --- Halfmove clock ---
  if (isPawn || capturedPiece != Piece::NONE)
    state_.halfmoveClock = 0;
  else
    state_.halfmoveClock++;

  // --- Hash: piece movements ---
  int pieceZIdx = piece::pieceZobristIndex(piece);
  hash_ ^= zobrist::KEYS.pieces[pieceZIdx][from];
  hash_ ^= zobrist::KEYS.pieces[pieceZIdx][to];

  if (capturedPiece != Piece::NONE) {
    int capIdx = piece::pieceZobristIndex(capturedPiece);
    hash_ ^= zobrist::KEYS.pieces[capIdx][undo.capturedSquare];
  }

  // --- Update castling rights ---
  state_.castlingRights = utils::updateCastlingRights(
      state_.castlingRights, from, to);

  // --- Promotion ---
  Piece promotedTo = Piece::NONE;
  if (isPromo) {
    PieceType promoType = Move::promoTypeFromIndex(m.promoIndex());
    promotedTo = piece::makePiece(piece::pieceColor(piece), promoType);
    bb_.removePiece(to, piece);
    bb_.setPiece(to, promotedTo);
    mailbox_[to] = promotedTo;

    // Hash: swap pawn → promoted piece at destination
    int promoIdx = piece::pieceZobristIndex(promotedTo);
    hash_ ^= zobrist::KEYS.pieces[pieceZIdx][to];
    hash_ ^= zobrist::KEYS.pieces[promoIdx][to];
  }

  // --- Hash: add new state keys ---
  hash_ ^= zobrist::KEYS.castling[state_.castlingRights];
  hash_ ^= zobrist::KEYS.sideToMove;

  // New EP key (checking with the next side to move = opponent)
  Color nextSide = ~currentTurn_;
  if (state_.epRow >= 0 && state_.epCol >= 0 &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, nextSide, state_))
    hash_ ^= zobrist::KEYS.enPassant[state_.epCol];

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
  Square from = static_cast<Square>(m.from);
  Square to = static_cast<Square>(m.to);

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
  if (m.isCastling()) {
    int fromCol = colOf(from);
    int toCol = colOf(to);
    int rookFromCol = (toCol > fromCol) ? 7 : 0;
    int rookToCol = (toCol > fromCol) ? 5 : 3;
    int row = rowOf(from);
    Piece rook = piece::makePiece(piece::pieceColor(originalPiece), PieceType::ROOK);
    Square rookTo = squareOf(row, rookToCol);
    Square rookFrom = squareOf(row, rookFromCol);
    bb_.movePiece(rookTo, rookFrom, rook);
    mailbox_[rookFrom] = rook;
    mailbox_[rookTo] = Piece::NONE;
  }

  // Restore king cache
  if (piece::pieceType(originalPiece) == PieceType::KING)
    kingSquare_[piece::raw(currentTurn_)] = from;

  // Restore state and hash from undo (no recomputation!)
  state_ = undo.state;
  hash_ = undo.hash;
  mgPST_ = undo.mgPST;
  egPST_ = undo.egPST;
  material_ = undo.material;
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
  undo.historyCount = hashHistory_.count;

  // Remove old EP key (if the current EP square is legal)
  if (state_.epRow >= 0 && state_.epCol >= 0 &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, currentTurn_, state_))
    hash_ ^= zobrist::KEYS.enPassant[state_.epCol];

  // Clear EP — null move resets en passant opportunity
  state_.epRow = -1;
  state_.epCol = -1;

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
  for (int row = 0; row < 8; ++row) {
    result += utils::rankChar(row);
    result += ' ';
    for (int col = 0; col < 8; ++col) {
      char c = piece::pieceToChar(mailbox_[squareOf(row, col)]);
      result += (c == ' ') ? '.' : c;
      result += ' ';
    }
    result += ' ';
    result += utils::rankChar(row);
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

void Position::getPossibleMoves(int row, int col, MoveList& moves) const {
  movegen::getPossibleMoves(bb_, mailbox_, row, col, state_, moves);
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
  int kidx = piece::pieceZobristIndex(piece::makePiece(kingColor, PieceType::KING));
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
  int pIdx = piece::pieceZobristIndex(Piece::W_PAWN);
  if (bb.byPiece[pIdx] | bb.byPiece[pIdx + 6]) return false;

  int rIdx = piece::pieceZobristIndex(Piece::W_ROOK);
  if (bb.byPiece[rIdx] | bb.byPiece[rIdx + 6]) return false;

  int qIdx = piece::pieceZobristIndex(Piece::W_QUEEN);
  if (bb.byPiece[qIdx] | bb.byPiece[qIdx + 6]) return false;

  int wKnightIdx = piece::pieceZobristIndex(Piece::W_KNIGHT);
  int wBishopIdx = piece::pieceZobristIndex(Piece::W_BISHOP);
  int bKnightIdx = piece::pieceZobristIndex(Piece::B_KNIGHT);
  int bBishopIdx = piece::pieceZobristIndex(Piece::B_BISHOP);

  int whiteMinors = popcount(bb.byPiece[wKnightIdx]) + popcount(bb.byPiece[wBishopIdx]);
  int blackMinors = popcount(bb.byPiece[bKnightIdx]) + popcount(bb.byPiece[bBishopIdx]);

  if (whiteMinors > 1 || blackMinors > 1) return false;

  // K vs K
  if (whiteMinors == 0 && blackMinors == 0) return true;

  // K+minor vs K
  if ((whiteMinors == 1 && blackMinors == 0) ||
      (whiteMinors == 0 && blackMinors == 1))
    return true;

  // K+B vs K+B with same-color bishops
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
  int kidx = piece::pieceZobristIndex(piece::makePiece(colorToMove, PieceType::KING));
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
// Row/col API preserved for callers; delegates to Square-based free functions.
// ---------------------------------------------------------------------------

EnPassantInfo Position::checkEnPassant(int fromRow, int fromCol,
                                       int toRow, int toCol) const {
  return utils::checkEnPassant(mailbox_, squareOf(fromRow, fromCol),
                               squareOf(toRow, toCol));
}

CastlingInfo Position::checkCastling(int fromRow, int fromCol,
                                     int toRow, int toCol) const {
  return utils::checkCastling(mailbox_, squareOf(fromRow, fromCol),
                              squareOf(toRow, toCol));
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
