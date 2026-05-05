#ifndef BOARD_H
#define BOARD_H

#include "game.h"
#include "logger.h"
#include "gui/selection.h"

#include <atomic>
#include <cstdint>
#include <memory>

// Public facade for physical board hardware, interaction workflows, and
// occupancy state. This is the only board header consumed outside src/board/.
class Board {
 public:
  using GameSelectionMode = BoardGameSelectionMode;
  using GameSelection = BoardGameSelection;

  Board();
  ~Board();

  Board(const Board&) = delete;
  Board& operator=(const Board&) = delete;
  Board(Board&&) = delete;
  Board& operator=(Board&&) = delete;

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

  /// Return one changed square through row/col out-params.
  /// Returns false when the index is out of range.
  bool changedSquare(uint8_t index, int& row, int& col) const;

  /// Reset the transition baseline to the current physical occupancy.
  void syncOccupancyBaseline();

  void waitForBoardSetup(const LibreChess::Game& game, LibreChess::Log& logger);
  void showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves, const LibreChess::Game& game);
  void showCapturePlacementPrompt(int row, int col);
  void guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol,
                     const LibreChess::CastlingInfo& castling, bool waitForKingCompletion,
                     LibreChess::Log& logger);
  void guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol,
                                 bool isCapture, bool isEnPassant,
                                 int enPassantCapturedPawnRow,
                                 LibreChess::Log& logger);

  void clearBoardFeedback(bool show = true);
  void clearFeedbackSquare(int row, int col);
  void showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol,
                              const LibreChess::Game& game);
  void showIllegalMoveFeedback(int row, int col);
  void showResignProgress(int row, int col, int level, bool clearFirst = false);
  void clearResignFeedback(int row, int col);
  void showWinner(LibreChess::Color winnerColor);
  void showRemoteGameEnd(char winnerColor);
  void showErrorFeedback();

  std::atomic<bool>* startThinkingStatus();
  std::atomic<bool>* startWaitingStatus();
  void stopStatusAnimation(std::atomic<bool>*& stopFlag);

  void clearAllLEDs(bool show = true);
  void showConnectingAnimation();

  void startGameSelectionMenu();
  void clearGameSelectionMenu();
  GameSelection pollGameSelectionMenu();

  bool confirmAction(bool flipped = false);
  bool confirmResume(GameSelectionMode mode, bool flipped = false);

  void beginDiagnostics();
  void updateDiagnostics();
  bool diagnosticsComplete() const;

  uint8_t getBrightness() const;
  uint8_t getDimMultiplier() const;
  void setBrightness(uint8_t value);
  void setDimMultiplier(uint8_t value);
  void saveLedSettings();
  void triggerCalibration();

  uint16_t sensorReadDelayMs() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#endif  // BOARD_H
