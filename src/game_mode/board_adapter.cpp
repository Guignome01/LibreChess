#include "game_mode/board_adapter.h"

namespace BoardAdapter {
namespace {

BoardPieceType toBoardPieceType(LibreChess::PieceType type) {
  switch (type) {
    case LibreChess::PieceType::PAWN:
      return BoardPieceType::PAWN;
    case LibreChess::PieceType::KNIGHT:
      return BoardPieceType::KNIGHT;
    case LibreChess::PieceType::BISHOP:
      return BoardPieceType::BISHOP;
    case LibreChess::PieceType::ROOK:
      return BoardPieceType::ROOK;
    case LibreChess::PieceType::QUEEN:
      return BoardPieceType::QUEEN;
    case LibreChess::PieceType::KING:
      return BoardPieceType::KING;
    case LibreChess::PieceType::NONE:
    default:
      return BoardPieceType::NONE;
  }
}

BoardMoveTarget moveTargetFor(const LibreChess::Game& game, int fromRow, int fromCol,
                              const LibreChess::Move& move) {
  const int targetRow = LibreChess::squareToRow(move.to);
  const int targetCol = LibreChess::squareToCol(move.to);
  const auto enPassant = game.checkEnPassant(fromRow, fromCol, targetRow, targetCol);
  const bool capturesBoardPiece =
      !LibreChess::Game::isEmptySquare(game.getSquare(targetRow, targetCol));

  BoardMoveTarget target;
  target.row = targetRow;
  target.col = targetCol;
  target.capture = capturesBoardPiece || enPassant.isCapture;
  target.enPassant = enPassant.isCapture;
  target.capturedRow = enPassant.isCapture ? LibreChess::squareToRow(enPassant.capturedPawnSq)
                                           : targetRow;
  target.capturedCol = targetCol;
  return target;
}

}  // namespace

BoardPieceColor toBoardColor(LibreChess::Color color) {
  return color == LibreChess::Color::WHITE ? BoardPieceColor::WHITE : BoardPieceColor::BLACK;
}

LibreChess::Color toGameColor(BoardPieceColor color) {
  return color == BoardPieceColor::WHITE ? LibreChess::Color::WHITE : LibreChess::Color::BLACK;
}

BoardPiece toBoardPiece(LibreChess::Piece piece) {
  if (LibreChess::Game::isEmptySquare(piece)) return {};
  return {toBoardPieceType(LibreChess::Game::pieceType(piece)),
          toBoardColor(LibreChess::Game::pieceColor(piece))};
}

GameProvider::GameProvider(const LibreChess::Game& game) : game_(game) {}

BoardPiece GameProvider::pieceAt(int row, int col) const {
  return toBoardPiece(game_.getSquare(row, col));
}

void GameProvider::setupSnapshot(BoardSetupSnapshot& snapshot) const {
  game_.forEachSquare([&](int row, int col, LibreChess::Piece piece) {
    snapshot.squares[row][col] = toBoardPiece(piece);
  });
}

void GameProvider::legalTargets(int fromRow, int fromCol, BoardMoveTargetList& targets) const {
  targets.clear();
  LibreChess::MoveList moves;
  game_.getPossibleMoves(fromRow, fromCol, moves);
  for (int i = 0; i < moves.count; ++i) {
    targets.addOrMerge(moveTargetFor(game_, fromRow, fromCol, moves.moves[i]));
  }
}

BoardCastlingGuide castlingGuide(const LibreChess::CastlingInfo& castling) {
  BoardCastlingGuide guide;
  guide.isCastling = castling.isCastling;
  if (!guide.isCastling) return guide;
  guide.rookFromRow = LibreChess::squareToRow(castling.rookFromSq);
  guide.rookFromCol = LibreChess::squareToCol(castling.rookFromSq);
  guide.rookToRow = LibreChess::squareToRow(castling.rookToSq);
  guide.rookToCol = LibreChess::squareToCol(castling.rookToSq);
  return guide;
}

BoardMoveCompletion moveCompletion(const LibreChess::MoveResult& result,
                                   const BoardCastlingGuide& castling,
                                   bool isRemoteMove) {
  BoardMoveCompletion completion;
  completion.isRemoteMove = isRemoteMove;
  completion.isCapture = result.isCapture();
  completion.isEnPassant = result.isEnPassant();
  completion.enPassantCapturedPawnRow = result.epCapturedSq == LibreChess::SQ_NONE
                                            ? -1
                                            : LibreChess::squareToRow(result.epCapturedSq);
  completion.castling = castling;
  return completion;
}

BoardMoveFeedbackData moveFeedback(const LibreChess::Game& game,
                                   const LibreChess::MoveResult& result,
                                   int toRow,
                                   int toCol) {
  BoardMoveFeedbackData feedback;
  feedback.capture = result.isCapture();
  feedback.promotion = result.isPromotion();
  feedback.check = result.isCheck();
  feedback.gameEnded = result.gameResult != LibreChess::GameResult::IN_PROGRESS;
  feedback.checkmate = result.gameResult == LibreChess::GameResult::CHECKMATE;
  feedback.winnerColor = result.winnerColor;
  feedback.toRow = toRow;
  feedback.toCol = toCol;
  if (feedback.check && !feedback.gameEnded) {
    const LibreChess::Color turn = game.sideToMove();
    feedback.checkKingRow = game.kingRow(turn);
    feedback.checkKingCol = game.kingCol(turn);
  }
  return feedback;
}

}  // namespace BoardAdapter