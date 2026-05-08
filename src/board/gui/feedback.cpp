#include "board/gui/feedback.h"

#include "board/core/runtime.h"
#include "board/gui/layers.h"

#include <Arduino.h>

// ---------------------------------------------------------------------------
// BoardFeedback implementation
// ---------------------------------------------------------------------------
// All operations acquire the canvas guard for the duration of the change
// (microseconds), then release. Effects are started via the canvas guard's
// effects reference; the renderer task picks up the change on the next
// tick.
// ---------------------------------------------------------------------------

namespace {

constexpr float RESIGN_BRIGHTNESS_LEVELS[] = {0.25f, 0.50f, 0.75f, 1.0f};

}  // namespace

BoardFeedback::BoardFeedback(BoardRuntime& runtime) : runtime_(runtime) {}

void BoardFeedback::clearBoard() {
  auto g = runtime_.lockCanvas();
  g.canvas.clearLayer(BoardLayer::FEEDBACK);
}

void BoardFeedback::clearSquare(int row, int col) {
  auto g = runtime_.lockCanvas();
  g.canvas.clearLayerSquare(BoardLayer::FEEDBACK, row, col);
}

void BoardFeedback::showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol,
                                           const LibreChess::Game& game) {
  auto g = runtime_.lockCanvas();
  g.canvas.clearLayer(BoardLayer::FEEDBACK);

  const uint32_t now = millis();
  if (result.isCapture()) {
    g.effects.startCapture(toRow, toCol, now);
  } else {
    g.effects.startBlink(toRow, toCol, LedColors::Green, 1, now, BoardLayer::FEEDBACK);
  }

  if (result.isPromotion()) {
    g.effects.startPromotion(toCol, now);
  }

  if (result.gameResult == LibreChess::GameResult::CHECKMATE) {
    g.effects.startFirework(LedColors::forWinner(result.winnerColor), now);
  } else if (result.gameResult != LibreChess::GameResult::IN_PROGRESS) {
    g.effects.startFirework(LedColors::Cyan, now);
  } else if (result.isCheck()) {
    LibreChess::Color turn = game.sideToMove();
    g.effects.startBlink(game.kingRow(turn), game.kingCol(turn), LedColors::Yellow, 3, now,
                         BoardLayer::FEEDBACK);
  }
}

void BoardFeedback::showIllegalMoveFeedback(int row, int col) {
  auto g = runtime_.lockCanvas();
  g.effects.startBlink(row, col, LedColors::Red, 2, millis(), BoardLayer::FEEDBACK);
}

void BoardFeedback::showResignProgress(int row, int col, int level, bool clearFirst) {
  if (level < 0 || level >= 4) return;
  auto g = runtime_.lockCanvas();
  if (clearFirst) {
    g.canvas.clearLayer(BoardLayer::FEEDBACK);
  }
  const LedRGB color = LedColors::scaleColor(LedColors::Orange, RESIGN_BRIGHTNESS_LEVELS[level]);
  g.canvas.setPixel(BoardLayer::FEEDBACK, row, col, color);
}

void BoardFeedback::clearResignFeedback(int row, int col) {
  auto g = runtime_.lockCanvas();
  g.canvas.clearLayerSquare(BoardLayer::FEEDBACK, row, col);
}

void BoardFeedback::showWinner(LibreChess::Color winnerColor) {
  auto g = runtime_.lockCanvas();
  g.canvas.clearLayer(BoardLayer::FEEDBACK);
  g.effects.startFirework(LedColors::forPieceColor(winnerColor), millis());
}

void BoardFeedback::showRemoteGameEnd(char winnerColor) {
  auto g = runtime_.lockCanvas();
  g.canvas.clearLayer(BoardLayer::FEEDBACK);
  g.effects.startFirework(LedColors::forWinner(winnerColor), millis());
}

void BoardFeedback::showError() {
  auto g = runtime_.lockCanvas();
  g.effects.startFlash(LedColors::Red, 3, millis());
}

BoardEffectHandle BoardFeedback::startThinking() {
  auto g = runtime_.lockCanvas();
  return g.effects.startThinking(millis());
}

BoardEffectHandle BoardFeedback::startWaiting() {
  auto g = runtime_.lockCanvas();
  return g.effects.startWaiting(millis());
}

void BoardFeedback::stopAnimation(BoardEffectHandle& handle) {
  auto g = runtime_.lockCanvas();
  g.effects.cancel(handle);
}
