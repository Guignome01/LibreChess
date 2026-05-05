#ifndef BOARD_ANIMATIONS_H
#define BOARD_ANIMATIONS_H

#include "colors.h"

#include <atomic>
#include <stdint.h>

class BoardLEDBatch;

// Animation job types for the board async queue.
// SYNC is a no-op queue barrier used by waitForAnimationQueueDrain().
enum class AnimationType : uint8_t {
  CAPTURE,
  PROMOTION,
  BLINK,
  WAITING,
  THINKING,
  FIREWORK,
  FLASH,
  CONNECTING,
  SYNC
};

// Animation job with parameters stored in the FreeRTOS animation queue.
struct AnimationJob {
  AnimationType type;
  std::atomic<bool>* stopFlag;
  union {
    struct {
      int row, col;
    } capture;
    struct {
      int col;
    } promotion;
    struct {
      int row, col;
      LedRGB color;
      int times;
      bool clearAfter;
      bool clearBefore;
    } blink;
    struct {
      LedRGB color;
      int times;
    } flash;
    struct {
      LedRGB color;
    } firework;
  } params;

  /// Build a capture animation centered on one logical board square.
  static AnimationJob capture(int row, int col);

  /// Build a promotion animation for one logical board file/column.
  static AnimationJob promotion(int col);

  /// Build a square blink animation.
  static AnimationJob blink(int row, int col, LedRGB color, int times = 3,
                            bool clearAfter = true, bool clearBefore = false);

  /// Build a full-board firework animation.
  static AnimationJob firework(LedRGB color = LedColors::White);

  /// Build a full-board flash animation.
  static AnimationJob flash(LedRGB color, int times = 3);

  /// Build a cancellable thinking-status animation.
  static AnimationJob thinking(std::atomic<bool>* stopFlag);

  /// Build a cancellable waiting-status animation.
  static AnimationJob waiting(std::atomic<bool>* stopFlag);

  /// Build the one-shot WiFi connecting animation.
  static AnimationJob connecting();

  /// Build a no-op queue barrier used to wait for prior jobs.
  static AnimationJob sync();
};

namespace BoardAnimations {

/// Return whether this animation type requires a caller-owned stop flag.
bool isCancellable(AnimationType type);

/// Return whether the scheduler should release its completion semaphore.
bool signalsCompletion(AnimationType type);

/// Execute one queued animation job. The caller owns queueing and LED mutexing.
void execute(BoardLEDBatch& leds, const AnimationJob& job);

}  // namespace BoardAnimations

#endif  // BOARD_ANIMATIONS_H
