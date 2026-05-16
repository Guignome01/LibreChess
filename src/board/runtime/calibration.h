#ifndef BOARD_RUNTIME_CALIBRATION_H
#define BOARD_RUNTIME_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

class BoardDriver;

// ---------------------------------------------------------------------------
// BoardCalibrationRunner — raw startup calibration over BoardDriver.
// ---------------------------------------------------------------------------
// Runs before BoardRenderer starts, so it deliberately uses raw driver sensor
// reads and raw LED writes instead of canvas surfaces or animations. Runtime
// recalibration clears the saved mapping and reboots through Board::resetCalibration().
// ---------------------------------------------------------------------------

class BoardCalibrationRunner {
 public:
  explicit BoardCalibrationRunner(BoardDriver& driver);

  bool load();
  void save();
  bool run();

 private:
  enum class Axis : uint8_t {
    ROWS = 0,
    COLS = 1,
    UNKNOWN = 2,
  };

  BoardDriver& driver_;

  bool waitForBoardEmpty(unsigned long stableMs = 500);
  bool waitForSingleRawPress(int& rawRow, int& rawCol, unsigned long stableMs = 500);
  void showCalibrationError();
  bool calibrateAxis(Axis axis, bool firstAxisSwapped);
  uint8_t axisMapping(Axis axis, int rawIndex) const;
  void setAxisMapping(Axis axis, int rawIndex, uint8_t logicalIndex);
  const char* axisToChessRankFile(Axis axis) const;
};

#endif  // BOARD_RUNTIME_CALIBRATION_H
