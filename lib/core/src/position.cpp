#include "position.h"

#include <cstring>

#include "evaluation.h"
#include "fen.h"

namespace LibreChess {

namespace zob = zobrist;

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

Position::Position()
    : currentTurn_(Color::WHITE),
      hash_(0),
      mgPST_(0),
      egPST_(0) {
  attacks::init();
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
  mgPST_ = eval::computeMaterialPST_MG(bb_);
  egPST_ = eval::computeMaterialPST_EG(bb_);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Position::newGame() {
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
  currentTurn_ = Color::WHITE;
  state_ = PositionState::initial();
  hashHistory_.count = 0;
  kingSquare_[0] = squareOf(7, 4);
  kingSquare_[1] = squareOf(0, 4);
  hash_ = zob::computeHash(bb_, mailbox_, currentTurn_, state_);
  mgPST_ = eval::computeMaterialPST_MG(bb_);
  egPST_ = eval::computeMaterialPST_EG(bb_);
  recordPosition();
}

bool Position::loadFEN(const std::string& fen) {
  if (!fen::validateFEN(fen)) return false;

  fen::fenToBoard(fen, bb_, mailbox_, currentTurn_, &state_);
  hashHistory_.count = 0;

  // Locate king positions from bitboards
  int wkIdx = piece::pieceZobristIndex(Piece::W_KING);
  int bkIdx = piece::pieceZobristIndex(Piece::B_KING);
  kingSquare_[0] = bb_.byPiece[wkIdx] ? lsb(bb_.byPiece[wkIdx]) : SQ_NONE;
  kingSquare_[1] = bb_.byPiece[bkIdx] ? lsb(bb_.byPiece[bkIdx]) : SQ_NONE;

  hash_ = zob::computeHash(bb_, mailbox_, currentTurn_, state_);
  mgPST_ = eval::computeMaterialPST_MG(bb_);
  egPST_ = eval::computeMaterialPST_EG(bb_);
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

  MoveResult result;
  result.valid = true;
  applyMoveToBoard(fromRow, fromCol, toRow, toCol, promotion, result);

  advanceTurn();
  recordPosition();
  char winner = ' ';
  GameResult endResult = rules::isGameOver(
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
    auto castle = utils::checkCastling(entry.fromRow, entry.fromCol,
                                             entry.toRow, entry.toCol, entry.piece);
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
  hash_ = zob::computeHash(bb_, mailbox_, currentTurn_, state_);
  mgPST_ = eval::computeMaterialPST_MG(bb_);
  egPST_ = eval::computeMaterialPST_EG(bb_);
}

MoveResult Position::applyMoveEntry(const MoveEntry& entry) {
  return makeMove(entry.fromRow, entry.fromCol, entry.toRow, entry.toCol,
                  entry.isPromotion ? piece::pieceToChar(entry.promotion) : ' ');
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

  int fromRow = rowOf(from), fromCol = colOf(from);
  int toRow = rowOf(to), toCol = colOf(to);

  // Analyze EP / Castling from flags and mailbox
  bool isEP = m.isEP();
  bool isCastle = m.isCastling();
  bool isPromo = m.isPromotion();

  // --- Hash: remove old state keys ---
  hash_ ^= zob::KEYS.castling[state_.castlingRights];
  if (state_.epRow >= 0 && state_.epCol >= 0 &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, currentTurn_, state_))
    hash_ ^= zob::KEYS.enPassant[state_.epCol];

  // --- Determine captured piece ---
  Piece capturedPiece = Piece::NONE;
  if (isEP) {
    int epPawnRow = toRow - piece::pawnDirection(piece::pieceColor(piece));
    Square epSq = squareOf(epPawnRow, toCol);
    capturedPiece = mailbox_[epSq];
    undo.captured = capturedPiece;
    undo.capturedSquare = epSq;

    int capIdx = piece::pieceZobristIndex(capturedPiece);
    mgPST_ -= eval::pieceSquareMG(capIdx, epSq);
    egPST_ -= eval::pieceSquareEG(capIdx, epSq);
    bb_.removePiece(epSq, capturedPiece);
    mailbox_[epSq] = Piece::NONE;
  } else {
    capturedPiece = mailbox_[to];
    if (capturedPiece != Piece::NONE) {
      undo.captured = capturedPiece;
      int capIdx = piece::pieceZobristIndex(capturedPiece);
      mgPST_ -= eval::pieceSquareMG(capIdx, to);
      egPST_ -= eval::pieceSquareEG(capIdx, to);
      bb_.removePiece(to, capturedPiece);
    }
  }

  // --- Move the piece ---
  int pieceZIdx = piece::pieceZobristIndex(piece);
  mgPST_ += eval::pieceSquareMG(pieceZIdx, to) - eval::pieceSquareMG(pieceZIdx, from);
  egPST_ += eval::pieceSquareEG(pieceZIdx, to) - eval::pieceSquareEG(pieceZIdx, from);
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
    int rookZIdx = piece::pieceZobristIndex(rook);
    mgPST_ += eval::pieceSquareMG(rookZIdx, rookTo) - eval::pieceSquareMG(rookZIdx, rookFrom);
    egPST_ += eval::pieceSquareEG(rookZIdx, rookTo) - eval::pieceSquareEG(rookZIdx, rookFrom);
    bb_.movePiece(rookFrom, rookTo, rook);
    mailbox_[rookFrom] = Piece::NONE;
    mailbox_[rookTo] = rook;

    int rookIdx = piece::pieceZobristIndex(rook);
    hash_ ^= zob::KEYS.pieces[rookIdx][rookFrom];
    hash_ ^= zob::KEYS.pieces[rookIdx][rookTo];
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
  hash_ ^= zob::KEYS.pieces[pieceZIdx][from];
  hash_ ^= zob::KEYS.pieces[pieceZIdx][to];

  if (capturedPiece != Piece::NONE) {
    int capIdx = piece::pieceZobristIndex(capturedPiece);
    hash_ ^= zob::KEYS.pieces[capIdx][undo.capturedSquare];
  }

  // --- Update castling rights ---
  state_.castlingRights = utils::updateCastlingRights(
      state_.castlingRights, fromRow, fromCol, toRow, toCol, piece, capturedPiece);

  // --- Promotion ---
  if (isPromo) {
    PieceType promoType = Move::promoTypeFromIndex(m.promoIndex());
    Piece promotedTo = piece::makePiece(piece::pieceColor(piece), promoType);

    int promoIdx = piece::pieceZobristIndex(promotedTo);
    mgPST_ += eval::pieceSquareMG(promoIdx, to) - eval::pieceSquareMG(pieceZIdx, to);
    egPST_ += eval::pieceSquareEG(promoIdx, to) - eval::pieceSquareEG(pieceZIdx, to);
    bb_.removePiece(to, piece);
    bb_.setPiece(to, promotedTo);
    mailbox_[to] = promotedTo;

    // Hash: swap pawn → promoted piece at destination
    hash_ ^= zob::KEYS.pieces[pieceZIdx][to];
    hash_ ^= zob::KEYS.pieces[promoIdx][to];
  }

  // --- Hash: add new state keys ---
  hash_ ^= zob::KEYS.castling[state_.castlingRights];
  hash_ ^= zob::KEYS.sideToMove;

  // New EP key (checking with the next side to move = opponent)
  Color nextSide = ~currentTurn_;
  if (state_.epRow >= 0 && state_.epCol >= 0 &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, nextSide, state_))
    hash_ ^= zob::KEYS.enPassant[state_.epCol];

  // --- Update king cache ---
  if (piece::pieceType(piece) == PieceType::KING)
    kingSquare_[piece::raw(piece::pieceColor(piece))] = to;

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
  undo.historyCount = hashHistory_.count;

  // Remove old EP key (if the current EP square is legal)
  if (state_.epRow >= 0 && state_.epCol >= 0 &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, currentTurn_, state_))
    hash_ ^= zob::KEYS.enPassant[state_.epCol];

  // Clear EP — null move resets en passant opportunity
  state_.epRow = -1;
  state_.epCol = -1;

  // Flip side to move
  currentTurn_ = ~currentTurn_;
  hash_ ^= zob::KEYS.sideToMove;

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
// Internal: apply move to board with incremental Zobrist hash
// ---------------------------------------------------------------------------

void Position::applyMoveToBoard(int fromRow, int fromCol, int toRow, int toCol,
                                char promotion, MoveResult& result) {
  Square from = squareOf(fromRow, fromCol);
  Square to = squareOf(toRow, toCol);
  Piece piece = mailbox_[from];
  Piece capturedPiece = mailbox_[to];

  // --- Analyze ---
  auto ep = utils::checkEnPassant(fromRow, fromCol, toRow, toCol, piece, capturedPiece);
  result.isEnPassant = ep.isCapture;
  result.epCapturedRow = ep.capturedPawnRow;
  result.isPromotion = false;
  result.promotedTo = Piece::NONE;

  auto castle = utils::checkCastling(fromRow, fromCol, toRow, toCol, piece);
  result.isCastling = castle.isCastling;

  // --- Hash: remove old state keys ---
  hash_ ^= zob::KEYS.castling[state_.castlingRights];
  if (state_.epRow >= 0 && state_.epCol >= 0 &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, currentTurn_, state_))
    hash_ ^= zob::KEYS.enPassant[state_.epCol];

  // --- Update EP state ---
  state_.epRow = ep.nextEpRow;
  state_.epCol = ep.nextEpCol;

  // --- Halfmove clock ---
  if (piece::pieceType(piece) == PieceType::PAWN || capturedPiece != Piece::NONE || ep.isCapture)
    state_.halfmoveClock = 0;
  else
    state_.halfmoveClock++;

  // --- Apply board transform ---
  Piece actualCapture;
  utils::applyBoardTransform(bb_, mailbox_, from, to, ep, castle, actualCapture);
  result.isCapture = (actualCapture != Piece::NONE);

  // --- Hash: piece movements ---
  int pieceIdx = piece::pieceZobristIndex(piece);
  hash_ ^= zob::KEYS.pieces[pieceIdx][from];
  hash_ ^= zob::KEYS.pieces[pieceIdx][to];

  if (actualCapture != Piece::NONE) {
    int capIdx = piece::pieceZobristIndex(actualCapture);
    Square capSq = ep.isCapture ? squareOf(ep.capturedPawnRow, colOf(to)) : to;
    hash_ ^= zob::KEYS.pieces[capIdx][capSq];
  }

  if (castle.isCastling) {
    Piece rook = piece::makePiece(piece::pieceColor(piece), PieceType::ROOK);
    int rookIdx = piece::pieceZobristIndex(rook);
    Square rookFrom = squareOf(fromRow, castle.rookFromCol);
    Square rookTo = squareOf(fromRow, castle.rookToCol);
    hash_ ^= zob::KEYS.pieces[rookIdx][rookFrom];
    hash_ ^= zob::KEYS.pieces[rookIdx][rookTo];
  }

  // --- Update castling rights ---
  state_.castlingRights = utils::updateCastlingRights(
      state_.castlingRights, fromRow, fromCol, toRow, toCol, piece, actualCapture);

  // --- Promotion ---
  if (piece::isPromotion(piece, toRow)) {
    result.isPromotion = true;
    Color pieceColor = piece::pieceColor(piece);
    Piece promotedTo;
    if (promotion != ' ' && promotion != '\0')
      promotedTo = piece::makePiece(pieceColor, piece::charToPieceType(promotion));
    else
      promotedTo = piece::makePiece(pieceColor, PieceType::QUEEN);
    result.promotedTo = promotedTo;

    bb_.removePiece(to, piece);
    bb_.setPiece(to, promotedTo);
    mailbox_[to] = promotedTo;

    // Hash: swap pawn → promoted piece at destination
    hash_ ^= zob::KEYS.pieces[pieceIdx][to];
    hash_ ^= zob::KEYS.pieces[piece::pieceZobristIndex(promotedTo)][to];
  }

  // --- Hash: add new state keys ---
  hash_ ^= zob::KEYS.castling[state_.castlingRights];

  // Toggle side to move
  hash_ ^= zob::KEYS.sideToMove;

  // New EP key (checking with the next side to move = opponent)
  Color nextSide = ~currentTurn_;
  if (state_.epRow >= 0 && state_.epCol >= 0 &&
      movegen::hasLegalEnPassantCapture(bb_, mailbox_, nextSide, state_))
    hash_ ^= zob::KEYS.enPassant[state_.epCol];

  // --- Update king cache ---
  if (piece::pieceType(piece) == PieceType::KING)
    kingSquare_[piece::raw(piece::pieceColor(piece))] = to;

  // --- Recompute material+PST accumulators ---
  mgPST_ = eval::computeMaterialPST_MG(bb_);
  egPST_ = eval::computeMaterialPST_EG(bb_);
}

// ---------------------------------------------------------------------------
// Internal: turn advancement + position recording
// ---------------------------------------------------------------------------

void Position::advanceTurn() {
  if (currentTurn_ == Color::BLACK)
    state_.fullmoveClock++;
  currentTurn_ = ~currentTurn_;
}

void Position::recordPosition() {
  if (state_.halfmoveClock == 0 && hashHistory_.count > 0)
    hashHistory_.count = 0;

  if (hashHistory_.count < HashHistory::MAX_SIZE)
    hashHistory_.keys[hashHistory_.count++] = hash_;
}

}  // namespace LibreChess
