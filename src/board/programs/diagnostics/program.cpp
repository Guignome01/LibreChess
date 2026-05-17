#include "board/programs/diagnostics/program.h"

#include "board/runtime/colors.h"
#include "board/runtime/runtime.h"
#include "board/runtime/helpers.h"
#include "board/services/visual/animations.h"

#include <Arduino.h>

BoardDiagnostics::BoardDiagnostics(BoardRuntime& runtime, BoardAnimations& animations)
    : BoardVisual(runtime),
      animations_(animations),
      visited_{},
      complete_(false),
      visitedCount_(0) {}

void BoardDiagnostics::begin() {
  Serial.println("Sensor Test: Visit all squares with a piece to complete the test.");
  complete_ = false;
  clearVisited();
  recordCurrentOccupancy();
  paintVisited();
}

void BoardDiagnostics::update() {
  if (complete_) return;

  recordCurrentOccupancy();
  paintVisited();

  if (visitedCount_ == SQUARES) {
    complete_ = true;
    Serial.println("Sensor Test complete! All squares verified.");
    auto g = runtime_.lockCanvas();
    clearSurface(g.canvas);
    animations_.startFirework(LedColors::Cyan, millis());
  }
}

void BoardDiagnostics::cancel() {
  complete_ = true;
  clearSurface();
}

void BoardDiagnostics::clearVisited() {
  visitedCount_ = 0;
  for (int row = 0; row < ROWS; ++row)
    for (int col = 0; col < COLS; ++col) visited_[row][col] = false;
}

void BoardDiagnostics::recordCurrentOccupancy() {
  bool occupied[ROWS][COLS];
  runtime_.copyInputOccupancy(occupied);
  for (int row = 0; row < ROWS; ++row)
    for (int col = 0; col < COLS; ++col)
      if (occupied[row][col]) recordVisitedSquare(row, col);
}

void BoardDiagnostics::recordVisitedSquare(int row, int col) {
  if (visited_[row][col]) return;
  visited_[row][col] = true;
  ++visitedCount_;
}

void BoardDiagnostics::paintVisited() {
  auto g = runtime_.lockCanvas();
  BoardCanvasHandle surface = writableSurface(g.canvas);
  g.canvas.clearSurface(surface);
  for (int row = 0; row < ROWS; ++row)
    for (int col = 0; col < COLS; ++col)
      if (visited_[row][col]) g.canvas.setPixel(surface, row, col, LedColors::White);
}
