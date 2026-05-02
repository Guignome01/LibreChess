#ifndef BOARD_CALIBRATION_H
#define BOARD_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

class BoardDriver;

// Board-internal calibration workflow and NVS mapping persistence.
class BoardCalibration {
 public:
  explicit BoardCalibration(BoardDriver* driver);

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

  BoardDriver* driver_;

  void readRawSensors(bool (&rawState)[8][8]);
  bool waitForBoardEmpty(unsigned long stableMs = 500);
  bool waitForSingleRawPress(int& rawRow, int& rawCol, unsigned long stableMs = 500);
  void showCalibrationError();
  bool calibrateAxis(Axis axis, uint8_t* axisPinsOrder, size_t pinCount, bool firstAxisSwapped);
  const char* axisToChessRankFile(Axis axis) const;
};

#endif  // BOARD_CALIBRATION_H
