#ifndef BOARD_CORE_SNAPSHOT_H
#define BOARD_CORE_SNAPSHOT_H

#include "board/board.h"

#include <stdint.h>

/// Generic current/previous occupancy snapshot for a fixed row/column grid.
/// The snapshot stores boolean occupancy only; callers provide the semantic
/// meaning of occupied squares and transitions.
template <uint8_t Rows, uint8_t Cols>
class BoardSnapshot {
 public:
  static_assert(Rows > 0, "BoardSnapshot requires at least one row");
  static_assert(Cols > 0, "BoardSnapshot requires at least one column");
  static_assert(Rows <= 127 && Cols <= 127,
                "BoardSnapshot changed squares use int8_t coordinates");
  static_assert(static_cast<unsigned int>(Rows) * static_cast<unsigned int>(Cols) <= 255,
                "BoardSnapshot stores changed count in uint8_t");

  /// Initialize the snapshot with empty current/previous state.
  BoardSnapshot() : current_{}, previous_{}, changedSquares_{}, changedCount_(0) {}

  /// Read all squares from a callable `bool(int row, int col)` and treat the
  /// new state as both current and previous (no transitions).
  template <typename SensorRead>
  void sync(SensorRead&& sensorRead) {
    changedCount_ = 0;
    for (uint8_t row = 0; row < Rows; ++row)
      for (uint8_t col = 0; col < Cols; ++col) {
        current_[row][col] = sensorRead(row, col);
        previous_[row][col] = current_[row][col];
      }
  }

  /// Read all squares from a callable `bool(int row, int col)`, shift the
  /// current snapshot into previous, and rebuild the changed-square list.
  template <typename SensorRead>
  void update(SensorRead&& sensorRead) {
    changedCount_ = 0;
    for (uint8_t row = 0; row < Rows; ++row)
      for (uint8_t col = 0; col < Cols; ++col)
        previous_[row][col] = current_[row][col];

    for (uint8_t row = 0; row < Rows; ++row)
      for (uint8_t col = 0; col < Cols; ++col) {
        current_[row][col] = sensorRead(row, col);
        if (current_[row][col] != previous_[row][col])
          changedSquares_[changedCount_++] = LibreChess::board::BoardSquare{
              static_cast<int8_t>(row), static_cast<int8_t>(col)};
      }
  }

  /// Return whether the latest snapshot has an occupied square at row/col.
  bool occupied(int row, int col) const {
    return validSquare(row, col) ? current_[row][col] : false;
  }

  /// Return whether the previous snapshot had an occupied square at row/col.
  bool wasOccupied(int row, int col) const {
    return validSquare(row, col) ? previous_[row][col] : false;
  }

  /// Return whether row/col transitioned from occupied to empty.
  bool wasLifted(int row, int col) const { return wasOccupied(row, col) && !occupied(row, col); }

  /// Return whether row/col transitioned from empty to occupied.
  bool wasPlaced(int row, int col) const { return !wasOccupied(row, col) && occupied(row, col); }

  /// Return the number of changed squares captured by the last update.
  uint8_t changedCount() const { return changedCount_; }

  /// Return one changed square by index, or an invalid square when out of range.
  LibreChess::board::BoardSquare changedSquare(uint8_t index) const {
    if (index >= changedCount_) return LibreChess::board::BoardSquare{-1, -1};
    return changedSquares_[index];
  }

 private:
  static constexpr unsigned int SQUARE_COUNT =
      static_cast<unsigned int>(Rows) * static_cast<unsigned int>(Cols);

  static constexpr bool validSquare(int row, int col) {
    return row >= 0 && row < Rows && col >= 0 && col < Cols;
  }

  bool current_[Rows][Cols];
  bool previous_[Rows][Cols];
  LibreChess::board::BoardSquare changedSquares_[SQUARE_COUNT];
  uint8_t changedCount_;
};

#endif  // BOARD_CORE_SNAPSHOT_H
