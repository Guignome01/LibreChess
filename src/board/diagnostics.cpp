#include "diagnostics.h"

#include "board.h"
#include "gui/animations.h"
#include "gui/layering.h"
#include "services.h"

#include <Arduino.h>

BoardDiagnostics::BoardDiagnostics(Board& board)
    : services_(board.services()), visited_{}, complete_(false), visitedCount_(0) {}

void BoardDiagnostics::begin() {
  Serial.println("Sensor Test: Visit all squares with a piece to complete the test.");
  complete_ = false;
  clearVisited();
  services_.waitForAnimationQueueDrain();
  services_.readSensors();
  recordCurrentOccupancy();
  showVisitedSquares();
}

void BoardDiagnostics::update() {
  if (complete_) return;

  services_.readSensors();
  recordCurrentOccupancy();
  showVisitedSquares();

  if (visitedCount_ == LibreChess::board::BOARD_SQUARES) {
    complete_ = true;
    Serial.println("Sensor Test complete! All squares verified.");
    services_.layering().clearAll(false);
    services_.runAnimation(AnimationJob::firework());
  }
}

void BoardDiagnostics::clearVisited() {
  visitedCount_ = 0;
  for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row)
    for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col)
      visited_[row][col] = false;
}

void BoardDiagnostics::recordCurrentOccupancy() {
  for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row)
    for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col)
      if (services_.occupied(row, col)) recordVisitedSquare(row, col);
}

void BoardDiagnostics::recordVisitedSquare(int row, int col) {
  if (visited_[row][col]) return;
  visited_[row][col] = true;
  ++visitedCount_;
}

void BoardDiagnostics::showVisitedSquares() {
  services_.layering().replaceBase([&](BoardLayering::LayerWriter& leds) {
    leds.clearAllLEDs(false);
    for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row)
      for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col)
        if (visited_[row][col]) leds.setSquareLED(row, col, LedColors::White);
    leds.showLEDs();
  });
}
