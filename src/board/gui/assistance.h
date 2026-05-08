#ifndef BOARD_ASSISTANCE_H
#define BOARD_ASSISTANCE_H

#include "board/core/colors.h"
#include "game.h"
#include "logger.h"

#include <stdint.h>

class BoardRuntime;

/// Optional move hinting mode for board-owned chess assistance.
enum class BoardAssistanceLevel : uint8_t {
  NONE = 0,
  LEGAL_MOVES = 1,
  BEST_MOVE = 2,
};

// ---------------------------------------------------------------------------
// BoardAssistance — optional physical guidance visuals.
// ---------------------------------------------------------------------------
// Paints onto BoardLayer::ASSISTANCE. Loops that wait for the user (e.g.
// `waitForSetup`, `guideCastling`) query occupancy through BoardRuntime's
// synchronized input helpers and `delay(runtime.cadenceMs())` between checks.
// The renderer task keeps
// the painted highlights visible without any explicit `service()` call.
// ---------------------------------------------------------------------------

class BoardAssistance {
 public:
  explicit BoardAssistance(BoardRuntime& runtime,
                           BoardAssistanceLevel level = BoardAssistanceLevel::LEGAL_MOVES);

  void setLevel(BoardAssistanceLevel level) { level_ = level; }
  BoardAssistanceLevel level() const { return level_; }

  /// Block until the physical board matches the in-memory game position,
  /// painting mismatch hints onto ASSISTANCE every cadence tick.
  void waitForSetup(const LibreChess::Game& game, LibreChess::Log& logger);

  /// Highlight legal destination squares for the piece on (fromRow, fromCol).
  /// Cyan = source, white = quiet move, red = capture, purple = en-passant
  /// captured pawn. Cleared when the workflow paints over ASSISTANCE again.
  void showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves,
                               const LibreChess::Game& game);

  /// Single red blink at (row, col) prompting the user to place a captured
  /// piece off-board.
  void showCapturePlacementPrompt(int row, int col);

  /// Lead the user through a castling move: king first (if requested),
  /// then rook. Blocks until completion.
  void guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol,
                     const LibreChess::CastlingInfo& castling, bool waitForKingCompletion,
                     LibreChess::Log& logger);

  /// Lead the user through completing a remote-engine move on the board
  /// (lift source, place destination, remove captured piece if any).
  /// Blocks until completion.
  void guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol, bool isCapture,
                                 bool isEnPassant, int enPassantCapturedPawnRow,
                                 LibreChess::Log& logger);

 private:
  BoardRuntime& runtime_;
  BoardAssistanceLevel level_;

  // Helpers that paint a movement prompt onto ASSISTANCE under the canvas
  // guard. Pulled out of the .cpp to keep flow readable.
  void paintMovePrompt(int fromRow, int fromCol, int toRow, int toCol, LedRGB destColor,
                       int extraRow = -1, int extraCol = -1, LedRGB extraColor = LedColors::Off);
  void paintDestinationOnly(int row, int col, LedRGB color);
};

#endif  // BOARD_ASSISTANCE_H
