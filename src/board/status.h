#ifndef BOARD_STATUS_H
#define BOARD_STATUS_H

class Board;
class BoardServices;

/// External board workflow exposing low-level visual primitives needed by the
/// application layer (clear LEDs, network connecting animation). Owned around
/// a long-lived Board reference and may be shared with WiFi for status visuals.
class BoardStatus {
 public:
  explicit BoardStatus(Board& board);

  /// Clear all LEDs through the layered render path.
  void clearAllLEDs(bool show = true);

  /// Run the synchronous WiFi-connecting animation.
  void showConnectingAnimation();

 private:
  BoardServices& services_;
};

#endif  // BOARD_STATUS_H
