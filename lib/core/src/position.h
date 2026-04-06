#ifndef LIBRECHESS_POSITION_H
#define LIBRECHESS_POSITION_H

// Chess position container — board representation and position-level logic.
//
// Owns a BitboardSet (12 piece bitboards + color aggregates + occupancy),
// a parallel Piece mailbox[64] for O(1) piece lookup, current turn, position
// state (castling, en passant, clocks), and Zobrist hash history.
//
// Two move interfaces:
  //   • makeMove(Square from, Square to, promotion) — validated move for game
  //     play.  Detects game-end conditions, returns a full MoveResult with UI
  //     metadata.  Used by Game.
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

#include "bitboard.h"
#include "utils.h"
#include "move.h"
#include "types.h"
#include "zobrist.h"

namespace LibreChess {

// ---------------------------------------------------------------------------
// UndoInfo — saved state for unmake().  Returned by make(), passed to unmake().
// ---------------------------------------------------------------------------

struct UndoInfo {
  PositionState state;    // position state before the move
  uint64_t hash;          // Zobrist hash before the move
  int16_t mgPST;          // material+PST midgame before the move
  int16_t egPST;          // material+PST endgame before the move
  int16_t material;       // material score before the move
  uint16_t historyCount;  // hashHistory_.count before the move
  Piece captured;         // piece captured (Piece::NONE if quiet)
  Square capturedSquare;  // where the capture occurred (differs from `to` for EP)
  int8_t phase;           // game phase before the move (0-24)
  bool epIsLegal;         // cached EP legality before the move
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

  MoveResult makeMove(Square from, Square to, char promotion = ' ');
  void reverseMove(const MoveEntry& entry);
  MoveResult applyMoveEntry(const MoveEntry& entry);

  // --- Raw make/unmake (search, no validation, no game-end detection) ---

  // Apply a move without validation.  Caller guarantees legality.
  // Returns UndoInfo for unmake().
  UndoInfo make(Move m);

  // Reverse a move using saved UndoInfo.  Restores exact pre-move state
  // including hash (one assignment, no recomputation).
  void unmake(Move m, const UndoInfo& undo);

  // --- Null move (search-only) ---
  // Pass the move: flip side-to-move without moving any piece.  Used by
  // null-move pruning to test if "doing nothing" still beats beta.
  // Flips turn, clears EP, toggles side hash key.  No piece state changes.
  // Returns UndoInfo for unmakeNullMove().
  // Reference: https://www.chessprogramming.org/Null_Move_Pruning
  UndoInfo makeNullMove();
  void unmakeNullMove(const UndoInfo& undo);

  // --- Queries ---

  Piece getSquare(Square sq) const {
    return mailbox_[sq];
  }

  Color currentTurn() const { return currentTurn_; }
  Color sideToMove() const { return currentTurn_; }

  Square kingSq(Color c) const { return kingSquare_[piece::raw(c)]; }

  uint8_t getCastlingRights() const { return state_.castlingRights; }
  const PositionState& positionState() const { return state_; }

  std::string getFen() const;

  // Expose bitboard and mailbox for core-internal callers (notation::, etc.)
  const BitboardSet& bitboards() const { return bb_; }
  const Piece* mailbox() const { return mailbox_; }

  uint64_t hash() const { return hash_; }
  bool isRepetition() const;

  // Incremental material+PST accumulators (white-relative centipawns).
  // Updated on make/unmake; used by the search to skip the per-piece loop
  // in evaluatePosition().
  int mgPST() const { return mgPST_; }
  int egPST() const { return egPST_; }

  // Incremental material accumulator (white-relative centipawns, MG values).
  // Updated on make/unmake for captures and promotions only.  Used by lazy
  // evaluation in search to avoid 12 popcount calls per node.
  // Reference: https://www.chessprogramming.org/Incremental_Updates
  int material() const { return material_; }

  // Incremental game phase (sum of non-pawn piece weights on board).
  // Updated on captures/promotions in make/unmake.  Eliminates 4 popcount
  // calls per evaluatePosition() call.  Clamped to 24 (MAX_PHASE) because
  // promotions can push the raw sum above the maximum.
  // Reference: https://www.chessprogramming.org/Game_Phases
  int phase() const {
    constexpr int MAX_PHASE = 24;  // eval::MAX_PHASE
    return (phase_ > MAX_PHASE) ? MAX_PHASE : phase_;
  }

  // --- Convenience wrappers (delegate to static methods / movegen:: / attacks::) ---
  // Instance methods for callers who already have a Position object.

  void getPossibleMoves(Square sq, MoveList& moves) const;
  bool inCheck() const;
  bool isCheckmate() const;
  bool isFiftyMoves() const;
  bool isDraw() const;

  // --- Static game-state detection (raw bitboard + mailbox interface) ---
  // Testable without constructing a Position object.
  // Bodies in position.cpp to avoid movegen.h/attacks.h includes in this header.

  static bool isCheck(const BitboardSet& bb, Color kingColor);

  static bool isCheckmate(const BitboardSet& bb, const Piece mailbox[],
                           Color kingColor, const PositionState& state);
  static bool isStalemate(const BitboardSet& bb, const Piece mailbox[],
                           Color colorToMove, const PositionState& state);

  static bool isInsufficientMaterial(const BitboardSet& bb);
  static bool isThreefoldRepetition(const HashHistory& hashes);
  static bool isFiftyMoveRule(const PositionState& state);

  static bool isDraw(const BitboardSet& bb, const Piece mailbox[],
                     Color colorToMove, const PositionState& state,
                     const HashHistory& hashes);

  static GameResult isGameOver(const BitboardSet& bb, const Piece mailbox[],
                                Color colorToMove, const PositionState& state,
                                const HashHistory& hashes, char& winner);

  std::string boardToText() const;

  EnPassantInfo checkEnPassant(Square from, Square to) const;
  CastlingInfo checkCastling(Square from, Square to) const;

  // --- Board iteration ---
  // Callback: fn(Square sq, Piece piece)
  template <typename Fn>
  void forEachSquare(Fn&& fn) const {
    utils::forEachSquare(mailbox_, static_cast<Fn&&>(fn));
  }

  // --- Constants ---

  static const Piece INITIAL_BOARD[64];

 private:
  // ---------------------------------------------------------------------------
  // UndoCache — 1-deep cache for O(1) state restoration in reverseMove.
  //
  // Stores the Move + UndoInfo from the most recent makeMove(), plus the
  // post-move hash for cache validation.  On cache-hit, reverseMove()
  // delegates to unmake() — one well-tested path for both game and search.
  // On cache-miss (multi-undo, FEN reload), falls back to manual board
  // reversal + full recomputation.
  //
  // This is a pragmatic ESP32-optimized variant of the CPW-recommended
  // undo-stack pattern.  The search path (make/unmake) stores a full UndoInfo
  // per ply; the game path (makeMove/reverseMove) uses this 1-deep cache to
  // avoid O(N) memory while still achieving O(1) restoration for the hot path
  // (perft, immediate undo).
  //
  // Reference: https://www.chessprogramming.org/Copy-Make
  // ---------------------------------------------------------------------------
  struct UndoCache {
    Move move;           // Move that was applied
    UndoInfo undo;       // Full undo state from make()
    uint64_t postHash;   // Zobrist hash after the move (validation key)
    bool valid;          // Whether cache contains valid data
  };

  BitboardSet bb_;
  Piece mailbox_[64];
  Color currentTurn_;
  PositionState state_;
  Square kingSquare_[2];
  uint64_t hash_;
  HashHistory hashHistory_;
  int mgPST_;    // incremental material+PST midgame accumulator
  int egPST_;    // incremental material+PST endgame accumulator
  int material_; // incremental pure material accumulator (white-relative)
  int phase_;    // incremental game phase (sum of non-pawn piece weights)
  bool epIsLegal_ = false;  // cached EP legality for current position
  UndoCache undoCache_{};  // 1-deep cache for reverseMove()

  void recordPosition();
  void initializeBoard();

  // Recompute hash, PST accumulators, and material from scratch.
  // Used after bulk board mutations (FEN load, reverseMove cache-miss)
  // where incremental tracking is not possible.
  void recomputeDerived();

  // Shared incremental update for mgPST_, egPST_, material_ accumulators.
  // Called by make() with the move's captured piece, castling flag, and
  // promoted piece (Piece::NONE when not applicable).
  // Castling rook squares are derived from king from/to when isCastling.
  void updateAccumulators(Piece piece, Square from, Square to,
                          Piece captured, Square capturedSq,
                          bool isCastling, Piece promotedTo);

  // --- make() / makeMove() sub-operations ---
  // Each encapsulates a named chess programming concept used during move
  // application.  Defined in the same TU (position.cpp) so the compiler
  // can inline them in the hot path.

  // Remove captured piece from bitboards and mailbox (EP or normal).
  // Returns the captured piece (NONE if quiet move).
  // Reference: https://www.chessprogramming.org/Make_Move#Captures
  Piece removeCapture(Piece piece, Square to, bool isEP, UndoInfo& undo);

  // Move the castling rook to its destination square.
  // Updates bitboards, mailbox, and Zobrist hash.
  // Reference: https://www.chessprogramming.org/Castling
  void moveCastlingRook(Color color, Square kingFrom, Square kingTo);

  // Swap pawn for promoted piece at destination.  Returns promoted piece.
  // Updates bitboards, mailbox, and Zobrist hash.
  // Reference: https://www.chessprogramming.org/Promotions
  Piece applyPromotion(Move m, Piece pawn, Square to);

  // Derive Move flags (capture, EP, castling, promotion) from coordinates.
  // Reference: https://www.chessprogramming.org/Encoding_Moves
  uint8_t buildMoveFlags(Piece piece, Square from, Square to,
                         char promotion) const;

  // Detect check/checkmate/stalemate/draw after a move.
  // Populates gameResult, winnerColor, and MR_CHECK flag in MoveResult.
  // Reference: https://www.chessprogramming.org/Chess#702
  void detectGameEnd(MoveResult& result) const;

  // Build MoveResult from Move flags (capture, EP, castling, promotion).
  // Called by makeMove() after board mutation to translate internal flags
  // into the UI-facing MoveResult metadata struct.
  // Reference: https://www.chessprogramming.org/Encoding_Moves
  MoveResult buildMoveResult(Move m, Piece piece, Square to) const;

  // Reverse the castling rook during unmake() — moves the rook back from
  // its castling destination to its original corner square.
  // Symmetric counterpart to moveCastlingRook().
  // Reference: https://www.chessprogramming.org/Castling
  void unmakeCastlingRook(Piece king, Square kingFrom, Square kingTo);
};

}  // namespace LibreChess

#endif  // LIBRECHESS_POSITION_H
