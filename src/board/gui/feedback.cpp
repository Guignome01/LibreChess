#include "feedback.h"

#include "animations.h"
#include "layering.h"

namespace {

static constexpr float RESIGN_BRIGHTNESS_LEVELS[] = {0.25f, 0.50f, 0.75f, 1.0f};

}  // namespace

BoardFeedback::BoardFeedback(BoardSystem& system, BoardLayering& layering)
    : system_(system), layering_(layering) {}

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
    system_.runAnimation(AnimationJob::capture(toRow, toCol));
  else
    confirmSquareCompletion(toRow, toCol);

  if (result.isPromotion())
    system_.runAnimation(AnimationJob::promotion(toCol));

  if (result.gameResult == LibreChess::GameResult::CHECKMATE) {
    system_.runAnimation(AnimationJob::firework(LedColors::forWinner(result.winnerColor)));
  } else if (result.gameResult != LibreChess::GameResult::IN_PROGRESS) {
    system_.runAnimation(AnimationJob::firework(LedColors::Cyan));
  } else if (result.isCheck()) {
    LibreChess::Color turn = game.sideToMove();
    system_.runAnimation(
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
  system_.runAnimation(AnimationJob::firework(LedColors::forPieceColor(winnerColor)));
}

void BoardFeedback::showRemoteGameEnd(char winnerColor) {
  layering_.clearAll(false);
  system_.runAnimation(AnimationJob::firework(LedColors::forWinner(winnerColor)));
}

void BoardFeedback::showError() {
  layering_.runTemporaryAnimation(AnimationJob::flash(LedColors::Red));
}

std::atomic<bool>* BoardFeedback::startThinking() {
  system_.waitForAnimationQueueDrain();
  return system_.startAnimation(AnimationType::THINKING);
}

std::atomic<bool>* BoardFeedback::startWaiting() {
  system_.waitForAnimationQueueDrain();
  return system_.startAnimation(AnimationType::WAITING);
}

void BoardFeedback::stopAnimation(std::atomic<bool>*& stopFlag) {
  system_.stopAndWaitForAnimation(stopFlag);
  layering_.render();
}

void BoardFeedback::confirmSquareCompletion(int row, int col) {
  system_.runAnimation(AnimationJob::blink(row, col, LedColors::Green, 1));
}
