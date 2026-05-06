#ifndef BOARD_CALIBRATION_H
#define BOARD_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

class BoardDriver;
class Board;

// External board calibration mode used by application and WiFi flows.
class BoardCalibration {
 public:
  explicit BoardCalibration(Board& board);

  void trigger();

 private:
  Board& board_;
};

// Board-internal calibration workflow and NVS mapping persistence.
class BoardCalibrationWorkflow {
 public:
  explicit BoardCalibrationWorkflow(BoardDriver& driver);

  bool load();
  void save();
  bool run();
  void trigger();

 private:
  enum class Axis : uint8_t {
    ROWS = 0,
    COLS = 1,
    UNKNOWN = 2,
  };

  BoardDriver& driver_;

  void readRawSensors(bool (&rawState)[8][8]);
  bool waitForBoardEmpty(unsigned long stableMs = 500);
  bool waitForSingleRawPress(int& rawRow, int& rawCol, unsigned long stableMs = 500);
  void showCalibrationError();
  bool calibrateAxis(Axis axis, bool firstAxisSwapped);
  uint8_t axisMapping(Axis axis, int rawIndex) const;
  void setAxisMapping(Axis axis, int rawIndex, uint8_t logicalIndex);
  const char* axisToChessRankFile(Axis axis) const;
};

#endif  // BOARD_CALIBRATION_H
