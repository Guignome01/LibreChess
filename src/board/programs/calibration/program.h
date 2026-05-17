#ifndef BOARD_PROGRAMS_CALIBRATION_PROGRAM_H
#define BOARD_PROGRAMS_CALIBRATION_PROGRAM_H

#include "board/services/program/program.h"

// ---------------------------------------------------------------------------
// BoardCalibration — runtime recalibration program.
// ---------------------------------------------------------------------------
// Started via `Board::startProgram(BoardProgramIds::CALIBRATION)`. Clears the
// persisted hall-sensor → square mapping in NVS and reboots so the next boot
// runs the existing interactive calibration routine inside
// `BoardRuntime::begin()`. This is the program-shaped replacement for the
// former `Board::resetCalibration()` helper.
//
// The low-level calibration routine (raw LED sweep, sensor sampling, NVS
// save) lives under `src/board/runtime/calibration.*` and is intentionally
// kept invoked from `BoardRuntime::begin()` so that interactive calibration
// runs before the render task and input task are started — avoiding contention
// with those services for the LED strip and sensor bus.
// ---------------------------------------------------------------------------

class BoardCalibration final : public BoardProgram {
 public:
  BoardCalibration();

  void begin() override {}
  void update() override;
  void cancel() override { complete_ = true; }
  bool isComplete() const override { return complete_; }

 private:
  bool complete_;
};

#endif  // BOARD_PROGRAMS_CALIBRATION_PROGRAM_H
