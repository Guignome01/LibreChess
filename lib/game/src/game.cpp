#include "game.h"

#include <cstring>

#include "notation.h"

namespace LibreChess {

Game::Game(IGameStorage* storage, IGameObserver* observer, ILogger* logger)
    : history_(storage, logger), observer_(observer), logger_(logger), batchDepth_(0),
      batchDirty_(false), gameOver_(false), gameResult_(GameResult::IN_PROGRESS), winnerColor_(' '),
      cachedEval_(0), fenDirty_(true), evalDirty_(true) {}

Game::~Game() {
  delete searchState_;
  if (tt_) { tt_->free(); delete tt_; }
  if (pawnHash_) { pawnHash_->free(); delete pawnHash_; }
  if (evalHash_) { evalHash_->free(); delete evalHash_; }
}

static const char* STANDARD_START_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

void Game::initSearch(int ttSize) {
  if (searchInitialized_) return;

  tt_ = new search::TranspositionTable();
  tt_->resize(ttSize);
  pawnHash_ = new eval::PawnHashTable();
  pawnHash_->resize(eval::DEFAULT_PAWN_HASH_SIZE);
  evalHash_ = new eval::EvalHashTable();
  evalHash_->resize(eval::DEFAULT_EVAL_HASH_SIZE);
  searchState_ = new search::SearchState(nullptr, tt_, pawnHash_, evalHash_);
  searchInitialized_ = true;
}

search::SearchResult Game::calculateMove(const search::SearchLimits& limits) {
  // Build internal limits with our stop flag wired in
  search::SearchLimits internalLimits;
  internalLimits.maxDepth = limits.maxDepth;
  internalLimits.softTimeMs = limits.softTimeMs;
  internalLimits.hardTimeMs = limits.hardTimeMs;

  searchStop_.store(false, std::memory_order_relaxed);
  internalLimits.stop = externalStop_ ? externalStop_ : &searchStop_;

  return search::findBestMove(board_, internalLimits, *searchState_);
}

void Game::setTimeFunc(search::TimeFunc fn) {
  if (searchState_) searchState_->timeFunc = fn;
}

void Game::setExternalStop(std::atomic<bool>* flag) {
  externalStop_ = flag;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Game::newGame() {
  board_.newGame();
  history_.clear();
  startFen_ = STANDARD_START_FEN;
  gameOver_ = false;
  gameResult_ = GameResult::IN_PROGRESS;
  winnerColor_ = ' ';

  // Clear search state if initialized (TT, hash tables, heuristics)
  if (searchInitialized_) {
    tt_->clear();
    pawnHash_->clear();
    evalHash_->clear();
    searchState_->clearHeuristics();
  }

  invalidateCache();
  notifyObserver();
}

void Game::startNewGame(uint8_t playerColor, const uint8_t* meta) {
  newGame();

  // Build header and start recording
  GameHeader header;
  memset(&header, 0, sizeof(header));
  header.result = GameResult::IN_PROGRESS;
  header.winnerColor = '?';
  header.playerColor = playerColor;
  if (meta)
    memcpy(header.meta, meta, GAME_META_SIZE);
  history_.setHeader(header);  // creates live file, no-op if no storage
  history_.snapshotPosition(board_.getFen());  // record initial FEN
}

void Game::endGame(GameResult result, char winnerColor) {
  if (gameOver_) return;  // Guard against double-call

  gameOver_ = true;
  gameResult_ = result;
  winnerColor_ = winnerColor;

  if (winnerColor == ' ' || winnerColor == 'd')
    logger_.infof("Game over: %s", gameResultName(result));
  else
    logger_.infof("Game over: %s \xe2\x80\x94 %s wins!",
                   gameResultName(result), piece::colorName(winnerColor == 'w' ? Color::WHITE : Color::BLACK));

  history_.save(result, winnerColor);  // no-op if not recording
  notifyObserver();
}

void Game::discardRecording() {
  history_.discard();
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

MoveResult Game::makeMove(Square from, Square to, char promotion) {
  if (gameOver_) return invalidMoveResult();

  // Save pre-move state for history
  Piece piece = board_.getSquare(from);
  Piece targetPiece = board_.getSquare(to);
  PositionState prevState = board_.positionState();

  // Delegate move validation, application, and all end-condition detection to board
  // (checkmate, stalemate, 50-move, insufficient material, threefold repetition)
  MoveResult result = board_.makeMove(from, to, promotion);
  if (!result.valid()) return result;

  // --- Logging ---
  const char* moveType = result.isCastling()    ? "castling"
                         : result.isEnPassant() ? "en passant"
                         : result.isCapture()   ? "capture"
                                                : "move";
  logger_.infof("%s: %c %s -> %s", moveType, piece::pieceToChar(piece),
                 utils::squareName(from).c_str(),
                 utils::squareName(to).c_str());
  if (result.isPromotion())
    logger_.infof("Pawn promoted to %c", piece::pieceToChar(result.promotedTo));

  // Build MoveEntry and record in history (addMove handles both in-memory log
  // and persistent recording automatically)
  MoveEntry entry = MoveEntry::build(from, to, piece, targetPiece, result, prevState);
  history_.addMove(entry);

  invalidateCache();

  // Auto-end game on checkmate/stalemate/draw
  if (result.gameResult != GameResult::IN_PROGRESS) {
    endGame(result.gameResult, result.winnerColor);  // calls notifyObserver()
  } else {
    if (result.isCheck()) {
      logger_.infof("%s is in check!", piece::colorName(board_.sideToMove()));
    }
    logger_.infof("It's %s's turn", piece::colorName(board_.sideToMove()));
    notifyObserver();
  }

  return result;
}

MoveResult Game::makeMove(int fromRow, int fromCol, int toRow, int toCol, char promotion) {
  return makeMove(rowColToSquare(fromRow, fromCol),
                  rowColToSquare(toRow, toCol), promotion);
}

MoveResult Game::makeMove(const std::string& move) {
  Square from, to;
  char promotion = ' ';
  if (!notation::parseCoordinate(move, from, to, promotion))
    return invalidMoveResult();
  return makeMove(from, to, promotion);
}

bool Game::loadFEN(const std::string& fen) {
  if (!board_.loadFEN(fen))
    return false;

  history_.clear();
  startFen_ = fen;
  gameOver_ = false;
  gameResult_ = GameResult::IN_PROGRESS;
  winnerColor_ = ' ';
  history_.snapshotPosition(fen);  // no-op if not recording

  invalidateCache();
  notifyObserver();
  return true;
}

// ---------------------------------------------------------------------------
// Undo / Redo
// ---------------------------------------------------------------------------

bool Game::undoMove() {
  const MoveEntry* entry = history_.undoMove();
  if (!entry) return false;
  board_.reverseMove(*entry);
  gameOver_ = false;
  gameResult_ = GameResult::IN_PROGRESS;
  winnerColor_ = ' ';
  invalidateCache();
  notifyObserver();
  return true;
}

bool Game::redoMove() {
  const MoveEntry* entry = history_.redoMove();
  if (!entry) return false;
  MoveResult result = board_.applyMoveEntry(*entry);
  if (!result.valid()) {
    // Shouldn't happen with a valid history, but undo the cursor advance
    history_.undoMove();
    return false;
  }
  if (result.gameResult != GameResult::IN_PROGRESS) {
    gameOver_ = true;
    gameResult_ = result.gameResult;
    winnerColor_ = result.winnerColor;
  }
  invalidateCache();
  notifyObserver();
  return true;
}

// ---------------------------------------------------------------------------
// History queries
// ---------------------------------------------------------------------------

int Game::getHistory(std::string out[], int maxMoves, MoveFormat format) const {
  int count = history_.moveCount();
  if (count > maxMoves) count = maxMoves;

  if (format == MoveFormat::COORDINATE) {
    // Fast path — no board replay needed
    for (int i = 0; i < count; ++i) {
      const MoveEntry& m = history_.getMove(i);
      char promo = m.isPromotion() ? piece::pieceToChar(m.promotion) : ' ';
      out[i] = notation::toCoordinate(m.from, m.to, promo);
    }
    return count;
  }

  // SAN / LAN — replay moves through a temporary board for context
  Position tempBoard;
  if (!startFen_.empty()) {
    tempBoard.loadFEN(startFen_);
  } else {
    tempBoard.newGame();
  }

  for (int i = 0; i < count; ++i) {
    const MoveEntry& m = history_.getMove(i);

    // Generate notation before applying the move
    if (format == MoveFormat::SAN) {
      out[i] = notation::toSAN(tempBoard.bitboards(), tempBoard.mailbox(),
                                     tempBoard.positionState(), m);
    } else {
      out[i] = notation::toLAN(m);
    }

    // Apply the move to advance the temp board
    char promo = m.isPromotion() ? piece::pieceToChar(m.promotion) : ' ';
    tempBoard.makeMove(m.from, m.to, promo);

    // Append check/checkmate suffix
    if (m.isCheck()) {
      out[i] += tempBoard.isCheckmate() ? '#' : '+';
    }
  }

  return count;
}

// ---------------------------------------------------------------------------
// Notation helpers
// ---------------------------------------------------------------------------

std::string Game::toCoordinate(int fromRow, int fromCol, int toRow, int toCol, char promotion) {
  return notation::toCoordinate(rowColToSquare(fromRow, fromCol),
                                rowColToSquare(toRow, toCol), promotion);
}

bool Game::parseCoordinate(const std::string& move, int& fromRow, int& fromCol,
                                int& toRow, int& toCol, char& promotion) {
  Square from, to;
  if (!notation::parseCoordinate(move, from, to, promotion)) return false;
  fromRow = squareToRow(from);
  fromCol = squareToCol(from);
  toRow   = squareToRow(to);
  toCol   = squareToCol(to);
  return true;
}

// ---------------------------------------------------------------------------
// Replay
// ---------------------------------------------------------------------------

bool Game::resumeGame() {
  bool ok = history_.replayInto(board_);
  if (ok) {
    // Use the FEN that replayInto() loaded as the start position
    startFen_ = history_.replayFen();

    // Restore game-over state from the recording header
    GameResult hdrResult = history_.headerResult();
    if (hdrResult != GameResult::IN_PROGRESS) {
      gameOver_ = true;
      gameResult_ = hdrResult;
      winnerColor_ = history_.headerWinnerColor();
    }

    notifyObserver();
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Resume queries
// ---------------------------------------------------------------------------

bool Game::hasActiveGame() {
  return history_.hasActiveGame();
}

bool Game::getActiveGameInfo(uint8_t& playerColor, uint8_t* meta) {
  return history_.getActiveGameInfo(playerColor, meta);
}

// ---------------------------------------------------------------------------
// Batching
// ---------------------------------------------------------------------------

void Game::beginBatch() {
  ++batchDepth_;
}

void Game::endBatch() {
  if (batchDepth_ > 0) --batchDepth_;
  if (batchDepth_ == 0 && batchDirty_) {
    batchDirty_ = false;
    if (observer_)
      observer_->onBoardStateChanged(getFen(), getEvaluation());
  }
}

// ---------------------------------------------------------------------------
// Internal: observer dispatch
// ---------------------------------------------------------------------------

void Game::notifyObserver() {
  if (batchDepth_ > 0) {
    batchDirty_ = true;
    return;
  }
  if (observer_)
    observer_->onBoardStateChanged(getFen(), getEvaluation());
}

// ---------------------------------------------------------------------------
// Cached queries
// ---------------------------------------------------------------------------

std::string Game::getFen() const {
  if (fenDirty_) {
    cachedFen_ = board_.getFen();
    fenDirty_ = false;
  }
  return cachedFen_;
}

int Game::getEvaluation() const {
  if (evalDirty_) {
    cachedEval_ = eval::evaluatePosition(board_.bitboards());
    evalDirty_ = false;
  }
  return cachedEval_;
}

void Game::invalidateCache() {
  fenDirty_ = true;
  evalDirty_ = true;
}

}  // namespace LibreChess
