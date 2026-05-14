#include "board/gui/feedback.h"

#include "board/core/runtime.h"

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
    : runtime_(runtime), animations_(animations), surface_() {}

BoardCanvasHandle BoardFeedback::writableSurface(BoardCanvas& canvas) {
  if (!canvas.active(surface_)) {
    surface_ = canvas.acquireSurface();
  }
  canvas.bringToFront(surface_);
  return surface_;
}

void BoardFeedback::clearBoard() {
  auto g = runtime_.lockCanvas();
  if (g.canvas.active(surface_)) g.canvas.clearSurface(surface_);
}

void BoardFeedback::clearSquare(int row, int col) {
  auto g = runtime_.lockCanvas();
  if (g.canvas.active(surface_)) g.canvas.clearSurfaceSquare(surface_, row, col);
}

void BoardFeedback::showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol,
                                           const LibreChess::Game& game) {
  auto g = runtime_.lockCanvas();
  if (g.canvas.active(surface_)) g.canvas.clearSurface(surface_);

  const uint32_t now = millis();
  if (result.isCapture()) {
    animations_.startCapture(toRow, toCol, now);
  } else {
    animations_.startBlink(toRow, toCol, LedColors::Green, 1, now);
  }

  if (result.isPromotion()) {
    animations_.startPromotion(toCol, now);
  }

  if (result.gameResult == LibreChess::GameResult::CHECKMATE) {
    LedRGB winnerColor = LedColors::Cyan;
    if (result.winnerColor == 'w') winnerColor = LedColors::White;
    if (result.winnerColor == 'b') winnerColor = LedColors::Blue;
    animations_.startFirework(winnerColor, now);
  } else if (result.gameResult != LibreChess::GameResult::IN_PROGRESS) {
    animations_.startFirework(LedColors::Cyan, now);
  } else if (result.isCheck()) {
    LibreChess::Color turn = game.sideToMove();
    animations_.startBlink(game.kingRow(turn), game.kingCol(turn), LedColors::Yellow, 3, now);
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
    if (g.canvas.active(surface_)) g.canvas.clearSurface(surface_);
  }
  const LedRGB color = LedColors::scaleColor(LedColors::Orange, RESIGN_BRIGHTNESS_LEVELS[level]);
  g.canvas.setPixel(writableSurface(g.canvas), row, col, color);
}

void BoardFeedback::clearResignFeedback(int row, int col) {
  auto g = runtime_.lockCanvas();
  if (g.canvas.active(surface_)) g.canvas.clearSurfaceSquare(surface_, row, col);
}

void BoardFeedback::showWinner(LibreChess::Color winnerColor) {
  auto g = runtime_.lockCanvas();
  if (g.canvas.active(surface_)) g.canvas.clearSurface(surface_);
  animations_.startFirework(winnerColor == LibreChess::Color::WHITE ? LedColors::White
                                                                    : LedColors::Blue,
                             millis());
}

void BoardFeedback::showRemoteGameEnd(char winnerColor) {
  auto g = runtime_.lockCanvas();
  if (g.canvas.active(surface_)) g.canvas.clearSurface(surface_);
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
