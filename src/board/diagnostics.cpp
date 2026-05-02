#include "diagnostics.h"

#include "board.h"

#include <Arduino.h>

BoardDiagnostics::BoardDiagnostics(Board* board) : board_(board), visited_{}, complete_(false), visitedCount_(0) {}

void BoardDiagnostics::begin() {
  Serial.println("Sensor Test: Visit all squares with a piece to complete the test.");
  complete_ = false;
  clearVisited();
  board_->waitForAnimationQueueDrain();
  board_->readSensors();
  recordCurrentOccupancy();
  showVisitedSquares();
  board_->syncOccupancyBaseline();
}

void BoardDiagnostics::update() {
  if (complete_) return;

  board_->readSensors();
  recordChangedOccupancy();
  showVisitedSquares();

  if (visitedCount_ == LibreChess::board::BOARD_SQUARES) {
    complete_ = true;
    Serial.println("Sensor Test complete! All squares verified.");
    board_->fireworkAnimation();
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
      if (board_->occupied(row, col)) recordVisitedSquare(row, col);
    }
  }
}

void BoardDiagnostics::recordChangedOccupancy() {
  for (uint8_t index = 0; index < board_->changedCount(); ++index) {
    auto square = board_->changedSquare(index);
    if (square.valid() && board_->occupied(square.row, square.col))
      recordVisitedSquare(square.row, square.col);
  }
}

void BoardDiagnostics::recordVisitedSquare(int row, int col) {
  if (visited_[row][col]) return;
  visited_[row][col] = true;
  ++visitedCount_;
}

void BoardDiagnostics::showVisitedSquares() {
  Board::LedGuard guard(board_);
  board_->clearAllLEDs(false);

  for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row) {
    for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
      if (visited_[row][col])
        board_->setSquareLED(row, col, LedColors::White);
    }
  }

  board_->showLEDs();
}
