#ifndef BOARD_WORKFLOWS_CALIBRATION_H
#define BOARD_WORKFLOWS_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

class BoardDriver;
class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardCalibration — startup + on-demand board calibration workflow.
// ---------------------------------------------------------------------------
// External callers (firmware / WiFi) may only `trigger()` recalibration.
// `BoardRuntime` is the sole owner of the startup load/run/save flow.
//
// Calibration runs against `BoardDriver` directly because (a) the renderer
// task hasn't been started yet at startup, and (b) calibration needs raw
// (uncalibrated) sensor reads + raw LED writes that bypass canvas/animations.
// ---------------------------------------------------------------------------

class BoardCalibration {
 public:
  /// Public construction takes the runtime so external triggers can clear
  /// NVS + reboot. The driver reference comes from the runtime via friend.
  explicit BoardCalibration(BoardRuntime& runtime);

  void trigger();

 private:
  friend class BoardRuntime;

  /// Constructor used by `BoardRuntime` during startup, before the
  /// renderer task is running. Operates on the driver directly.
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

#endif  // BOARD_WORKFLOWS_CALIBRATION_H
