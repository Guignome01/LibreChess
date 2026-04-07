#ifndef LIBRECHESS_GAME_H
#define LIBRECHESS_GAME_H

#include <string>

#include "observer.h"
#include "history.h"
#include "notation.h"
#include "position.h"
#include "evaluation.h"
#include "types.h"

// ---------------------------------------------------------------------------
// Game — central chess game orchestrator.
//
// Composes Position (board representation, position queries) and History
// (move log + persistent recording), with optional IGameObserver for UI
// notification.
//
// All chess-state mutations (makeMove, loadFEN, endGame) flow through this
// class.  It handles move history recording, observer notification, batching,
// and game-end auto-detection.
//
// Recording is automatic: if IGameStorage is provided, Game calls
// history_.setHeader() / history_.save() at game lifecycle boundaries,
// and history_.addMove() transparently persists each move.
//
// Usage:
//   Game game;
//   game.newGame();
//   MoveResult r = game.makeMove("e2e4");  // coordinate string
//   MoveResult r = game.makeMove(SQ_E2, SQ_E4);  // Square-native
//   if (game.isGameOver()) { ... }
// ---------------------------------------------------------------------------
namespace LibreChess {

class Game {
 public:
  Game(IGameStorage* storage = nullptr,
       IGameObserver* observer = nullptr,
       ILogger* logger = nullptr);

  // --- Lifecycle ---

  void newGame();
  void startNewGame(uint8_t playerColor = '?', const uint8_t* meta = nullptr);
  void endGame(GameResult result, char winnerColor);
  void discardRecording();

  // --- Mutations ---

  MoveResult makeMove(Square from, Square to, char promotion = ' ');
  MoveResult makeMove(int fromRow, int fromCol, int toRow, int toCol, char promotion = ' ');
  MoveResult makeMove(const std::string& move);
  bool loadFEN(const std::string& fen);

  // --- Undo / Redo ---

  bool undoMove();
  bool redoMove();
  bool canUndo() const { return history_.canUndo(); }
  bool canRedo() const { return history_.canRedo(); }
  int currentMoveIndex() const { return history_.currentMoveIndex() + 1; }
  int moveCount() const { return history_.moveCount(); }

  int getHistory(std::string out[], int maxMoves,
                 MoveFormat format = MoveFormat::COORDINATE) const;

  // --- Notation helpers ---

  static std::string toCoordinate(int fromRow, int fromCol, int toRow, int toCol, char promotion = ' ');
  static bool parseCoordinate(const std::string& move, int& fromRow, int& fromCol,
                              int& toRow, int& toCol, char& promotion);

  // --- Replay ---

  bool resumeGame();

  // --- Resume queries ---

  bool hasActiveGame();
  bool getActiveGameInfo(uint8_t& playerColor, uint8_t* meta = nullptr);

  // --- Game state (owned by Game) ---

  bool isGameOver() const { return gameOver_; }
  GameResult gameResult() const { return gameResult_; }
  char winnerColor() const { return winnerColor_; }

  // --- Board pass-throughs ---

  const BitboardSet& bitboards() const { return board_.bitboards(); }
  const Piece* mailbox() const { return board_.mailbox(); }
  Piece getSquare(Square sq) const { return board_.getSquare(sq); }
  Piece getSquare(int row, int col) const {
    return board_.getSquare(rowColToSquare(row, col));
  }
  Color sideToMove() const { return board_.sideToMove(); }
  int kingRow(Color c) const { return squareToRow(board_.kingSq(c)); }
  int kingCol(Color c) const { return squareToCol(board_.kingSq(c)); }
  uint8_t getCastlingRights() const { return board_.getCastlingRights(); }
  const PositionState& positionState() const { return board_.positionState(); }
  std::string getFen() const;
  int getEvaluation() const;

  // --- Convenience wrappers ---

  void getPossibleMoves(Square sq, MoveList& moves) const {
    board_.getPossibleMoves(sq, moves);
  }
  void getPossibleMoves(int row, int col, MoveList& moves) const {
    board_.getPossibleMoves(rowColToSquare(row, col), moves);
  }

  bool isDraw() const { return board_.isDraw(); }

  std::string boardToText() const { return board_.boardToText(); }

  // --- Board iteration wrappers ---
  // Firmware callback: fn(int row, int col, Piece piece)
  template <typename Fn>
  void forEachSquare(Fn&& fn) const {
    board_.forEachSquare([&](Square sq, Piece piece) {
      fn(squareToRow(sq), squareToCol(sq), piece);
    });
  }



  EnPassantInfo checkEnPassant(Square from, Square to) const {
    return board_.checkEnPassant(from, to);
  }
  EnPassantInfo checkEnPassant(int fromRow, int fromCol, int toRow, int toCol) const {
    return board_.checkEnPassant(rowColToSquare(fromRow, fromCol),
                                 rowColToSquare(toRow, toCol));
  }

  // --- Piece & coordinate utilities ---
  // Static helpers re-exported from lib/core so firmware never needs to
  // include piece.h or utils.h directly.

  static bool isEmptySquare(Piece p) { return p == Piece::NONE; }
  static Color pieceColor(Piece p) { return piece::pieceColor(p); }
  static PieceType pieceType(Piece p) { return piece::pieceType(p); }
  static char pieceToChar(Piece p) { return piece::pieceToChar(p); }
  static const char* colorName(Color c) { return piece::colorName(c); }
  static std::string squareName(int row, int col) { return ::LibreChess::squareName(row, col); }
  static char fileChar(int col) { return utils::fileChar(col); }
  static char rankChar(int row) { return ::LibreChess::rankChar(row); }

  CastlingInfo checkCastling(Square from, Square to) const {
    return board_.checkCastling(from, to);
  }
  CastlingInfo checkCastling(int fromRow, int fromCol,
                             int toRow, int toCol) const {
    return board_.checkCastling(rowColToSquare(fromRow, fromCol),
                                rowColToSquare(toRow, toCol));
  }

  // --- History ---

  const History& history() const { return history_; }

  // --- Batching (suppress notifications during replay) ---

  void beginBatch();
  void endBatch();

  // --- Direct board access (read-only) ---

  const Position& board() const { return board_; }

 private:
  Position board_;
  History history_;
  IGameObserver* observer_;
  Log logger_;
  int batchDepth_;
  bool batchDirty_;
  std::string startFen_;
  bool gameOver_;
  GameResult gameResult_;
  char winnerColor_;

  // Lazy caches — invalidated on game-layer mutations only.
  // Avoids recomputation when observers query the same state multiple times.
  mutable std::string cachedFen_;
  mutable int cachedEval_;
  mutable bool fenDirty_;
  mutable bool evalDirty_;
  void invalidateCache();

  void notifyObserver();
};

}  // namespace LibreChess

#endif  // CORE_GAME_H
