#ifndef LIBRECHESS_GAME_H
#define LIBRECHESS_GAME_H

#include <string>

#include "iterator.h"
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
//   MoveResult r = game.makeMove(6, 4, 4, 4);  // e2e4
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
  bool parseMove(const std::string& move, int& fromRow, int& fromCol,
                 int& toRow, int& toCol, char& promotion) const;

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
  Piece getSquare(int row, int col) const { return board_.getSquare(row, col); }
  Color currentTurn() const { return board_.currentTurn(); }
  int kingRow(Color c) const { return board_.kingRow(c); }
  int kingCol(Color c) const { return board_.kingCol(c); }
  uint8_t getCastlingRights() const { return board_.getCastlingRights(); }
  const PositionState& positionState() const { return board_.positionState(); }
  std::string getFen() const;
  int getEvaluation() const;
  float getEvaluationFloat() const { return getEvaluation() / 100.0f; }

  // --- Convenience wrappers ---

  void getPossibleMoves(int row, int col, MoveList& moves) const {
    board_.getPossibleMoves(row, col, moves);
  }

  bool isCheck(Color kingColor) const { return board_.isCheck(kingColor); }
  bool inCheck() const { return board_.inCheck(); }
  bool isCheckmate() const { return board_.isCheckmate(); }
  bool isStalemate() const { return board_.isStalemate(); }
  bool isInsufficientMaterial() const { return board_.isInsufficientMaterial(); }
  bool isFiftyMoves() const { return board_.isFiftyMoves(); }
  bool isDraw() const { return board_.isDraw(); }
  bool isThreefoldRepetition() const { return board_.isThreefoldRepetition(); }

  bool isAttacked(int row, int col, Color byColor) const {
    return board_.isAttacked(row, col, byColor);
  }

  int findPiece(Piece target, int positions[][2], int maxPositions) const {
    return board_.findPiece(target, positions, maxPositions);
  }

  std::string boardToText() const { return board_.boardToText(); }

  // --- Board iteration wrappers ---

  template <typename Fn>
  void forEachSquare(Fn&& fn) const {
    iterator::forEachSquare(board_.mailbox(), static_cast<Fn&&>(fn));
  }

  template <typename Fn>
  void forEachPiece(Fn&& fn) const {
    iterator::forEachPiece(board_.bitboards(), board_.mailbox(), static_cast<Fn&&>(fn));
  }

  template <typename Fn>
  bool somePiece(Fn&& fn) const {
    return iterator::somePiece(board_.bitboards(), board_.mailbox(), static_cast<Fn&&>(fn));
  }

  int moveNumber() const { return board_.moveNumber(); }

  utils::EnPassantInfo checkEnPassant(int fromRow, int fromCol, int toRow, int toCol) const {
    return board_.checkEnPassant(fromRow, fromCol, toRow, toCol);
  }

  utils::CastlingInfo checkCastling(int fromRow, int fromCol, int toRow, int toCol) const {
    return board_.checkCastling(fromRow, fromCol, toRow, toCol);
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
