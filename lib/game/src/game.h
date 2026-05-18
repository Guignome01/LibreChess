#ifndef LIBRECHESS_GAME_H
#define LIBRECHESS_GAME_H

#include <string>

#include "observer.h"
#include "history.h"
#include "notation.h"
#include "position.h"
#include "types.h"
#include "../../core/src/engine.h"

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
  // Display-coordinate target square requested by a caller that wants to rank
  // only a lifted piece's legal destinations.
  struct CandidateTarget {
    int row = -1;
    int col = -1;
  };

  // Fixed-capacity target list for firmware-safe candidate ranking calls.
  struct CandidateTargetList {
    static constexpr int MAX_TARGETS = 64;

    CandidateTarget targets[MAX_TARGETS] = {};
    int count = 0;

    void clear() { count = 0; }
    bool add(int row, int col) {
      if (count >= MAX_TARGETS) return false;
      targets[count++] = CandidateTarget{row, col};
      return true;
    }
    bool contains(int row, int col) const {
      for (int index = 0; index < count; ++index) {
        if (targets[index].row == row && targets[index].col == col) return true;
      }
      return false;
    }
  };

  // Searched score for one candidate destination, from side-to-move's view.
  struct CandidateTargetScore {
    int row = -1;
    int col = -1;
    int score = 0;
  };

  // Fixed-capacity score list. Duplicate destination records keep the best
  // score, which collapses promotion alternatives for the same target square.
  struct CandidateTargetScoreList {
    static constexpr int MAX_SCORES = CandidateTargetList::MAX_TARGETS;

    CandidateTargetScore scores[MAX_SCORES] = {};
    int count = 0;

    void clear() { count = 0; }
    bool record(int row, int col, int score) {
      for (int index = 0; index < count; ++index) {
        if (scores[index].row != row || scores[index].col != col) continue;
        if (score > scores[index].score) scores[index].score = score;
        return true;
      }
      if (count >= MAX_SCORES) return false;
      scores[count++] = CandidateTargetScore{row, col, score};
      return true;
    }
  };

  Game(IGameStorage* storage = nullptr,
       IGameObserver* observer = nullptr,
       ILogger* logger = nullptr);
  ~Game();

  // --- Lifecycle ---

  void newGame();
  void startNewGame(uint8_t playerColor = '?', const uint8_t* meta = nullptr);
  void endGame(GameResult result, char winnerColor);
  void discardRecording();

  // --- Search ---

  // Initialize optional search resources (TT, hash tables, SearchState).
  // Must be called before calculateMove().  Only needed for bot mode —
  // player-only games skip this entirely.  idempotent.
  void initSearch(int ttSize = search::DEFAULT_TT_SIZE);

  // Run the search engine on a snapshot of the current position.
  // Requires initSearch() to have been called first.  If called before
  // initSearch(), returns an empty SearchResult and logs an error — the
  // engine pointer is never dereferenced when null.
  // The live board is not mutated by search make/unmake recursion.
  // Returns the best move, score, depth, and PV.
  search::SearchResult calculateMove(const search::SearchLimits& limits);

  // Search only legal moves from `from` to the requested target squares and
  // return the latest completed root score for each target.  The search runs
  // on a snapshot, does not use the opening book, and leaves the live game,
  // history, observers, and caches unchanged.  Scores are from the current
  // side-to-move perspective, so higher is better for the lifted piece's side.
  bool rankCandidateTargets(int fromRow, int fromCol,
                            const CandidateTargetList& targets,
                            uint32_t timeLimitMs,
                            CandidateTargetScoreList& scores,
                            int maxDepth = search::MAX_PLY);

  // Set the platform time function (firmware passes millis()).
  // Must be called after initSearch().
  void setTimeFunc(search::TimeFunc fn);

  // Wire an external stop flag for search cancellation.
  // Must be called after initSearch().
  void setExternalStop(std::atomic<bool>* flag);

  // Search resource diagnostics for firmware heap-pressure handling.
  bool searchInitialized() const { return engine_ != nullptr; }
  bool searchHashTablesReady() const {
    return engine_ != nullptr && engine_->hashTablesReady();
  }
  bool searchHashTableAllocationFailed() const {
    return engine_ != nullptr && engine_->hashTableAllocationFailed();
  }

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

  // Score a candidate move on a private position copy without changing the
  // live game, history, observers, or caches. Returns a score from the moving
  // side's perspective, so higher is better for the side that owns `from`.
  bool scoreCandidateMove(int fromRow, int fromCol, int toRow, int toCol,
                          int& scoreOut, char promotion = ' ') const;

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

  // --- Optional search resources (allocated by initSearch) ---
  Engine* engine_ = nullptr;

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
