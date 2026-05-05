#ifndef BOARD_DRAWABLE_H
#define BOARD_DRAWABLE_H

/// Board-internal contract for modal visual surfaces that can be shown,
/// hidden, reset, and polled by BoardStack without owning them.
class BoardDrawable {
 public:
  static constexpr int RESULT_NONE = -1;
  static constexpr int RESULT_BACK = -2;

  virtual ~BoardDrawable() = default;

  BoardDrawable(const BoardDrawable&) = delete;
  BoardDrawable& operator=(const BoardDrawable&) = delete;

  /// Light or otherwise present this visual surface.
  virtual void show() = 0;

  /// Remove this visual surface from the LEDs.
  virtual void hide() = 0;

  /// Reset transient input state before showing or re-showing this surface.
  virtual void reset() = 0;

  /// Poll this surface after sensors have been refreshed.
  virtual int poll() = 0;

 protected:
  BoardDrawable() = default;
};

#endif  // BOARD_DRAWABLE_H
