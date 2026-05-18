#ifndef LIBRECHESS_HISTORY_H
#define LIBRECHESS_HISTORY_H

#include <cstdint>
#include <string>

#include "storage.h"
#include "logger.h"
#include "move.h"
#include "piece.h"
#include "types.h"

namespace LibreChess {

class Position;  // forward declaration for replayInto()

// MoveEntry is defined in move.h (shared between core and game layers).

// ---------------------------------------------------------------------------
// History — in-memory game history with optional persistent recording.
//
// Two concerns, unified under a single API:
//   1. Move log: ordered list of all moves with cursor-based undo/redo
//   2. Recording: persistent game storage (when IGameStorage is provided)
//
// Recording is automatic: if an IGameStorage* is provided and a header has
// been set (via setHeader), addMove() persists encoded moves transparently.
//
// Undo/redo: a cursor (currentIndex_) tracks the "current" position in the
// move log.  undoMove() steps back, redoMove() steps forward.  addMove()
// wipes any moves after the cursor (branch point) before appending.
//
// Nullable storage: pass nullptr to disable recording.
// ---------------------------------------------------------------------------
class History {
 public:
  History(IGameStorage* storage = nullptr, ILogger* logger = nullptr);

  // Reset in-memory history (move log + cursor).
  void clear();

  // --- Move log with undo/redo ---

  void addMove(const MoveEntry& entry);
  const MoveEntry* undoMove();
  const MoveEntry* redoMove();

  bool canUndo() const { return currentIndex_ >= 0; }
  bool canRedo() const { return currentIndex_ < moveCount_ - 1; }
  int currentMoveIndex() const { return currentIndex_; }

  // --- Move log queries ---

  int moveCount() const { return moveCount_; }
  bool empty() const { return moveCount_ == 0; }
  const MoveEntry& getMove(int index) const;
  const MoveEntry& lastMove() const;

  // --- Recording (persistent storage) ---

  void setHeader(const GameHeader& header);
  void snapshotPosition(const std::string& fen);
  void save(GameResult result, char winnerColor);
  void discard();
  bool isRecording() const { return recordingActive_; }

  // --- State queries (persistent storage) ---

  bool hasActiveGame();
  bool getActiveGameInfo(uint8_t& playerColor, uint8_t* meta = nullptr);

  // --- Header accessors ---

  GameResult headerResult() const { return header_.result; }
  char headerWinnerColor() const { return static_cast<char>(header_.winnerColor); }

  // --- Replay ---

  bool replayInto(Position& board);
  const std::string& replayFen() const { return replayFen_; }

  // --- Compact 2-byte move encoding (binary storage format) ---

  static uint16_t encodeMove(Square from, Square to, char promotion);
  static void decodeMove(uint16_t encoded, Square& from, Square& to,
                          char& promotion);

  // --- Constants ---

  static constexpr int MAX_MOVES = 300;

 private:
  MoveEntry moves_[MAX_MOVES];
  int moveCount_;
  int currentIndex_;

  IGameStorage* storage_;
  Log logger_;
  GameHeader header_;
  bool recordingActive_;
  bool headerInitialized_;

  void persistMove(const MoveEntry& entry);
  std::string replayFen_;
};

}  // namespace LibreChess

#endif  // CORE_HISTORY_H
