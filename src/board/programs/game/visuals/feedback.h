#ifndef BOARD_PROGRAMS_GAME_VISUALS_FEEDBACK_H
#define BOARD_PROGRAMS_GAME_VISUALS_FEEDBACK_H

#include "board/runtime/colors.h"
#include "board/services/visual/animations.h"
#include "board/services/visual/visual.h"
#include "board/types.h"

#include <stdint.h>

class BoardAnimations;
class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardFeedback — mandatory visual feedback for move outcomes & status.
// ---------------------------------------------------------------------------
// Owns a canvas surface and starts retained animations through
// BoardAnimations. All canvas mutation goes through BoardRuntime::lockCanvas().
//
// Status animations (THINKING / WAITING) return a `BoardAnimationHandle`
// instead of a heap-allocated atomic flag. The caller cancels them via
// `stopAnimation(handle)`.
// ---------------------------------------------------------------------------

class BoardFeedback : private BoardVisual {
 public:
  BoardFeedback(BoardRuntime& runtime, BoardAnimations& animations);

  /// Clear this helper's surface entirely.
  void clearBoard();

  /// Clear a single square on this helper's surface.
  void clearSquare(int row, int col);

  /// Show capture / promotion / quiet / check / game-end visuals for the
  /// just-applied move.
  void showMoveResultFeedback(const BoardMoveFeedbackData& feedback);

  /// Two red blinks at (row, col) for an illegal-move attempt.
  void showIllegalMoveFeedback(int row, int col);

  /// Paint scaled-orange resign progress at (row, col). Levels 0..3.
  /// `clearFirst`=true clears the owned surface before painting.
  void showResignProgress(int row, int col, int level, bool clearFirst = false);

  /// Clear a single resign-progress pixel.
  void clearResignFeedback(int row, int col);

  /// Player-color firework for local game end.
  void showWinner(BoardPieceColor winnerColor);

  /// 'w'/'b'/'d' winner code firework for remote game end.
  void showRemoteGameEnd(char winnerColor);

  /// Three red flashes for unrecoverable error.
  void showError();

  /// Start the looping THINKING animation. Caller must cancel via stopAnimation.
  BoardAnimationHandle startThinking();

  /// Start the looping WAITING animation. Caller must cancel via stopAnimation.
  BoardAnimationHandle startWaiting();

  /// Cancel a status animation and invalidate the handle.
  void stopAnimation(BoardAnimationHandle& handle);

 private:
  BoardAnimations& animations_;
};

#endif  // BOARD_PROGRAMS_GAME_VISUALS_FEEDBACK_H
