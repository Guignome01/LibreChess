#ifndef BOARD_FEEDBACK_H
#define BOARD_FEEDBACK_H

#include "board/core/colors.h"
#include "board/core/effects.h"
#include "game.h"

class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardFeedback — mandatory visual feedback for move outcomes & status.
// ---------------------------------------------------------------------------
// Paints onto BoardLayer::FEEDBACK and starts retained effects through
// BoardEffects. All canvas mutation goes through BoardRuntime::lockCanvas().
//
// Status animations (THINKING / WAITING) return a `BoardEffectHandle`
// instead of a heap-allocated atomic flag. The caller cancels them via
// `stopAnimation(handle)`.
// ---------------------------------------------------------------------------

class BoardFeedback {
 public:
  explicit BoardFeedback(BoardRuntime& runtime);

  /// Clear the FEEDBACK layer entirely.
  void clearBoard();

  /// Clear a single square on the FEEDBACK layer.
  void clearSquare(int row, int col);

  /// Show capture / promotion / quiet / check / game-end visuals for the
  /// just-applied move.
  void showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol,
                              const LibreChess::Game& game);

  /// Two red blinks at (row, col) for an illegal-move attempt.
  void showIllegalMoveFeedback(int row, int col);

  /// Paint scaled-orange resign progress at (row, col). Levels 0..3.
  /// `clearFirst`=true clears the FEEDBACK layer before painting.
  void showResignProgress(int row, int col, int level, bool clearFirst = false);

  /// Clear a single resign-progress pixel.
  void clearResignFeedback(int row, int col);

  /// Player-color firework for local game end.
  void showWinner(LibreChess::Color winnerColor);

  /// 'w'/'b'/'d' winner code firework for remote game end.
  void showRemoteGameEnd(char winnerColor);

  /// Three red flashes for unrecoverable error.
  void showError();

  /// Start the looping THINKING effect. Caller must cancel via stopAnimation.
  BoardEffectHandle startThinking();

  /// Start the looping WAITING effect. Caller must cancel via stopAnimation.
  BoardEffectHandle startWaiting();

  /// Cancel a status animation and invalidate the handle.
  void stopAnimation(BoardEffectHandle& handle);

 private:
  BoardRuntime& runtime_;
};

#endif  // BOARD_FEEDBACK_H
