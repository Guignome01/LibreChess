#include "game_mode.h"
#include "board/gameplay.h"
#include "game.h"
#include "wifi_manager_esp32.h"

GameMode::GameMode(BoardGameplay* gameplay, WiFiManagerESP32* wm, Game* cg, ILogger* logger)
  : gameplay_(gameplay), wifiManager_(wm), chess_(cg), logger_(logger) {}

bool GameMode::isGameOver() const { return chess_->isGameOver(); }

bool GameMode::tryResumeGame() {
  if (chess_->hasActiveGame()) {
    logger_.info("Resuming live game...");
    return chess_->resumeGame();
  }
  return false;
}

void GameMode::waitForBoardSetup() {
  gameplay_->waitForSetup(*chess_, logger_);
}

MoveResult GameMode::applyMove(int fromRow, int fromCol, int toRow, int toCol, char promotion, bool isRemoteMove) {
  // Compute castling info before the move (piece still at from square)
  auto castleInfo = chess_->checkCastling(fromRow, fromCol, toRow, toCol);

  MoveResult result = chess_->makeMove(fromRow, fromCol, toRow, toCol, promotion);
  if (!result.valid()) return result;

  gameplay_->completeAppliedMove(*chess_, result, castleInfo, fromRow, fromCol, toRow, toCol, isRemoteMove, logger_);

  return result;
}

MoveResult GameMode::applyMove(const std::string& move) {
  int fromRow, fromCol, toRow, toCol;
  char promotion = ' ';
  if (!Game::parseCoordinate(move, fromRow, fromCol, toRow, toCol, promotion)) {
    logger_.errorf("Failed to parse move: %s", move.c_str());
    return invalidMoveResult();
  }
  return applyMove(fromRow, fromCol, toRow, toCol, promotion, true);
}

bool GameMode::tryPlayerMove(Color playerColor, int& fromRow, int& fromCol, int& toRow, int& toCol) {
  BoardGameplayMove selection;
  BoardGameplayResult result = gameplay_->tryPlayerMove(*chess_, playerColor, logger_, selection);
  if (result == BoardGameplayResult::RESIGN_REQUESTED) {
    completeResign(selection.resignColor);
    return false;
  }

  if (result != BoardGameplayResult::MOVE)
    return false;

  fromRow = selection.fromRow;
  fromCol = selection.fromCol;
  toRow = selection.toRow;
  toCol = selection.toCol;
  return true;
}

void GameMode::setBoardStateFromFEN(const std::string& fen) {
  chess_->loadFEN(fen);
  logger_.infof("Board state set from FEN: %s", fen.c_str());
  logger_.info(chess_->boardToText().c_str());
}

// ---------------------------
// Resign Feature
// ---------------------------

bool GameMode::processResign() {
  if (!resignPending_) return false;
  resignPending_ = false;
  completeResign(chess_->sideToMove());
  gameplay_->syncOccupancyBaseline();
  return true;
}

bool GameMode::completeResign(Color resignColor) {
  onBeforeResignConfirm();

  if (!gameplay_->confirmResign(resignColor, isFlipped(), logger_)) {
    onResignCancelled();
    return false;
  }

  onResignConfirmed(resignColor);
  gameplay_->showResignWinner(resignColor);

  Color winnerColor = ~resignColor;
  chess_->endGame(GameResult::RESIGNATION, winnerColor == Color::WHITE ? 'w' : 'b');
  return true;
}
