#ifndef BOARD_STATE_H
#define BOARD_STATE_H

#include <stdint.h>

namespace LibreChess {
namespace board {

static constexpr int BOARD_ROWS = 8;
static constexpr int BOARD_COLS = 8;
static constexpr int BOARD_SQUARES = BOARD_ROWS * BOARD_COLS;

/// Display-coordinate square on the physical board.
struct BoardSquare {
  int8_t row;
  int8_t col;

  /// Return whether this square is within the 8x8 board.
  bool valid() const;
};

/// Compare two physical board squares.
inline bool operator==(BoardSquare lhs, BoardSquare rhs) {
  return lhs.row == rhs.row && lhs.col == rhs.col;
}

/// Compare two physical board squares.
inline bool operator!=(BoardSquare lhs, BoardSquare rhs) {
  return !(lhs == rhs);
}

/// Physical occupancy snapshots and derived square transitions.
class BoardState {
 public:
  BoardState();

  /// Return whether a row/column pair is inside the board.
  static bool isValidSquare(int row, int col);

  /// Reset current and previous snapshots to one occupancy value.
  void clear(bool occupied = false);

  /// Load a snapshot as the stable baseline without reporting changes.
  void sync(const bool (&occupancy)[BOARD_ROWS][BOARD_COLS]);

  /// Advance to a new occupancy snapshot and compute changed squares.
  void update(const bool (&occupancy)[BOARD_ROWS][BOARD_COLS]);

  /// Return current physical occupancy for a square.
  bool occupied(int row, int col) const;

  /// Return previous physical occupancy for a square.
  bool wasOccupied(int row, int col) const;

  /// Return whether a piece was lifted from a square during the latest update.
  bool wasLifted(int row, int col) const;

  /// Return whether a piece was placed on a square during the latest update.
  bool wasPlaced(int row, int col) const;

  /// Return whether a square changed during the latest update.
  bool changed(int row, int col) const;

  /// Return how many squares changed during the latest update.
  uint8_t changedCount() const { return changedCount_; }

  /// Return the changed square at index, or an invalid square if out of range.
  BoardSquare changedSquare(uint8_t index) const;

 private:
  bool current_[BOARD_ROWS][BOARD_COLS];
  bool previous_[BOARD_ROWS][BOARD_COLS];
  BoardSquare changedSquares_[BOARD_SQUARES];
  uint8_t changedCount_;

  void copySnapshot(bool (&target)[BOARD_ROWS][BOARD_COLS], const bool (&source)[BOARD_ROWS][BOARD_COLS]);
};

}  // namespace board
}  // namespace LibreChess

#endif  // BOARD_STATE_H