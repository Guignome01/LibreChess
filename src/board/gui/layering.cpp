#include "layering.h"

#include "board/core/controller.h"

BoardLayerWriter::BoardLayerWriter(BoardLayering& layering, BoardLayerTarget target)
    : layering_(layering), target_(target), rendered_(false) {}

void BoardLayerWriter::clearAllLEDs(bool show) {
  layering_.clearTarget(target_);
  if (show) showLEDs();
}

void BoardLayerWriter::setSquareLED(int row, int col, LedRGB color) {
  layering_.setSquare(target_, row, col, color);
}

void BoardLayerWriter::showLEDs() {
  layering_.render();
  rendered_ = true;
}

BoardLayering::BoardLayering(BoardController& board) : board_(board), base_{}, overlay_{}, overlayEnabled_{} {
  clearBaseFrame();
  clearOverlayFrame();
}

void BoardLayering::clearBase(bool show) {
  clearBaseFrame();
  if (show) render();
}

void BoardLayering::clearOverlay(bool show) {
  clearOverlayFrame();
  if (show) render();
}

void BoardLayering::clearAll(bool show) {
  clearBaseFrame();
  clearOverlayFrame();
  if (show) render();
}

void BoardLayering::clearBaseSquare(int row, int col, bool show) {
  setSquare(BoardLayerTarget::BASE, row, col, LedColors::Off);
  if (show) render();
}

void BoardLayering::clearOverlaySquare(int row, int col, bool show) {
  if (!validSquare(row, col)) return;
  overlay_[row][col] = LedColors::Off;
  overlayEnabled_[row][col] = false;
  if (show) render();
}

void BoardLayering::render() {
  board_.batchLEDs([&](BoardController::LEDWriter& leds) {
    for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row) {
      for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
        LedRGB color = overlayEnabled_[row][col] ? overlay_[row][col] : base_[row][col];
        leds.setSquareLED(row, col, color);
      }
    }
    leds.showLEDs();
  });
}

void BoardLayering::runTemporaryAnimation(const AnimationJob& job) {
  board_.waitForAnimationQueueDrain();
  if (board_.runAnimation(job))
    board_.waitForAnimationQueueDrain();
  render();
}

bool BoardLayering::validSquare(int row, int col) {
  return row >= 0 && row < LibreChess::board::BOARD_ROWS &&
         col >= 0 && col < LibreChess::board::BOARD_COLS;
}

void BoardLayering::clearBaseFrame() {
  for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row) {
    for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
      base_[row][col] = LedColors::Off;
    }
  }
}

void BoardLayering::clearOverlayFrame() {
  for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row) {
    for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
      overlay_[row][col] = LedColors::Off;
      overlayEnabled_[row][col] = false;
    }
  }
}

void BoardLayering::setSquare(BoardLayerTarget target, int row, int col, LedRGB color) {
  if (!validSquare(row, col)) return;

  if (target == BoardLayerTarget::BASE) {
    base_[row][col] = color;
    return;
  }

  overlay_[row][col] = color;
  overlayEnabled_[row][col] = true;
}

void BoardLayering::clearTarget(BoardLayerTarget target) {
  if (target == BoardLayerTarget::BASE)
    clearBaseFrame();
  else
    clearOverlayFrame();
}

void BoardLayering::renderIfNeeded(const LayerWriter& writer) {
  if (!writer.rendered()) render();
}
