#include "diagnostics.h"

#include "layering.h"

#include <Arduino.h>

BoardDiagnostics::BoardDiagnostics(BoardSystem* system, BoardLayering* layering)
  : system_(system), layering_(layering), visited_{}, complete_(false), visitedCount_(0) {}

void BoardDiagnostics::begin() {
  Serial.println("Sensor Test: Visit all squares with a piece to complete the test.");
  complete_ = false;
  clearVisited();
  system_->waitForAnimationQueueDrain();
  system_->readSensors();
  recordCurrentOccupancy();
  showVisitedSquares();
  system_->syncOccupancyBaseline();
}

void BoardDiagnostics::update() {
  if (complete_) return;

  system_->readSensors();
  recordChangedOccupancy();
  showVisitedSquares();

  if (visitedCount_ == LibreChess::board::BOARD_SQUARES) {
    complete_ = true;
    Serial.println("Sensor Test complete! All squares verified.");
    if (layering_) layering_->clearAll(false);
    system_->runAnimation(AnimationJob::firework());
  }
}

void BoardDiagnostics::clearVisited() {
  visitedCount_ = 0;
  for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row) {
    for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
      visited_[row][col] = false;
    }
  }
}

void BoardDiagnostics::recordCurrentOccupancy() {
  for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row) {
    for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
      if (system_->occupied(row, col)) recordVisitedSquare(row, col);
    }
  }
}

void BoardDiagnostics::recordChangedOccupancy() {
  for (uint8_t index = 0; index < system_->changedCount(); ++index) {
    auto square = system_->changedSquare(index);
    if (square.valid() && system_->occupied(square.row, square.col))
      recordVisitedSquare(square.row, square.col);
  }
}

void BoardDiagnostics::recordVisitedSquare(int row, int col) {
  if (visited_[row][col]) return;
  visited_[row][col] = true;
  ++visitedCount_;
}

void BoardDiagnostics::showVisitedSquares() {
  auto drawVisited = [&](auto& leds) {
    leds.clearAllLEDs(false);

    for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row) {
      for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
        if (visited_[row][col])
          leds.setSquareLED(row, col, LedColors::White);
      }
    }

    leds.showLEDs();
  };

  if (layering_)
    layering_->replaceBase(drawVisited);
  else
    system_->batchLEDs(drawVisited);
}
