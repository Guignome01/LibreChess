#include "state.h"

namespace LibreChess {
namespace board {

bool BoardSquare::valid() const {
  return BoardState::isValidSquare(row, col);
}

BoardState::BoardState() : current_{}, previous_{}, changedSquares_{}, changedCount_(0) {}

bool BoardState::isValidSquare(int row, int col) {
  return row >= 0 && row < BOARD_ROWS && col >= 0 && col < BOARD_COLS;
}

void BoardState::clear(bool occupiedValue) {
  changedCount_ = 0;
  for (int row = 0; row < BOARD_ROWS; ++row) {
    for (int col = 0; col < BOARD_COLS; ++col) {
      current_[row][col] = occupiedValue;
      previous_[row][col] = occupiedValue;
    }
  }
}

void BoardState::sync(const bool (&occupancy)[BOARD_ROWS][BOARD_COLS]) {
  changedCount_ = 0;
  copySnapshot(current_, occupancy);
  copySnapshot(previous_, occupancy);
}

void BoardState::update(const bool (&occupancy)[BOARD_ROWS][BOARD_COLS]) {
  changedCount_ = 0;
  copySnapshot(previous_, current_);

  for (int row = 0; row < BOARD_ROWS; ++row) {
    for (int col = 0; col < BOARD_COLS; ++col) {
      current_[row][col] = occupancy[row][col];
      if (current_[row][col] != previous_[row][col]) {
        changedSquares_[changedCount_++] = BoardSquare{static_cast<int8_t>(row), static_cast<int8_t>(col)};
      }
    }
  }
}

bool BoardState::occupied(int row, int col) const {
  return isValidSquare(row, col) ? current_[row][col] : false;
}

bool BoardState::wasOccupied(int row, int col) const {
  return isValidSquare(row, col) ? previous_[row][col] : false;
}

bool BoardState::wasLifted(int row, int col) const {
  return wasOccupied(row, col) && !occupied(row, col);
}

bool BoardState::wasPlaced(int row, int col) const {
  return !wasOccupied(row, col) && occupied(row, col);
}

bool BoardState::changed(int row, int col) const {
  return wasOccupied(row, col) != occupied(row, col);
}

BoardSquare BoardState::changedSquare(uint8_t index) const {
  if (index >= changedCount_) return BoardSquare{-1, -1};
  return changedSquares_[index];
}

void BoardState::copySnapshot(bool (&target)[BOARD_ROWS][BOARD_COLS], const bool (&source)[BOARD_ROWS][BOARD_COLS]) {
  for (int row = 0; row < BOARD_ROWS; ++row) {
    for (int col = 0; col < BOARD_COLS; ++col) {
      target[row][col] = source[row][col];
    }
  }
}

}  // namespace board
}  // namespace LibreChess