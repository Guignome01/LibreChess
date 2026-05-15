#ifndef BOARD_TYPES_H
#define BOARD_TYPES_H

#include "board/core/helpers.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// Board-owned gameplay data contracts.
// ---------------------------------------------------------------------------
// The board subsystem uses these small DTOs and callback interfaces to render
// physical guidance and validate sensor gestures without importing any chess
// engine, Game, MoveList, or provider type. Firmware/game-mode code maps its
// own chess model into these structures at the boundary.
// ---------------------------------------------------------------------------

/// Optional move hinting mode for board-owned chess assistance.
enum class BoardAssistanceLevel : uint8_t {
  NONE = 0,
  LEGAL_MOVES = 1,
  BEST_MOVE = 2,
};

/// Return true only for assistance modes that need engine-backed work.
inline constexpr bool boardAssistanceUsesEngine(BoardAssistanceLevel level) {
  return level == BoardAssistanceLevel::BEST_MOVE;
}

/// Board-local piece colour. Deliberately independent from engine/core colour
/// enums so board code can stay engine-agnostic.
enum class BoardPieceColor : uint8_t {
  WHITE = 0,
  BLACK = 1,
};

/// Board-local piece type. NONE represents an empty square.
enum class BoardPieceType : uint8_t {
  NONE = 0,
  PAWN,
  KNIGHT,
  BISHOP,
  ROOK,
  QUEEN,
  KING,
};

/// Board-local square content.
struct BoardPiece {
  BoardPieceType type = BoardPieceType::NONE;
  BoardPieceColor color = BoardPieceColor::WHITE;

  bool occupied() const { return type != BoardPieceType::NONE; }
};

inline constexpr BoardPieceColor oppositeBoardColor(BoardPieceColor color) {
  return color == BoardPieceColor::WHITE ? BoardPieceColor::BLACK : BoardPieceColor::WHITE;
}

inline constexpr const char* boardColorName(BoardPieceColor color) {
  return color == BoardPieceColor::WHITE ? "White" : "Black";
}

inline constexpr char boardColorCode(BoardPieceColor color) {
  return color == BoardPieceColor::WHITE ? 'w' : 'b';
}

inline constexpr bool boardSquareInBounds(int row, int col) {
  return BoardHelpers::inBounds(row, col);
}

inline constexpr char boardFileChar(int col) { return static_cast<char>('a' + col); }
inline constexpr char boardRankChar(int row) { return static_cast<char>('1' + (7 - row)); }

/// Snapshot of the expected chess position for setup guidance.
struct BoardSetupSnapshot {
  BoardPiece squares[BoardHelpers::ROWS][BoardHelpers::COLS] = {};
};

/// One unique destination square for a lifted piece.
struct BoardMoveTarget {
  int row = -1;
  int col = -1;
  bool capture = false;
  bool enPassant = false;
  int capturedRow = -1;
  int capturedCol = -1;
};

/// Fixed-size legal target list used by physical move validation and LED hints.
struct BoardMoveTargetList {
  static constexpr uint8_t MAX_TARGETS = BoardHelpers::SQUARES;

  BoardMoveTarget targets[MAX_TARGETS] = {};
  uint8_t count = 0;

  void clear() { count = 0; }

  bool addOrMerge(const BoardMoveTarget& target) {
    if (!boardSquareInBounds(target.row, target.col)) return false;
    for (uint8_t i = 0; i < count; ++i) {
      if (targets[i].row != target.row || targets[i].col != target.col) continue;
      targets[i].capture = targets[i].capture || target.capture;
      targets[i].enPassant = targets[i].enPassant || target.enPassant;
      if (target.capturedRow >= 0 && target.capturedCol >= 0) {
        targets[i].capturedRow = target.capturedRow;
        targets[i].capturedCol = target.capturedCol;
      }
      return true;
    }
    if (count >= MAX_TARGETS) return false;
    targets[count++] = target;
    return true;
  }

  const BoardMoveTarget* find(int row, int col) const {
    for (uint8_t i = 0; i < count; ++i) {
      if (targets[i].row == row && targets[i].col == col) return &targets[i];
    }
    return nullptr;
  }

  bool hasTarget(int row, int col) const { return find(row, col) != nullptr; }

  bool hasQuietTarget(int row, int col) const {
    const BoardMoveTarget* target = find(row, col);
    return target != nullptr && !target->capture;
  }

  bool captureForLiftedSquare(int liftedRow, int liftedCol, BoardMoveTarget& targetOut) const {
    for (uint8_t i = 0; i < count; ++i) {
      const BoardMoveTarget& target = targets[i];
      if (!target.capture) continue;
      if (target.capturedRow == liftedRow && target.capturedCol == liftedCol) {
        targetOut = target;
        return true;
      }
    }
    return false;
  }
};

/// Castling rook guidance derived outside the board subsystem.
struct BoardCastlingGuide {
  bool isCastling = false;
  int rookFromRow = -1;
  int rookFromCol = -1;
  int rookToRow = -1;
  int rookToCol = -1;
};

/// Physical completion metadata for an already-applied move.
struct BoardMoveCompletion {
  bool isRemoteMove = false;
  bool isCapture = false;
  bool isEnPassant = false;
  int enPassantCapturedPawnRow = -1;
  BoardCastlingGuide castling;
};

/// Visual feedback metadata for a just-applied move.
struct BoardMoveFeedbackData {
  bool capture = false;
  bool promotion = false;
  bool check = false;
  bool gameEnded = false;
  bool checkmate = false;
  char winnerColor = ' ';
  int toRow = -1;
  int toCol = -1;
  int checkKingRow = -1;
  int checkKingCol = -1;
};

/// Best-move hint returned by an optional assistance provider.
struct BoardBestMoveHint {
  bool valid = false;
  int fromRow = -1;
  int fromCol = -1;
  int toRow = -1;
  int toCol = -1;
  bool capture = false;
  bool enPassant = false;
  int capturedRow = -1;
  int capturedCol = -1;
};

#endif  // BOARD_TYPES_H
