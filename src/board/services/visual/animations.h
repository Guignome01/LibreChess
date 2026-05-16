#ifndef BOARD_SERVICES_VISUAL_ANIMATIONS_H
#define BOARD_SERVICES_VISUAL_ANIMATIONS_H

#include "board/runtime/colors.h"
#include "board/runtime/scheduler.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// BoardAnimations — board-specific visual animation API
// ---------------------------------------------------------------------------
// Board-owned visual animation vocabulary and frame painting. The generic scheduler owns
// timed slot lifecycle; this class turns board animation requests into scheduled
// painters that render logical frames into BoardCanvas.
// ---------------------------------------------------------------------------

namespace BoardAnimationTiming {
constexpr uint32_t BLINK_HALF_MS = 200;
constexpr uint32_t FLASH_HALF_MS = 200;
constexpr uint32_t FIREWORK_FRAME_MS = 100;
constexpr uint32_t FIREWORK_FRAMES = 24;
constexpr uint32_t CAPTURE_FRAME_MS = 50;
constexpr uint32_t CAPTURE_FRAMES = 20;
constexpr uint32_t PROMOTION_FRAME_MS = 100;
constexpr uint32_t PROMOTION_FRAMES = 16;
constexpr uint32_t CONNECTING_FRAME_MS = 100;
constexpr uint32_t CONNECTING_FRAMES = 8;
constexpr uint32_t THINKING_FRAME_MS = 30;
constexpr uint32_t WAITING_FRAME_MS = 200;
}  // namespace BoardAnimationTiming

enum class BoardAnimationKind : uint8_t {
  BLINK,
  FLASH,
  FIREWORK,
  CAPTURE,
  PROMOTION,
  THINKING,
  WAITING,
  CONNECTING,
};

struct BoardAnimationParams {
  struct BlinkParams { int row; int col; LedRGB color; int times; };
  struct FlashParams { LedRGB color; int times; };
  struct FireworkParams { LedRGB color; };
  struct CaptureParams { int row; int col; };
  struct PromotionParams { int col; };

  union {
    BlinkParams blink;
    FlashParams flash;
    FireworkParams firework;
    CaptureParams capture;
    PromotionParams promotion;
  };

  BoardAnimationParams() { blink = {0, 0, LedColors::Off, 0}; }
};

struct BoardAnimationSpec {
  BoardAnimationKind kind;
  uint32_t durationMs;
  bool loop;
  BoardAnimationParams params;
};

using BoardAnimationHandle = BoardScheduledHandle;

class BoardAnimations {
 public:
  static constexpr uint8_t SLOT_COUNT = BoardScheduler::SLOT_COUNT;

  BoardAnimations(BoardScheduler& scheduler, BoardCanvas& canvas);

  BoardAnimations(const BoardAnimations&) = delete;
  BoardAnimations& operator=(const BoardAnimations&) = delete;

  /// Schedule an animation painter. Finite animations auto-expire; looping
  /// animations keep running until cancelled.
  BoardAnimationHandle start(const BoardAnimationSpec& spec, uint32_t nowMs);

  /// Cancel an active animation and invalidate the handle.
  void cancel(BoardAnimationHandle& handle);

  /// Return true iff the handle still references a live animation.
  bool active(BoardAnimationHandle handle) const;

  /// Return true iff at least one animation is scheduled.
  bool any() const;

  /// Cancel every active animation and release its surface.
  void clearAll();

  BoardAnimationHandle startBlink(int row, int col, LedRGB color, int times, uint32_t nowMs);
  BoardAnimationHandle startFlash(LedRGB color, int times, uint32_t nowMs);
  BoardAnimationHandle startFirework(LedRGB color, uint32_t nowMs);
  BoardAnimationHandle startCapture(int row, int col, uint32_t nowMs);
  BoardAnimationHandle startPromotion(int col, uint32_t nowMs);
  BoardAnimationHandle startThinking(uint32_t nowMs);
  BoardAnimationHandle startWaiting(uint32_t nowMs);
  BoardAnimationHandle startConnecting(uint32_t nowMs);

 private:
  BoardScheduler& scheduler_;
  BoardCanvas& canvas_;
};

class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardAnimationToken — move-only RAII handle for a board animation.
// ---------------------------------------------------------------------------
// Acquires the canvas lock and cancels the underlying scheduler slot on
// destruction. Designed for use at module boundaries (game program ↔ game
// mode, board ↔ external firmware). Callers inside `src/board/` that already
// hold the canvas lock must continue to use the raw `BoardAnimationHandle`
// via `BoardAnimations::cancel(...)` to avoid recursive lock acquisition.
// ---------------------------------------------------------------------------

class BoardAnimationToken {
 public:
  BoardAnimationToken();
  BoardAnimationToken(BoardRuntime* runtime, BoardAnimations* animations,
                      BoardAnimationHandle handle);
  ~BoardAnimationToken();

  BoardAnimationToken(const BoardAnimationToken&) = delete;
  BoardAnimationToken& operator=(const BoardAnimationToken&) = delete;
  BoardAnimationToken(BoardAnimationToken&& other) noexcept;
  BoardAnimationToken& operator=(BoardAnimationToken&& other) noexcept;

  /// Stop the animation immediately. Safe to call more than once.
  void stop();

  /// True while the token still owns a live animation handle.
  bool active() const { return runtime_ != nullptr && handle_.valid(); }
  explicit operator bool() const { return active(); }

 private:
  BoardRuntime* runtime_;
  BoardAnimations* animations_;
  BoardAnimationHandle handle_;
};

#endif  // BOARD_SERVICES_VISUAL_ANIMATIONS_H
