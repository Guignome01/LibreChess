#ifndef SENSOR_TEST_H
#define SENSOR_TEST_H

#include "board_driver.h"

// ---------------------------
// Sensor Test Mode Class
// ---------------------------
class SensorTest {
 private:
  BoardDriver* boardDriver_;
  bool visited_[8][8]; // Track which squares have been visited_
  bool complete_;      // True when all squares visited_

 public:
  SensorTest(BoardDriver* bd);
  void begin();
  void update();
  bool isComplete() const { return complete_; }
};

#endif // SENSOR_TEST_H