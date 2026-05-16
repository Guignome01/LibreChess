#include "game_mode.h"
#include "board/programs/game/game_program.h"
#include "game_mode/board_adapter.h"
#include "game.h"
#include "wifi_manager_esp32.h"

GameMode::GameMode(BoardGameProgram* gameplay, WiFiManagerESP32* wm, Game* cg, ILogger* logger)
  : gameplay_(gameplay),
    wifiManager_(wm),
    chess_(cg),
    logger_(logger) {}

GameMode::~GameMode() {
  if (gameplay_) gameplay_->cancelAssistance();
}

bool GameMode::isGameOver() const { return chess_->isGameOver(); }

bool GameMode::tryResumeGame() {
  if (chess_->hasActiveGame()) {
    logger_.info("Resuming live game...");
    return chess_->resumeGame();
  }
  return false;
}

void GameMode::waitForBoardSetup() {
  BoardAdapter::GameRules gameRules(*chess_);
  gameplay_->waitForSetup(gameRules);
}

MoveResult GameMode::applyMove(int fromRow, int fromCol, int toRow, int toCol,
                               char promotion, bool isRemoteMove) {
  // Compute castling info before the move (piece still at from square)
  auto castleInfo = chess_->checkCastling(fromRow, fromCol, toRow, toCol);
  BoardCastlingGuide castleGuide = BoardAdapter::castlingGuide(castleInfo);

  cancelAssistance();

  MoveResult result = chess_->makeMove(fromRow, fromCol, toRow, toCol, promotion);
  if (!result.valid()) return result;

  const BoardMoveCompletion completion =
      BoardAdapter::moveCompletion(result, castleGuide, isRemoteMove);
  const BoardMoveFeedbackData feedback =
      BoardAdapter::moveFeedback(*chess_, result, toRow, toCol);
  gameplay_->completeAppliedMove(completion, feedback, fromRow, fromCol, toRow, toCol);

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
  BoardAdapter::GameRules gameRules(*chess_);
  BoardGameplayResult result = gameplay_->tryPlayerMove(gameRules,
                                                        BoardAdapter::toBoardColor(playerColor),
                                                        selection);
  if (result == BoardGameplayResult::RESIGN_REQUESTED) {
    completeResign(BoardAdapter::toGameColor(selection.resignColor));
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

void GameMode::serviceAssistance() {
  gameplay_->serviceAssistance();
}

void GameMode::cancelAssistance() {
  gameplay_->cancelAssistance();
}

void GameMode::setBoardStateFromFEN(const std::string& fen) {
  cancelAssistance();
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
  return true;
}

bool GameMode::completeResign(Color resignColor) {
  cancelAssistance();
  onBeforeResignConfirm();

  if (!gameplay_->confirmResign(BoardAdapter::toBoardColor(resignColor), isFlipped())) {
    onResignCancelled();
    return false;
  }

  onResignConfirmed(resignColor);
  gameplay_->showResignWinner(BoardAdapter::toBoardColor(resignColor));

  Color winnerColor = ~resignColor;
  chess_->endGame(GameResult::RESIGNATION, winnerColor == Color::WHITE ? 'w' : 'b');
  return true;
}
