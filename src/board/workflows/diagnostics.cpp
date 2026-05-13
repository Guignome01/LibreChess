#include "board/workflows/diagnostics.h"

#include "board/core/colors.h"
#include "board/core/runtime.h"

#include <Arduino.h>

BoardDiagnostics::BoardDiagnostics(BoardRuntime& runtime)
    : runtime_(runtime), surface_(), visited_{}, complete_(false), visitedCount_(0) {}

BoardCanvasHandle BoardDiagnostics::writableSurface(BoardCanvas& canvas) {
  if (!canvas.active(surface_)) {
    surface_ = canvas.acquireSurface();
  }
  canvas.bringToFront(surface_);
  return surface_;
}

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
    if (g.canvas.active(surface_)) g.canvas.clearSurface(surface_);
    g.animations.startFirework(LedColors::Cyan, millis());
  }
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
