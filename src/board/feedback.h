#ifndef BOARD_FEEDBACK_H
#define BOARD_FEEDBACK_H

#include "colors.h"
#include "game.h"

#include <atomic>

class Board;

// Board-owned visual feedback for move outcomes, status, and errors.
class BoardFeedback {
 public:
  explicit BoardFeedback(Board* board = nullptr);

  void clearBoard(bool show = true);
  void clearSquare(int row, int col);
  void showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol, const LibreChess::Game& game);
  void showIllegalMoveFeedback(int row, int col);
  void showResignProgress(int row, int col, int level, bool clearFirst = false);
  void clearResignFeedback(int row, int col);
  void showWinner(LibreChess::Color winnerColor);
  void showRemoteGameEnd(char winnerColor);
  void showError();

  std::atomic<bool>* startThinking();
  std::atomic<bool>* startWaiting();
  void stopAnimation(std::atomic<bool>*& stopFlag);

 private:
  Board* board_;

  void confirmSquareCompletion(int row, int col);
};

#endif  // BOARD_FEEDBACK_H