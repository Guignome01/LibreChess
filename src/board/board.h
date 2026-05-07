#ifndef BOARD_H
#define BOARD_H

#include "gui/selection.h"

#include <cstdint>
#include <memory>

class BoardCalibration;
class BoardDiagnostics;
class BoardGameplay;
class BoardMenu;

namespace LibreChess {
namespace board {

static constexpr int BOARD_ROWS = 8;
static constexpr int BOARD_COLS = 8;
static constexpr int BOARD_SQUARES = BOARD_ROWS * BOARD_COLS;

/// Return whether a row/column pair is inside the physical board.
inline constexpr bool isValidSquare(int row, int col) {
  return row >= 0 && row < BOARD_ROWS && col >= 0 && col < BOARD_COLS;
}

/// Display-coordinate square on the physical board.
struct BoardSquare {
  int8_t row;
  int8_t col;

  /// Return whether this square is within the 8x8 board.
  bool valid() const { return isValidSquare(row, col); }
};

/// Compare two physical board squares.
inline bool operator==(BoardSquare lhs, BoardSquare rhs) {
  return lhs.row == rhs.row && lhs.col == rhs.col;
}

/// Compare two physical board squares.
inline bool operator!=(BoardSquare lhs, BoardSquare rhs) {
  return !(lhs == rhs);
}

}  // namespace board
}  // namespace LibreChess

/// Public physical-board package root. It owns one internal BoardController
/// runtime plus the long-lived board workflows that external firmware consumes.
class Board {
 public:
  Board();
  ~Board();

  Board(const Board&) = delete;
  Board& operator=(const Board&) = delete;
  Board(Board&&) = delete;
  Board& operator=(Board&&) = delete;

  /// Initialize hardware. Must be called once before any other method.
  void begin();

  // --- LED settings (publicly exposed so WiFi/web UI can persist them) ---
  uint8_t getBrightness() const;
  uint8_t getDimMultiplier() const;
  void setBrightness(uint8_t value);
  void setDimMultiplier(uint8_t value);
  void saveLedSettings();

  /// Polling delay used by debounced sensor scans, exposed to coordinate
  /// firmware-side timing loops with the board's sensor cadence.
  uint16_t sensorReadDelayMs() const;

  /// Gameplay workflow for physical chess interactions.
  BoardGameplay& gameplay();

  /// Game-selection and confirmation menu workflow.
  BoardMenu& menu();

  /// Sensor diagnostics workflow.
  BoardDiagnostics& diagnostics();

  /// Calibration trigger workflow.
  BoardCalibration& calibration();

  /// Clear all board LEDs through the layered render path.
  void clearAllLEDs(bool show = true);

  /// Run the synchronous WiFi-connecting animation.
  void showConnectingAnimation();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#endif  // BOARD_H
