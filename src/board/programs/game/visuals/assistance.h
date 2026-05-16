#ifndef BOARD_PROGRAMS_GAME_VISUALS_ASSISTANCE_H
#define BOARD_PROGRAMS_GAME_VISUALS_ASSISTANCE_H

#include "board/runtime/colors.h"
#include "board/services/visual/visual.h"
#include "board/types.h"

#include <stdint.h>

class BoardRuntime;
class BoardAnimations;

// ---------------------------------------------------------------------------
// BoardAssistance — optional physical guidance visuals.
// ---------------------------------------------------------------------------
// Owns a canvas surface. Loops that wait for the user (e.g.
// `waitForSetup`, `guideCastling`) query occupancy through BoardRuntime's
// synchronized input helpers and `delay(runtime.cadenceMs())` between checks.
// The renderer task keeps
// the painted highlights visible without any explicit `service()` call.
// ---------------------------------------------------------------------------

class BoardAssistance : private BoardVisual {
 public:
  BoardAssistance(BoardRuntime& runtime, BoardAnimations& animations,
                           BoardAssistanceLevel level = BoardAssistanceLevel::LEGAL_MOVES);

  void setLevel(BoardAssistanceLevel level) { level_ = level; }
  BoardAssistanceLevel level() const { return level_; }

  /// Clear this helper's guidance surface.
  void clear();

  /// Block until the physical board matches the in-memory game position,
  /// painting mismatch hints every sensor cadence interval.
  void waitForSetup(const BoardSetupSnapshot& setup);

  /// Highlight legal destination squares for the piece on (fromRow, fromCol).
  /// Cyan = source, white = quiet move, red = capture, purple = en-passant
  /// captured pawn. Cleared when the game program repaints its surface.
  void showLegalMoveHighlights(int fromRow, int fromCol, const BoardMoveTargetList& targets);

  /// Highlight one best-move suggestion returned by the configured assistance
  /// callback. Does not request or poll an engine itself.
  void showBestMoveHint(const BoardBestMoveHint& hint);

  /// Single red blink at (row, col) prompting the user to place a captured
  /// piece off-board.
  void showCapturePlacementPrompt(int row, int col);

  /// Lead the user through a castling move: king first (if requested),
  /// then rook. Blocks until completion.
  void guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol,
                     const BoardCastlingGuide& castling, bool waitForKingCompletion);

  /// Lead the user through completing a remote-engine move on the board
  /// (lift source, place destination, remove captured piece if any).
  /// Blocks until completion.
  void guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol,
                                 const BoardMoveCompletion& completion);

 private:
  BoardAnimations& animations_;
  BoardAssistanceLevel level_;

  // Helpers that paint a movement prompt under the canvas
  // guard. Pulled out of the .cpp to keep flow readable.
  void paintMovePrompt(int fromRow, int fromCol, int toRow, int toCol, LedRGB destColor,
                       int extraRow = -1, int extraCol = -1, LedRGB extraColor = LedColors::Off);
  void paintDestinationOnly(int row, int col, LedRGB color);
};

#endif  // BOARD_PROGRAMS_GAME_VISUALS_ASSISTANCE_H
