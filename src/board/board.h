#ifndef BOARD_H
#define BOARD_H

#include "board/gui/animations.h"
#include "board/menus/selection.h"

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

inline constexpr bool isValidSquare(int row, int col) {
  return row >= 0 && row < BOARD_ROWS && col >= 0 && col < BOARD_COLS;
}

struct BoardSquare {
  int8_t row;
  int8_t col;
  bool valid() const { return isValidSquare(row, col); }
};

inline bool operator==(BoardSquare lhs, BoardSquare rhs) {
  return lhs.row == rhs.row && lhs.col == rhs.col;
}
inline bool operator!=(BoardSquare lhs, BoardSquare rhs) { return !(lhs == rhs); }

}  // namespace board
}  // namespace LibreChess

// ---------------------------------------------------------------------------
// Board — public physical-board package root
// ---------------------------------------------------------------------------
// Owns one internal BoardRuntime plus the long-lived workflows (gameplay,
// menu, diagnostics, calibration). External firmware accesses the board
// only through this class.
// ---------------------------------------------------------------------------

class Board {
 public:
  Board();
  ~Board();

  Board(const Board&) = delete;
  Board& operator=(const Board&) = delete;
  Board(Board&&) = delete;
  Board& operator=(Board&&) = delete;

  /// Initialize hardware. Must be called once before any other method.
  /// Returns false when a required runtime resource could not start.
  bool begin();

  // --- LED settings ---
  uint8_t getBrightness() const;
  uint8_t getDimMultiplier() const;
  void setBrightness(uint8_t value);
  void setDimMultiplier(uint8_t value);
  void saveLedSettings();

  /// Sensor poll cadence (ms). Main loop should `delay(cadenceMs())`.
  uint16_t cadenceMs() const;

  // --- Workflows ---
  BoardGameplay& gameplay();
  BoardMenu& menu();
  BoardDiagnostics& diagnostics();
  BoardCalibration& calibration();

  /// Clear every canvas surface. The renderer flushes when it next wakes.
  void clearAllSurfaces();

  /// Start the looping WiFi-connecting animation. Returns a handle the caller
  /// passes back to `stopConnectingStatus`.
  BoardAnimationHandle startConnectingStatus();

  /// Cancel a connecting animation previously started.
  void stopConnectingStatus(BoardAnimationHandle& handle);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#endif  // BOARD_H
