#include "board/services/visual/animations.h"

#include "board/runtime/runtime.h"

// ---------------------------------------------------------------------------
// BoardAnimationToken — RAII handle for cross-module animation ownership.
// ---------------------------------------------------------------------------
// Implementation lives in its own translation unit because BoardRuntime pulls
// in Arduino-only headers (BoardDriver → NeoPixelBusLg). Native unit tests
// that compile `animations.cpp` directly therefore do not see these symbols
// — they can still use the raw `BoardAnimationHandle` API on BoardAnimations.
// ---------------------------------------------------------------------------

BoardAnimationToken::BoardAnimationToken()
    : runtime_(nullptr), animations_(nullptr), handle_() {}

BoardAnimationToken::BoardAnimationToken(BoardRuntime* runtime, BoardAnimations* animations,
                                         BoardAnimationHandle handle)
    : runtime_(handle.valid() && runtime != nullptr && animations != nullptr ? runtime : nullptr),
      animations_(handle.valid() && runtime != nullptr && animations != nullptr ? animations
                                                                                : nullptr),
      handle_(handle) {}

BoardAnimationToken::~BoardAnimationToken() { stop(); }

BoardAnimationToken::BoardAnimationToken(BoardAnimationToken&& other) noexcept
    : runtime_(other.runtime_), animations_(other.animations_), handle_(other.handle_) {
  other.runtime_ = nullptr;
  other.animations_ = nullptr;
  other.handle_ = BoardAnimationHandle{};
}

BoardAnimationToken& BoardAnimationToken::operator=(BoardAnimationToken&& other) noexcept {
  if (this == &other) return *this;
  stop();
  runtime_ = other.runtime_;
  animations_ = other.animations_;
  handle_ = other.handle_;
  other.runtime_ = nullptr;
  other.animations_ = nullptr;
  other.handle_ = BoardAnimationHandle{};
  return *this;
}

void BoardAnimationToken::stop() {
  if (runtime_ == nullptr || animations_ == nullptr || !handle_.valid()) return;
  auto g = runtime_->lockCanvas();
  animations_->cancel(handle_);
  runtime_ = nullptr;
  animations_ = nullptr;
  handle_ = BoardAnimationHandle{};
}
