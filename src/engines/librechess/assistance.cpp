#include "engines/librechess/assistance.h"

LibreChessAssistanceProvider::LibreChessAssistanceProvider(LibreChess::Game* game, int level,
                                                           LibreChess::ILogger* logger)
  : game_(game), provider_(game, level, 'w', logger) {}

LibreChessAssistanceProvider::~LibreChessAssistanceProvider() {
  cancel();
}

bool LibreChessAssistanceProvider::initializeIfNeeded() {
  if (initialized_) return true;
  if (!game_) return false;

  EngineInitResult ignored;
  initialized_ = provider_.initialize(ignored);
  return initialized_;
}

bool LibreChessAssistanceProvider::service(BoardBestMoveHint& hint) {
  hint = {};
  if (state_ == State::DISPLAYED) return false;
  if (state_ == State::IDLE) {
    requestBestMove();
    return false;
  }
  return pollBestMove(hint) && hint.valid;
}

bool LibreChessAssistanceProvider::requestBestMove() {
  if (state_ == State::PENDING) return true;
  if (!game_ || game_->isGameOver()) return false;
  if (!initializeIfNeeded()) return false;

  requestedFen_ = game_->getFen();
  provider_.requestMove(requestedFen_);
  state_ = State::PENDING;
  return true;
}

bool LibreChessAssistanceProvider::pollBestMove(BoardBestMoveHint& hint) {
  if (state_ != State::PENDING) return false;

  EngineResult result;
  if (!provider_.checkResult(result)) return false;

  state_ = State::DISPLAYED;
  if (result.type != EngineResult::MOVE || !game_ || game_->getFen() != requestedFen_) {
    requestedFen_.clear();
    hint = {};
    return true;
  }

  hint = mapResult(result.move);
  requestedFen_.clear();
  return true;
}

void LibreChessAssistanceProvider::cancel() {
  provider_.cancelRequest();
  state_ = State::IDLE;
  requestedFen_.clear();
}

BoardBestMoveHint LibreChessAssistanceProvider::mapResult(const std::string& coordinateMove) const {
  BoardBestMoveHint hint;
  char promotion = ' ';
  if (!game_ || !LibreChess::Game::parseCoordinate(coordinateMove, hint.fromRow, hint.fromCol,
                                                   hint.toRow, hint.toCol, promotion)) {
    return hint;
  }

  const auto enPassant = game_->checkEnPassant(hint.fromRow, hint.fromCol, hint.toRow, hint.toCol);
  const bool capturesBoardPiece =
      !LibreChess::Game::isEmptySquare(game_->getSquare(hint.toRow, hint.toCol));

  hint.valid = true;
  hint.capture = capturesBoardPiece || enPassant.isCapture;
  hint.enPassant = enPassant.isCapture;
  hint.capturedRow = enPassant.isCapture ? LibreChess::squareToRow(enPassant.capturedPawnSq)
                                         : hint.toRow;
  hint.capturedCol = hint.toCol;
  return hint;
}
