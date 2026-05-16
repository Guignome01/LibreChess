#include "board/programs/game/visuals/feedback.h"

#include "board/runtime/runtime.h"
#include "board/services/visual/animations.h"

#include <Arduino.h>

// ---------------------------------------------------------------------------
// BoardFeedback implementation
// ---------------------------------------------------------------------------
// All operations acquire the canvas guard for the duration of the change
// (microseconds), then release. Animations are scheduled while the same guard
// is held, so scheduler/canvas state stays synchronized with the renderer.
// ---------------------------------------------------------------------------

namespace {

constexpr float RESIGN_BRIGHTNESS_LEVELS[] = {0.25f, 0.50f, 0.75f, 1.0f};

}  // namespace

BoardFeedback::BoardFeedback(BoardRuntime& runtime, BoardAnimations& animations)
    : BoardVisual(runtime), animations_(animations) {}

void BoardFeedback::clearBoard() {
  clearSurface();
}

void BoardFeedback::clearSquare(int row, int col) {
  BoardVisual::clearSquare(row, col);
}

void BoardFeedback::showMoveResultFeedback(const BoardMoveFeedbackData& feedback) {
  auto g = runtime_.lockCanvas();
  clearSurface(g.canvas);

  const uint32_t now = millis();
  if (feedback.capture) {
    animations_.startCapture(feedback.toRow, feedback.toCol, now);
  } else {
    animations_.startBlink(feedback.toRow, feedback.toCol, LedColors::Green, 1, now);
  }

  if (feedback.promotion) {
    animations_.startPromotion(feedback.toCol, now);
  }

  if (feedback.checkmate) {
    LedRGB winnerColor = LedColors::Cyan;
    if (feedback.winnerColor == 'w') winnerColor = LedColors::White;
    if (feedback.winnerColor == 'b') winnerColor = LedColors::Blue;
    animations_.startFirework(winnerColor, now);
  } else if (feedback.gameEnded) {
    animations_.startFirework(LedColors::Cyan, now);
  } else if (feedback.check && boardSquareInBounds(feedback.checkKingRow, feedback.checkKingCol)) {
    animations_.startBlink(feedback.checkKingRow, feedback.checkKingCol, LedColors::Yellow, 3, now);
  }
}

void BoardFeedback::showIllegalMoveFeedback(int row, int col) {
  auto g = runtime_.lockCanvas();
  animations_.startBlink(row, col, LedColors::Red, 2, millis());
}

void BoardFeedback::showResignProgress(int row, int col, int level, bool clearFirst) {
  if (level < 0 || level >= 4) return;
  auto g = runtime_.lockCanvas();
  if (clearFirst) {
    clearSurface(g.canvas);
  }
  const LedRGB color = LedColors::scaleColor(LedColors::Orange, RESIGN_BRIGHTNESS_LEVELS[level]);
  g.canvas.setPixel(writableSurface(g.canvas), row, col, color);
}

void BoardFeedback::clearResignFeedback(int row, int col) {
  BoardVisual::clearSquare(row, col);
}

void BoardFeedback::showWinner(BoardPieceColor winnerColor) {
  auto g = runtime_.lockCanvas();
  clearSurface(g.canvas);
  animations_.startFirework(winnerColor == BoardPieceColor::WHITE ? LedColors::White
                                                                  : LedColors::Blue,
                             millis());
}

void BoardFeedback::showRemoteGameEnd(char winnerColor) {
  auto g = runtime_.lockCanvas();
  clearSurface(g.canvas);
  LedRGB ledColor = LedColors::Cyan;
  if (winnerColor == 'w') ledColor = LedColors::White;
  if (winnerColor == 'b') ledColor = LedColors::Blue;
  animations_.startFirework(ledColor, millis());
}

void BoardFeedback::showError() {
  auto g = runtime_.lockCanvas();
  animations_.startFlash(LedColors::Red, 3, millis());
}

BoardAnimationHandle BoardFeedback::startThinking() {
  auto g = runtime_.lockCanvas();
  return animations_.startThinking(millis());
}

BoardAnimationHandle BoardFeedback::startWaiting() {
  auto g = runtime_.lockCanvas();
  return animations_.startWaiting(millis());
}

void BoardFeedback::stopAnimation(BoardAnimationHandle& handle) {
  auto g = runtime_.lockCanvas();
  animations_.cancel(handle);
}
