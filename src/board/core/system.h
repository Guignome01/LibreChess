#ifndef BOARD_SYSTEM_H
#define BOARD_SYSTEM_H

#include "colors.h"
#include "driver.h"
#include "scheduler.h"
#include "state.h"

#include <atomic>
#include <stdint.h>
#include <utility>

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

/// Board-internal service boundary for low-level driver and scheduler operations.
class BoardSystem {
 public:
  using LEDWriter = BoardLEDBatch;

  BoardSystem();

  /// Initialize low-level hardware and the animation scheduler.
  bool begin();

  /// Poll debounced sensor state from the low-level driver and refresh the
  /// physical occupancy transition snapshot.
  void readSensors();

  /// Reset the transition baseline to the current physical occupancy.
  void syncOccupancyBaseline();

  /// Return current physical occupancy for a logical square.
  bool occupied(int row, int col) const;

  /// Return previous physical occupancy for a logical square.
  bool wasOccupied(int row, int col) const;

  /// Return whether a piece was lifted from a square on the latest poll.
  bool wasLifted(int row, int col) const;

  /// Return whether a piece was placed on a square on the latest poll.
  bool wasPlaced(int row, int col) const;

  /// Return whether a square changed on the latest poll.
  bool changed(int row, int col) const;

  /// Return how many squares changed on the latest poll.
  uint8_t changedCount() const;

  /// Return one changed square, or an invalid square when out of range.
  LibreChess::board::BoardSquare changedSquare(uint8_t index) const;

  /// Run one batch of direct LED writes while exposing only LED operations to
  /// the callback.
  template <typename UpdateFn>
  void batchLEDs(UpdateFn&& update) {
    beginLEDBatch();
    LEDWriter leds(driver_);
    std::forward<UpdateFn>(update)(leds);
    endLEDBatch();
  }

  /// Clear all LEDs through a scheduler-owned direct batch.
  void clearAllLEDs(bool show = true);

  /// Set one square LED through a scheduler-owned direct batch.
  void setSquareLED(int row, int col, LedRGB color);

  /// Flush LED changes through a scheduler-owned direct batch.
  void showLEDs();

  /// Queue one prebuilt fire-and-forget animation job.
  bool runAnimation(const AnimationJob& job);

  /// Execute one prebuilt animation immediately under the LED mutex.
  void runAnimationNow(const AnimationJob& job);

  /// Queue a cancellable animation type and return its stop flag.
  std::atomic<bool>* startAnimation(AnimationType type);

  /// Stop a cancellable animation and wait until its worker finishes.
  void stopAndWaitForAnimation(std::atomic<bool>*& stopFlag);

  /// Wait until all queued animation work before this call has completed.
  void waitForAnimationQueueDrain();

  /// Return configured LED strip brightness.
  uint8_t getBrightness() const;

  /// Return configured dark-square dim multiplier.
  uint8_t getDimMultiplier() const;

  /// Set LED strip brightness under the scheduler-owned LED mutex.
  void setBrightness(uint8_t value);

  /// Set the dim multiplier under the scheduler-owned LED mutex.
  void setDimMultiplier(uint8_t value);

  /// Persist current LED settings.
  void saveLedSettings();

  /// Request calibration on the next driver initialization path.
  void triggerCalibration();

  /// Return the expected polling delay used by the debounced sensor scan.
  uint16_t sensorReadDelayMs() const;

 private:
  BoardDriver driver_;
  BoardScheduler scheduler_;
  LibreChess::board::BoardState state_;

  void beginLEDBatch();
  void endLEDBatch();
  void refreshState(bool initializeBaseline);
};

#endif  // BOARD_SYSTEM_H