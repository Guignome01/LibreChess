#ifndef LIBRECHESS_POSITION_H
#define LIBRECHESS_POSITION_H

// Chess position container — board representation and position-level logic.
//
// Owns a BitboardSet (12 piece bitboards + color aggregates + occupancy),
// a parallel Piece mailbox[64] for O(1) piece lookup, current turn, position
// state (castling, en passant, clocks), and Zobrist hash history.
//
// Two move interfaces:
//   • makeMove(row, col, row, col, promotion) — validated move for game play.
//     Delegates to rules:: for legality, detects game-end conditions, returns
//     a full MoveResult with UI metadata.  Used by Game.
//   • make(Move) / unmake(Move, UndoInfo) — raw make/unmake for search.
//     No validation, no game-end detection, incremental hash.  Caller
//     guarantees legality.  unmake restores hash from UndoInfo (one assignment,
//     no recomputation).
//
// Pure position container: no lifecycle state (game-over flag, result, winner).
// Lifecycle authority lives in Game.
// Pure logic, no hardware dependencies, natively compilable for unit tests.

#include <cstring>
#include <string>

#include "attacks.h"
#include "bitboard.h"
#include "iterator.h"
#include "utils.h"
#include "evaluation.h"
#include "move.h"
#include "movegen.h"
#include "rules.h"
#include "types.h"
#include "zobrist.h"

namespace LibreChess {

// ---------------------------------------------------------------------------
// UndoInfo — saved state for unmake().  Returned by make(), passed to unmake().
// ---------------------------------------------------------------------------

struct UndoInfo {
  PositionState state;    // position state before the move
  uint64_t hash;          // Zobrist hash before the move
  Piece captured;         // piece captured (Piece::NONE if quiet)
  Square capturedSquare;  // where the capture occurred (differs from `to` for EP)
  int historyCount;       // hashHistory_.count before the move
};

// ---------------------------------------------------------------------------
// Position class
// ---------------------------------------------------------------------------

class Position {
 public:
  Position();

  // --- Lifecycle ---

  void newGame();
  bool loadFEN(const std::string& fen);

  // --- Validated move (game play, used by Game) ---

  MoveResult makeMove(int fromRow, int fromCol, int toRow, int toCol, char promotion = ' ');
  void reverseMove(const MoveEntry& entry);
  MoveResult applyMoveEntry(const MoveEntry& entry);

  // --- Raw make/unmake (search, no validation, no game-end detection) ---

  // Apply a move without validation.  Caller guarantees legality.
  // Returns UndoInfo for unmake().
  UndoInfo make(Move m);

  // Reverse a move using saved UndoInfo.  Restores exact pre-move state
  // including hash (one assignment, no recomputation).
  void unmake(Move m, const UndoInfo& undo);

  // --- Queries ---

  Piece getSquare(int row, int col) const {
    return mailbox_[squareOf(row, col)];
  }

  Piece pieceOn(Square sq) const { return mailbox_[sq]; }
  Color currentTurn() const { return currentTurn_; }
  Color sideToMove() const { return currentTurn_; }

  int kingRow(Color c) const { return rowOf(kingSquare_[piece::raw(c)]); }
  int kingCol(Color c) const { return colOf(kingSquare_[piece::raw(c)]); }
  Square kingSquare(Color c) const { return kingSquare_[piece::raw(c)]; }

  uint8_t getCastlingRights() const { return state_.castlingRights; }
  const PositionState& positionState() const { return state_; }

  std::string getFen() const;
  int getEvaluation() const;

  // Expose bitboard and mailbox for core-internal callers (notation::, etc.)
  const BitboardSet& bitboards() const { return bb_; }
  const Piece* mailbox() const { return mailbox_; }

  uint64_t hash() const { return hash_; }
  bool isRepetition() const { return rules::isThreefoldRepetition(hashHistory_); }

  // --- Convenience wrappers (delegate to movegen:: / utils::) ---

  void getPossibleMoves(int row, int col, MoveList& moves) const {
    movegen::getPossibleMoves(bb_, mailbox_, row, col, state_, moves);
  }

  bool isCheck(Color kingColor) const {
    return attacks::isSquareUnderAttack(bb_, kingSquare_[piece::raw(kingColor)], kingColor);
  }

  bool inCheck() const {
    return attacks::isSquareUnderAttack(bb_, kingSquare_[piece::raw(currentTurn_)], currentTurn_);
  }

  bool isCheckmate() const {
    return rules::isCheckmate(bb_, mailbox_, currentTurn_, state_);
  }

  bool isStalemate() const {
    return rules::isStalemate(bb_, mailbox_, currentTurn_, state_);
  }

  bool isInsufficientMaterial() const {
    return rules::isInsufficientMaterial(bb_);
  }

  bool isFiftyMoves() const {
    return rules::isFiftyMoveRule(state_);
  }

  bool isThreefoldRepetition() const {
    return rules::isThreefoldRepetition(hashHistory_);
  }

  bool isDraw() const {
    return rules::isDraw(bb_, mailbox_, currentTurn_, state_, hashHistory_);
  }

  bool isAttacked(int row, int col, Color byColor) const {
    Color defendingColor = ~byColor;
    return attacks::isSquareUnderAttack(bb_, row, col, defendingColor);
  }

  int findPiece(Piece target, int positions[][2], int maxPositions) const {
    return iterator::findPiece(bb_, target, positions, maxPositions);
  }

  std::string boardToText() const;

  int moveNumber() const { return state_.fullmoveClock; }

  utils::EnPassantInfo checkEnPassant(int fromRow, int fromCol, int toRow, int toCol) const {
    return utils::checkEnPassant(fromRow, fromCol, toRow, toCol,
                                      mailbox_[squareOf(fromRow, fromCol)],
                                      mailbox_[squareOf(toRow, toCol)]);
  }

  utils::CastlingInfo checkCastling(int fromRow, int fromCol, int toRow, int toCol) const {
    return utils::checkCastling(fromRow, fromCol, toRow, toCol,
                                     mailbox_[squareOf(fromRow, fromCol)]);
  }

  // --- Board iteration ---

  template <typename Fn>
  void forEachSquare(Fn&& fn) const {
    iterator::forEachSquare(mailbox_, static_cast<Fn&&>(fn));
  }

  template <typename Fn>
  void forEachPiece(Fn&& fn) const {
    iterator::forEachPiece(bb_, mailbox_, static_cast<Fn&&>(fn));
  }

  template <typename Fn>
  bool somePiece(Fn&& fn) const {
    return iterator::somePiece(bb_, mailbox_, static_cast<Fn&&>(fn));
  }

  // --- Constants ---

  static const Piece INITIAL_BOARD[8][8];

 private:
  BitboardSet bb_;
  Piece mailbox_[64];
  Color currentTurn_;
  PositionState state_;
  Square kingSquare_[2];
  uint64_t hash_;
  HashHistory hashHistory_;

  mutable std::string cachedFen_;
  mutable int cachedEval_;
  mutable bool fenDirty_;
  mutable bool evalDirty_;
  void invalidateCache();

  void recordPosition();
  void applyMoveToBoard(int fromRow, int fromCol, int toRow, int toCol, char promotion, MoveResult& result);
  void advanceTurn();
};

}  // namespace LibreChess

#endif  // LIBRECHESS_POSITION_H
