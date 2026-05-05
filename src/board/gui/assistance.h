#ifndef BOARD_ASSISTANCE_H
#define BOARD_ASSISTANCE_H

#include "board/core/colors.h"
#include "board/core/system.h"
#include "game.h"
#include "logger.h"

#include <stdint.h>

class BoardLayering;

/// Optional move hinting mode for board-owned chess assistance.
enum class BoardAssistanceLevel : uint8_t {
  NONE = 0,
  LEGAL_MOVES = 1,
  BEST_MOVE = 2,
};

// Board-owned physical guidance for chess play.
class BoardAssistance {
 public:
  explicit BoardAssistance(BoardSystem* system = nullptr, BoardAssistanceLevel level = BoardAssistanceLevel::LEGAL_MOVES,
                           BoardLayering* layering = nullptr);

  void setLevel(BoardAssistanceLevel level) { level_ = level; }
  BoardAssistanceLevel level() const { return level_; }

  void waitForSetup(const LibreChess::Game& game, LibreChess::Log& logger);
  void showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves, const LibreChess::Game& game);
  void showCapturePlacementPrompt(int row, int col);
  void guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol, const LibreChess::CastlingInfo& castling, bool waitForKingCompletion, LibreChess::Log& logger);
  void guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol, bool isCapture, bool isEnPassant, int enPassantCapturedPawnRow, LibreChess::Log& logger);

 private:
  BoardSystem* system_;
  BoardAssistanceLevel level_;
  BoardLayering* layering_;
};

#endif  // BOARD_ASSISTANCE_H
