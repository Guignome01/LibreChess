#ifndef BOARD_H
#define BOARD_H

#include "assistance.h"
#include "driver.h"
#include "feedback.h"
#include "state.h"

#include <atomic>

// Public facade for physical board hardware, drawing, and occupancy state.
class Board {
 public:
  Board();

  void begin();

  /// Poll sensors and update the physical occupancy transition state.
  void tick();

  /// Poll sensors and update the physical occupancy transition state.
  void readSensors();

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

  /// Reset the transition baseline to the current physical occupancy.
  void syncOccupancyBaseline();

  BoardAssistance& assistance() { return assistance_; }
  const BoardAssistance& assistance() const { return assistance_; }
  BoardFeedback& feedback() { return feedback_; }
  const BoardFeedback& feedback() const { return feedback_; }

  void acquireLEDs();
  void releaseLEDs();

  struct LedGuard {
    Board* board;
    explicit LedGuard(Board* board) : board(board) { board->acquireLEDs(); }
    ~LedGuard() { board->releaseLEDs(); }
    LedGuard(const LedGuard&) = delete;
    LedGuard& operator=(const LedGuard&) = delete;
  };

  void clearAllLEDs(bool show = true);
  void setSquareLED(int row, int col, LedRGB color);
  void showLEDs();

  void fireworkAnimation(LedRGB color = LedColors::White);
  void captureAnimation(int row, int col);
  void promotionAnimation(int col);
  void blinkSquare(int row, int col, LedRGB color, int times = 3, bool clearAfter = true, bool clearBefore = false);
  void showConnectingAnimation();
  void flashBoardAnimation(LedRGB color, int times = 3);

  std::atomic<bool>* startThinkingAnimation();
  std::atomic<bool>* startWaitingAnimation();
  void stopAndWaitForAnimation(std::atomic<bool>*& stopFlag);
  void waitForAnimationQueueDrain();

  uint8_t getBrightness() const;
  uint8_t getDimMultiplier() const;
  void setBrightness(uint8_t value);
  void setDimMultiplier(uint8_t value);
  void saveLedSettings();
  void triggerCalibration();

 private:
  BoardDriver driver_;
  BoardFeedback feedback_;
  BoardAssistance assistance_;
  LibreChess::board::BoardState state_;

  void syncStateFromDriver(bool initializeBaseline);
};

#endif  // BOARD_H
