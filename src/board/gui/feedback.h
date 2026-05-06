#ifndef BOARD_FEEDBACK_H
#define BOARD_FEEDBACK_H

#include "board/core/colors.h"
#include "board/core/system.h"
#include "game.h"

#include <atomic>

class BoardLayering;

/// Board-internal visual feedback for move outcomes, status, and errors.
/// All persistent rendering goes through BoardLayering; one-shot animations
/// go through BoardSystem's animation submission API.
class BoardFeedback {
 public:
  BoardFeedback(BoardSystem& system, BoardLayering& layering);

  void clearBoard(bool show = true);
  void clearSquare(int row, int col);
  void showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol,
                              const LibreChess::Game& game);
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
  BoardSystem& system_;
  BoardLayering& layering_;

  void confirmSquareCompletion(int row, int col);
};

#endif  // BOARD_FEEDBACK_H
