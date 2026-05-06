#ifndef BOARD_GAMEPLAY_H
#define BOARD_GAMEPLAY_H

#include "board.h"
#include "gameplay_snapshot.h"
#include "game.h"
#include "logger.h"

#include <atomic>
#include <stdint.h>

class BoardServices;

/// Result of polling the physical board for a player interaction.
enum class BoardGameplayResult : uint8_t {
  NONE,
  MOVE,
  RESIGN_REQUESTED,
};

/// Display-coordinate move or resign request detected on the physical board.
struct BoardGameplayMove {
  int fromRow = -1;
  int fromCol = -1;
  int toRow = -1;
  int toCol = -1;
  LibreChess::Color resignColor = LibreChess::Color::WHITE;
};

/// Board-owned physical chess interaction mode shared by firmware game modes.
/// All low-level hardware/visual access flows through BoardServices, which is
/// the only board internal a workflow ever sees. Occupancy transitions are
/// tracked in an OccupancySnapshot helper so this class stays focused on
/// interpreting transitions as chess intent.
class BoardGameplay {
 public:
  explicit BoardGameplay(Board& board);

  /// Poll sensors and refresh gameplay-owned physical transition state.
  void readSensors();

  /// Reset gameplay transition state to the current physical occupancy.
  void syncOccupancyBaseline();

  /// Block until the physical board matches the current chess position.
  void waitForSetup(const LibreChess::Game& game, LibreChess::Log& logger);

  /// Poll changed physical squares and detect a legal player move or resign gesture.
  BoardGameplayResult tryPlayerMove(const LibreChess::Game& game, LibreChess::Color playerColor,
                                    LibreChess::Log& logger, BoardGameplayMove& selection);

  /// Guide physical completion and show outcome visuals for an already-applied move.
  void completeAppliedMove(const LibreChess::Game& game, const LibreChess::MoveResult& result,
                           const LibreChess::CastlingInfo& castling, int fromRow, int fromCol,
                           int toRow, int toCol, bool isRemoteMove, LibreChess::Log& logger);

  /// Run board-side resign confirmation without mutating Game.
  bool confirmResign(LibreChess::Color resignColor, bool flipped, LibreChess::Log& logger);

  /// Show the winner feedback for a confirmed resignation.
  void showResignWinner(LibreChess::Color resignColor);

  /// Start the gameplay thinking status animation.
  std::atomic<bool>* startThinkingStatus();

  /// Start the gameplay waiting status animation.
  std::atomic<bool>* startWaitingStatus();

  /// Stop a status animation started by this gameplay mode.
  void stopStatusAnimation(std::atomic<bool>*& stopFlag);

  /// Show a remote game-end result on the board.
  void showRemoteGameEnd(char winnerColor);

  /// Show a gameplay error on the board.
  void showErrorFeedback();

 private:
  static constexpr unsigned long RESIGN_HOLD_MS = 3000;
  static constexpr unsigned long RESIGN_LIFT_WINDOW_MS = 1000;

  BoardServices& services_;
  OccupancySnapshot snapshot_;

  bool continueResignGesture(int row, int col, LibreChess::Color color, LibreChess::Log& logger);
};

#endif  // BOARD_GAMEPLAY_H
