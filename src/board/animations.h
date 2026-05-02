#ifndef BOARD_ANIMATIONS_H
#define BOARD_ANIMATIONS_H

#include "colors.h"

#include <atomic>
#include <stdint.h>

class BoardDriver;

// Animation job types for the BoardDriver async queue.
// SYNC is a no-op queue barrier used by waitForAnimationQueueDrain().
enum class AnimationType : uint8_t {
  CAPTURE,
  PROMOTION,
  BLINK,
  WAITING,
  THINKING,
  FIREWORK,
  FLASH,
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
};

namespace BoardAnimations {

/// Execute one queued animation job. The caller owns queueing and LED mutexing.
void execute(BoardDriver& driver, const AnimationJob& job);

/// Run the one-shot WiFi connecting animation under the caller's LED guard.
void runConnecting(BoardDriver& driver);

}  // namespace BoardAnimations

#endif  // BOARD_ANIMATIONS_H
