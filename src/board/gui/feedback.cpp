#include "feedback.h"

#include "animations.h"
#include "board/core/controller.h"
#include "layering.h"

namespace {

static constexpr float RESIGN_BRIGHTNESS_LEVELS[] = {0.25f, 0.50f, 0.75f, 1.0f};

}  // namespace

BoardFeedback::BoardFeedback(BoardController& board, BoardLayering& layering)
  : board_(board), layering_(layering) {}

void BoardFeedback::clearBoard(bool show) {
  layering_.clearAll(show);
}

void BoardFeedback::clearSquare(int row, int col) {
  layering_.clearBaseSquare(row, col);
}

void BoardFeedback::showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol,
                                           const LibreChess::Game& game) {
  layering_.clearAll(false);

  if (result.isCapture())
    board_.runAnimation(AnimationJob::capture(toRow, toCol));
  else
    confirmSquareCompletion(toRow, toCol);

  if (result.isPromotion())
    board_.runAnimation(AnimationJob::promotion(toCol));

  if (result.gameResult == LibreChess::GameResult::CHECKMATE) {
    board_.runAnimation(AnimationJob::firework(LedColors::forWinner(result.winnerColor)));
  } else if (result.gameResult != LibreChess::GameResult::IN_PROGRESS) {
    board_.runAnimation(AnimationJob::firework(LedColors::Cyan));
  } else if (result.isCheck()) {
    LibreChess::Color turn = game.sideToMove();
    board_.runAnimation(
        AnimationJob::blink(game.kingRow(turn), game.kingCol(turn), LedColors::Yellow, 3, true, true));
  }
}

void BoardFeedback::showIllegalMoveFeedback(int row, int col) {
  layering_.runTemporaryAnimation(AnimationJob::blink(row, col, LedColors::Red, 2));
}

void BoardFeedback::showResignProgress(int row, int col, int level, bool clearFirst) {
  if (clearFirst) {
    layering_.clearBase(false);
    layering_.clearOverlay(false);
  }
  layering_.updateOverlay([&](BoardLayering::LayerWriter& leds) {
    leds.setSquareLED(row, col, LedColors::scaleColor(LedColors::Orange, RESIGN_BRIGHTNESS_LEVELS[level]));
    leds.showLEDs();
  });
}

void BoardFeedback::clearResignFeedback(int row, int col) {
  layering_.clearOverlaySquare(row, col);
}

void BoardFeedback::showWinner(LibreChess::Color winnerColor) {
  layering_.clearAll(false);
  board_.runAnimation(AnimationJob::firework(LedColors::forPieceColor(winnerColor)));
}

void BoardFeedback::showRemoteGameEnd(char winnerColor) {
  layering_.clearAll(false);
  board_.runAnimation(AnimationJob::firework(LedColors::forWinner(winnerColor)));
}

void BoardFeedback::showError() {
  layering_.runTemporaryAnimation(AnimationJob::flash(LedColors::Red));
}

std::atomic<bool>* BoardFeedback::startThinking() {
  board_.waitForAnimationQueueDrain();
  return board_.startAnimation(AnimationType::THINKING);
}

std::atomic<bool>* BoardFeedback::startWaiting() {
  board_.waitForAnimationQueueDrain();
  return board_.startAnimation(AnimationType::WAITING);
}

void BoardFeedback::stopAnimation(std::atomic<bool>*& stopFlag) {
  board_.stopAndWaitForAnimation(stopFlag);
  layering_.render();
}

void BoardFeedback::confirmSquareCompletion(int row, int col) {
  board_.runAnimation(AnimationJob::blink(row, col, LedColors::Green, 1));
}
