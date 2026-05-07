#ifndef BOARD_CORE_CONTROLLER_H
#define BOARD_CORE_CONTROLLER_H

#include "board/core/driver.h"
#include "board/core/scheduler.h"
#include "board/gui/assistance.h"
#include "board/gui/feedback.h"
#include "board/gui/layering.h"
#include "board/gui/stack.h"

#include <atomic>
#include <stdint.h>
#include <utility>

class BoardCalibration;

/// LED-only adapter used during batched strip updates.
class BoardLEDBatch {
 public:
  /// Bind this LED batch adapter to the low-level driver it safely narrows.
  explicit BoardLEDBatch(BoardDriver& driver);

  /// Clear all logical square LEDs, optionally showing the strip immediately.
  void clearAllLEDs(bool show = true);

  /// Set one logical square LED color.
  void setSquareLED(int row, int col, LedRGB color);

  /// Flush pending LED color changes to the strip.
  void showLEDs();

  /// Set global LED brightness and refresh the strip.
  void setBrightness(uint8_t value);

  /// Set the dark-square dim multiplier and refresh the strip.
  void setDimMultiplier(uint8_t value);

 private:
  BoardDriver& driver_;
};

/// Board-internal runtime owner shared by all board workflows. External
/// firmware consumes the public Board package root instead of this class.
class BoardController {
 public:
  using LEDWriter = BoardLEDBatch;

  BoardController();

  BoardController(const BoardController&) = delete;
  BoardController& operator=(const BoardController&) = delete;

  /// Initialize low-level hardware, startup calibration, and scheduler resources.
  bool begin();

  /// Poll debounced sensors.
  void readSensors();

  /// Return current debounced occupancy for a logical square.
  bool occupied(int row, int col) const;

  /// Run one batch of direct LED writes while exposing only LED operations to
  /// the callback.
  template <typename UpdateFn>
  void batchLEDs(UpdateFn&& update) {
    beginLEDBatch();
    LEDWriter leds(driver_);
    std::forward<UpdateFn>(update)(leds);
    endLEDBatch();
  }

  /// Queue one fire-and-forget animation.
  bool runAnimation(const AnimationJob& job);

  /// Run one animation immediately under the LED mutex.
  void runAnimationNow(const AnimationJob& job);

  /// Queue a cancellable animation type and return its stop flag.
  std::atomic<bool>* startAnimation(AnimationType type);

  /// Stop a cancellable animation and wait until its worker finishes.
  void stopAndWaitForAnimation(std::atomic<bool>*& stopFlag);

  /// Wait until queued animation work before this call has completed.
  void waitForAnimationQueueDrain();

  /// Return configured LED strip brightness.
  uint8_t getBrightness() const;

  /// Return configured dark-square dim multiplier.
  uint8_t getDimMultiplier() const;

  /// Set LED strip brightness.
  void setBrightness(uint8_t value);

  /// Set dark-square dim multiplier.
  void setDimMultiplier(uint8_t value);

  /// Persist current LED settings.
  void saveLedSettings();

  /// Return the debounced sensor polling cadence.
  uint16_t sensorReadDelayMs() const;

  /// Clear all persistent board visuals.
  void clearAllLEDs(bool show = true);

  /// Run the synchronous WiFi-connecting animation.
  void showConnectingAnimation();

  /// Persistent visual layer compositor.
  BoardLayering& layering();

  /// Mandatory status/outcome visual feedback.
  BoardFeedback& feedback();

  /// Optional physical guidance visuals.
  BoardAssistance& assistance();

  /// Modal board drawable stack.
  BoardStack& stack();

 private:
  friend class BoardCalibration;

  BoardDriver driver_;
  BoardScheduler scheduler_;
  BoardLayering layering_;
  BoardFeedback feedback_;
  BoardAssistance assistance_;
  BoardStack stack_;

  void beginLEDBatch();
  void endLEDBatch();
};

#endif  // BOARD_CORE_CONTROLLER_H