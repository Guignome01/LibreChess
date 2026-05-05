#include "feedback.h"

#include "board/core/layering.h"

namespace {

static constexpr float RESIGN_BRIGHTNESS_LEVELS[] = {0.25f, 0.50f, 0.75f, 1.0f};

}  // namespace

BoardFeedback::BoardFeedback(BoardSystem* system, BoardLayering* layering) : system_(system), layering_(layering) {}

void BoardFeedback::clearBoard(bool show) {
  if (layering_) {
    layering_->clearAll(show);
    return;
  }
  system_->clearAllLEDs(show);
}

void BoardFeedback::clearSquare(int row, int col) {
  if (layering_) {
    layering_->clearBaseSquare(row, col);
    return;
  }
  system_->batchLEDs([&](BoardSystem::LEDWriter& leds) {
    leds.setSquareLED(row, col, LedColors::Off);
    leds.showLEDs();
  });
}

void BoardFeedback::showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol, const LibreChess::Game& game) {
  if (layering_) layering_->clearAll(false);

  if (result.isCapture())
    system_->runAnimation(AnimationJob::capture(toRow, toCol));
  else
    confirmSquareCompletion(toRow, toCol);

  if (result.isPromotion())
    system_->runAnimation(AnimationJob::promotion(toCol));

  if (result.gameResult == LibreChess::GameResult::CHECKMATE) {
    system_->runAnimation(AnimationJob::firework(LedColors::forWinner(result.winnerColor)));
  } else if (result.gameResult != LibreChess::GameResult::IN_PROGRESS) {
    system_->runAnimation(AnimationJob::firework(LedColors::Cyan));
  } else if (result.isCheck()) {
    LibreChess::Color turn = game.sideToMove();
    system_->runAnimation(AnimationJob::blink(game.kingRow(turn), game.kingCol(turn), LedColors::Yellow, 3, true, true));
  }
}

void BoardFeedback::showIllegalMoveFeedback(int row, int col) {
  if (layering_) {
    layering_->runTemporaryAnimation(AnimationJob::blink(row, col, LedColors::Red, 2));
    return;
  }
  system_->runAnimation(AnimationJob::blink(row, col, LedColors::Red, 2));
}

void BoardFeedback::showResignProgress(int row, int col, int level, bool clearFirst) {
  if (layering_) {
    if (clearFirst) {
      layering_->clearBase(false);
      layering_->clearOverlay(false);
    }
    layering_->updateOverlay([&](BoardLayering::LayerWriter& leds) {
      leds.setSquareLED(row, col, LedColors::scaleColor(LedColors::Orange, RESIGN_BRIGHTNESS_LEVELS[level]));
      leds.showLEDs();
    });
    return;
  }
  system_->batchLEDs([&](BoardSystem::LEDWriter& leds) {
    if (clearFirst) leds.clearAllLEDs(false);
    leds.setSquareLED(row, col, LedColors::scaleColor(LedColors::Orange, RESIGN_BRIGHTNESS_LEVELS[level]));
    leds.showLEDs();
  });
}

void BoardFeedback::clearResignFeedback(int row, int col) {
  if (layering_) {
    layering_->clearOverlaySquare(row, col);
    return;
  }
  clearSquare(row, col);
}

void BoardFeedback::showWinner(LibreChess::Color winnerColor) {
  if (layering_) layering_->clearAll(false);
  system_->runAnimation(AnimationJob::firework(LedColors::forPieceColor(winnerColor)));
}

void BoardFeedback::showRemoteGameEnd(char winnerColor) {
  if (layering_) layering_->clearAll(false);
  system_->runAnimation(AnimationJob::firework(LedColors::forWinner(winnerColor)));
}

void BoardFeedback::showError() {
  if (layering_) {
    layering_->runTemporaryAnimation(AnimationJob::flash(LedColors::Red));
    return;
  }
  system_->runAnimation(AnimationJob::flash(LedColors::Red));
}

std::atomic<bool>* BoardFeedback::startThinking() {
  system_->waitForAnimationQueueDrain();
  return system_->startAnimation(AnimationType::THINKING);
}

std::atomic<bool>* BoardFeedback::startWaiting() {
  system_->waitForAnimationQueueDrain();
  return system_->startAnimation(AnimationType::WAITING);
}

void BoardFeedback::stopAnimation(std::atomic<bool>*& stopFlag) {
  system_->stopAndWaitForAnimation(stopFlag);
  if (layering_) layering_->render();
}

void BoardFeedback::confirmSquareCompletion(int row, int col) {
  system_->runAnimation(AnimationJob::blink(row, col, LedColors::Green, 1));
}