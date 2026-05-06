#ifndef BOARD_SERVICES_H
#define BOARD_SERVICES_H

#include "core/system.h"
#include "gui/assistance.h"
#include "gui/feedback.h"
#include "gui/layering.h"
#include "gui/stack.h"

#include <atomic>
#include <stdint.h>

class BoardCalibrationWorkflow;

/// Board-internal services facade. Returned by Board::services() to a bounded
/// friend list of board-mode/workflow classes, and is the only handle those
/// classes use to reach low-level board internals. External firmware never
/// sees BoardServices; workflow classes never see BoardSystem/BoardGui types
/// directly.
class BoardServices {
 public:
  BoardServices(BoardSystem& system, BoardLayering& layering, BoardFeedback& feedback,
                BoardAssistance& assistance, BoardStack& stack);

  BoardServices(const BoardServices&) = delete;
  BoardServices& operator=(const BoardServices&) = delete;

  // --- Sensors ---
  void readSensors() { system_.readSensors(); }
  bool occupied(int row, int col) const { return system_.occupied(row, col); }
  uint16_t sensorReadDelayMs() const { return system_.sensorReadDelayMs(); }

  // --- Animations ---
  bool runAnimation(const AnimationJob& job) { return system_.runAnimation(job); }
  void runAnimationNow(const AnimationJob& job) { system_.runAnimationNow(job); }
  std::atomic<bool>* startAnimation(AnimationType type) { return system_.startAnimation(type); }
  void stopAndWaitForAnimation(std::atomic<bool>*& flag) { system_.stopAndWaitForAnimation(flag); }
  void waitForAnimationQueueDrain() { system_.waitForAnimationQueueDrain(); }

  // --- Visual modules (board-internal, never exposed externally) ---
  BoardLayering& layering() { return layering_; }
  BoardFeedback& feedback() { return feedback_; }
  BoardAssistance& assistance() { return assistance_; }
  BoardStack& stack() { return stack_; }

  // --- LED settings ---
  uint8_t getBrightness() const { return system_.getBrightness(); }
  uint8_t getDimMultiplier() const { return system_.getDimMultiplier(); }
  void setBrightness(uint8_t v) { system_.setBrightness(v); }
  void setDimMultiplier(uint8_t v) { system_.setDimMultiplier(v); }
  void saveLedSettings() { system_.saveLedSettings(); }

  /// Internal accessor for visual-module construction. Workflow classes that
  /// own MenuView/BoardFeedback/BoardAssistance instances pass this reference
  /// to their constructors. Not part of the standard workflow surface --
  /// regular sensor/animation/LED operations should use the forwarded
  /// methods above.
  BoardSystem& system() { return system_; }

  // --- Calibration ---
  BoardCalibrationWorkflow makeCalibrationWorkflow();

 private:
  BoardSystem& system_;
  BoardLayering& layering_;
  BoardFeedback& feedback_;
  BoardAssistance& assistance_;
  BoardStack& stack_;
};

#endif  // BOARD_SERVICES_H
