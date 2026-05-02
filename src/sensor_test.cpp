#include "sensor_test.h"
#include <Arduino.h>

SensorTest::SensorTest(BoardDriver* bd) : boardDriver_(bd), complete_(false) {
  memset(visited_, false, sizeof(visited_));
}

void SensorTest::begin() {
  Serial.println("Sensor Test: Visit all squares with a piece to complete_ the test!");
  complete_ = false;
  memset(visited_, false, sizeof(visited_));
  boardDriver_->clearAllLEDs();
}

void SensorTest::update() {
  if (complete_) return;

  boardDriver_->readSensors();
  boardDriver_->clearAllLEDs(false);

  int visitedCount = 0;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      if (boardDriver_->getSensorState(row, col))
        visited_[row][col] = true;
      if (visited_[row][col]) {
        boardDriver_->setSquareLED(row, col, LedColors::White);
        visitedCount++;
      }
    }
  }
  boardDriver_->showLEDs();
  if (visitedCount == 64) {
    complete_ = true;
    Serial.println("Sensor Test complete_! All squares verified");
    boardDriver_->fireworkAnimation();
  }
}