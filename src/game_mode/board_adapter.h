#ifndef GAME_MODE_BOARD_ADAPTER_H
#define GAME_MODE_BOARD_ADAPTER_H

#include "board/programs/game/game_rules.h"
#include "board/types.h"
#include "game.h"

// ---------------------------------------------------------------------------
// BoardAdapter — maps LibreChess Game data into board-owned contracts.
// ---------------------------------------------------------------------------
// Game modes own this boundary so src/board can stay independent from the
// LibreChess engine/game types while still receiving everything it needs for
// physical validation and LED feedback.
// ---------------------------------------------------------------------------

namespace BoardAdapter {

BoardPieceColor toBoardColor(LibreChess::Color color);
LibreChess::Color toGameColor(BoardPieceColor color);
BoardPiece toBoardPiece(LibreChess::Piece piece);

class GameRules final : public BoardGameRules {
 public:
  explicit GameRules(const LibreChess::Game& game);

  BoardPiece pieceAt(int row, int col) const override;
  void setupSnapshot(BoardSetupSnapshot& snapshot) const override;
  void legalTargets(int fromRow, int fromCol, BoardMoveTargetList& targets) const override;

 private:
  const LibreChess::Game& game_;
};

BoardCastlingGuide castlingGuide(const LibreChess::CastlingInfo& castling);
BoardMoveCompletion moveCompletion(const LibreChess::MoveResult& result,
                                   const BoardCastlingGuide& castling,
                                   bool isRemoteMove);
BoardMoveFeedbackData moveFeedback(const LibreChess::Game& game,
                                   const LibreChess::MoveResult& result,
                                   int toRow,
                                   int toCol);

}  // namespace BoardAdapter

#endif  // GAME_MODE_BOARD_ADAPTER_H