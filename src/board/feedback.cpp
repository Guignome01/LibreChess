#include "feedback.h"

#include "board.h"

namespace {

static constexpr float RESIGN_BRIGHTNESS_LEVELS[] = {0.25f, 0.50f, 0.75f, 1.0f};

}  // namespace

BoardFeedback::BoardFeedback(Board* board) : board_(board) {}

void BoardFeedback::clearBoard(bool show) {
  Board::LedGuard guard(board_);
  board_->clearAllLEDs(show);
}

void BoardFeedback::clearSquare(int row, int col) {
  Board::LedGuard guard(board_);
  board_->setSquareLED(row, col, LedColors::Off);
  board_->showLEDs();
}

void BoardFeedback::showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol, const LibreChess::Game& game) {
  if (result.isCapture())
    board_->captureAnimation(toRow, toCol);
  else
    confirmSquareCompletion(toRow, toCol);

  if (result.isPromotion())
    board_->promotionAnimation(toCol);

  if (result.gameResult == LibreChess::GameResult::CHECKMATE) {
    board_->fireworkAnimation(LedColors::forWinner(result.winnerColor));
  } else if (result.gameResult != LibreChess::GameResult::IN_PROGRESS) {
    board_->fireworkAnimation(LedColors::Cyan);
  } else if (result.isCheck()) {
    LibreChess::Color turn = game.sideToMove();
    board_->blinkSquare(game.kingRow(turn), game.kingCol(turn), LedColors::Yellow, 3, true, true);
  }
}

void BoardFeedback::showIllegalMoveFeedback(int row, int col) {
  board_->blinkSquare(row, col, LedColors::Red, 2);
}

void BoardFeedback::showResignProgress(int row, int col, int level, bool clearFirst) {
  Board::LedGuard guard(board_);
  if (clearFirst) board_->clearAllLEDs(false);
  board_->setSquareLED(row, col, LedColors::scaleColor(LedColors::Orange, RESIGN_BRIGHTNESS_LEVELS[level]));
  board_->showLEDs();
}

void BoardFeedback::clearResignFeedback(int row, int col) {
  clearSquare(row, col);
}

void BoardFeedback::showWinner(LibreChess::Color winnerColor) {
  board_->fireworkAnimation(LedColors::forPieceColor(winnerColor));
}

void BoardFeedback::showRemoteGameEnd(char winnerColor) {
  board_->fireworkAnimation(LedColors::forWinner(winnerColor));
}

void BoardFeedback::showError() {
  board_->flashBoardAnimation(LedColors::Red);
}

std::atomic<bool>* BoardFeedback::startThinking() {
  board_->waitForAnimationQueueDrain();
  return board_->startThinkingAnimation();
}

std::atomic<bool>* BoardFeedback::startWaiting() {
  board_->waitForAnimationQueueDrain();
  return board_->startWaitingAnimation();
}

void BoardFeedback::stopAnimation(std::atomic<bool>*& stopFlag) {
  board_->stopAndWaitForAnimation(stopFlag);
}

void BoardFeedback::confirmSquareCompletion(int row, int col) {
  board_->blinkSquare(row, col, LedColors::Green, 1);
}