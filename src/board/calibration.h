#ifndef BOARD_CALIBRATION_H
#define BOARD_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

#include "workflow.h"

class BoardDriver;
class BoardController;

// Board calibration workflow used by startup calibration and application/WiFi
// trigger flows. External callers may only trigger recalibration; BoardController
// is the sole owner of load/run/save startup calibration.
class BoardCalibration : private BoardWorkflow {
 public:
  explicit BoardCalibration(BoardController& board);

  void trigger();

 private:
  friend class BoardController;

  explicit BoardCalibration(BoardDriver& driver);
  bool load();
  void save();
  bool run();

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
