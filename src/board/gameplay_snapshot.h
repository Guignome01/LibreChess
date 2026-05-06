#ifndef BOARD_GAMEPLAY_SNAPSHOT_H
#define BOARD_GAMEPLAY_SNAPSHOT_H

#include "board.h"

#include <stdint.h>

/// Current/previous occupancy snapshot used by gameplay to detect lifted/placed
/// transitions between sensor reads.
class OccupancySnapshot {
 public:
  OccupancySnapshot() : current_{}, previous_{}, changedSquares_{}, changedCount_(0) {}

  /// Read all squares from a callable `bool(int row, int col)` and treat the
  /// new state as both current and previous (no transitions).
  template <typename SensorRead>
  void sync(SensorRead&& sensorRead) {
    changedCount_ = 0;
    for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row)
      for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
        current_[row][col] = sensorRead(row, col);
        previous_[row][col] = current_[row][col];
      }
  }

  /// Read all squares from a callable `bool(int row, int col)`, shift the
  /// current snapshot into previous, and rebuild the changed-square list.
  template <typename SensorRead>
  void update(SensorRead&& sensorRead) {
    changedCount_ = 0;
    for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row)
      for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col)
        previous_[row][col] = current_[row][col];

    for (int row = 0; row < LibreChess::board::BOARD_ROWS; ++row)
      for (int col = 0; col < LibreChess::board::BOARD_COLS; ++col) {
        current_[row][col] = sensorRead(row, col);
        if (current_[row][col] != previous_[row][col])
          changedSquares_[changedCount_++] = LibreChess::board::BoardSquare{
              static_cast<int8_t>(row), static_cast<int8_t>(col)};
      }
  }

  bool occupied(int row, int col) const {
    return LibreChess::board::isValidSquare(row, col) ? current_[row][col] : false;
  }

  bool wasOccupied(int row, int col) const {
    return LibreChess::board::isValidSquare(row, col) ? previous_[row][col] : false;
  }

  bool wasLifted(int row, int col) const { return wasOccupied(row, col) && !occupied(row, col); }
  bool wasPlaced(int row, int col) const { return !wasOccupied(row, col) && occupied(row, col); }

  uint8_t changedCount() const { return changedCount_; }
  LibreChess::board::BoardSquare changedSquare(uint8_t index) const {
    if (index >= changedCount_) return LibreChess::board::BoardSquare{-1, -1};
    return changedSquares_[index];
  }

 private:
  bool current_[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS];
  bool previous_[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS];
  LibreChess::board::BoardSquare changedSquares_[LibreChess::board::BOARD_SQUARES];
  uint8_t changedCount_;
};

#endif  // BOARD_GAMEPLAY_SNAPSHOT_H
