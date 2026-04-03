#ifndef LIBRECHESS_UTILS_H
#define LIBRECHESS_UTILS_H

#include <cctype>
#include <cstdint>
#include <string>

#include "bitboard.h"
#include "piece.h"
#include "types.h"

namespace LibreChess {
namespace utils {

// ---------------------------------------------------------------------------
// Game result name lookup
// ---------------------------------------------------------------------------

inline const char* gameResultName(GameResult result) {
  static constexpr const char* NAMES[] = {
      "In progress",                  // 0 = IN_PROGRESS
      "Checkmate",                    // 1 = CHECKMATE
      "Stalemate",                    // 2 = STALEMATE
      "Draw (50-move rule)",          // 3 = DRAW_50
      "Draw (threefold repetition)",  // 4 = DRAW_3FOLD
      "Resignation",                  // 5 = RESIGNATION
      "Draw (insufficient material)", // 6 = DRAW_INSUFFICIENT
      "Draw (agreement)",             // 7 = DRAW_AGREEMENT
      "Timeout",                      // 8 = TIMEOUT
      "Aborted",                      // 9 = ABORTED
  };
  auto idx = static_cast<uint8_t>(result);
  return (idx < sizeof(NAMES) / sizeof(NAMES[0])) ? NAMES[idx] : "Unknown";
}

// --- Castling rights ---
// Bitmask: 0x01 = K, 0x02 = Q, 0x04 = k, 0x08 = q.

inline uint8_t castlingCharToBit(char c) {
  switch (c) {
    case 'K': return 0x01;
    case 'Q': return 0x02;
    case 'k': return 0x04;
    case 'q': return 0x08;
    default:  return 0;
  }
}

inline bool hasCastlingRight(uint8_t castlingRights, Color color, bool kingSide) {
  // Direct bit lookup: [raw(Color)][kingSide] → castling bitmask.
  // [WHITE][queenside=0]=Q(0x02), [WHITE][kingside=1]=K(0x01)
  // [BLACK][queenside=0]=q(0x08), [BLACK][kingside=1]=k(0x04)
  static constexpr uint8_t BIT[2][2] = {{0x02, 0x01}, {0x08, 0x04}};
  return (castlingRights & BIT[raw(color)][kingSide]) != 0;
}

inline std::string castlingRightsToString(uint8_t rights) {
  std::string s;
  for (char c : {'K', 'Q', 'k', 'q'})
    if (rights & castlingCharToBit(c)) s += c;
  if (s.empty()) s = "-";
  return s;
}

inline uint8_t castlingRightsFromString(const std::string& rightsStr) {
  uint8_t rights = 0;
  for (size_t i = 0; i < rightsStr.length(); i++)
    rights |= castlingCharToBit(rightsStr[i]);
  return rights;
}

// Coordinate helpers — single source of truth for the row/col ↔ rank/file mapping.
// Board convention: row 0 = rank 8 (black back rank), col 0 = file 'a'.
inline constexpr char fileChar(int col) { return 'a' + col; }
inline constexpr char rankChar(int row) { return '1' + (7 - row); }
inline constexpr int fileIndex(char file) { return file - 'a'; }
inline constexpr int rankIndex(char rank) { return 8 - (rank - '0'); }

inline std::string squareName(int row, int col) {
  return {fileChar(col), rankChar(row)};
}

// ---------------------------------------------------------------------------
// Castling rights update — lookup table approach.
//
// A move FROM or TO a special square (king start, rook corner) masks out
// the corresponding castling bits.  For non-special squares the mask is
// 0xFF (no-op AND).  This replaces 8 if-statements with two table lookups.
//
// Castling bits: 0x01=WK, 0x02=WQ, 0x04=BK, 0x08=BQ.
// Reference: https://www.chessprogramming.org/Castling_Rights
// ---------------------------------------------------------------------------

// Square-based overload — preferred when LERF squares are already available.
inline uint8_t updateCastlingRights(uint8_t rights, Square from, Square to) {
  // clang-format off
  static constexpr uint8_t CASTLING_MASK[64] = {
    // Rank 1: a1       b1    c1    d1    e1          f1    g1    h1
       0xFD, 0xFF, 0xFF, 0xFF, 0xFC, 0xFF, 0xFF, 0xFE,
    // Ranks 2–7
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // Rank 8: a8       b8    c8    d8    e8          f8    g8    h8
       0xF7, 0xFF, 0xFF, 0xFF, 0xF3, 0xFF, 0xFF, 0xFB
  };
  // clang-format on
  return rights & CASTLING_MASK[from] & CASTLING_MASK[to];
}

// ---------------------------------------------------------------------------
// En passant / castling detection — pure functions over mailbox + squares.
// Shared by Position member methods (row/col API) and movegen (Square API).
// Reference: https://www.chessprogramming.org/En_passant
// Reference: https://www.chessprogramming.org/Castling
// ---------------------------------------------------------------------------

/// Detect whether a move is an en passant capture or creates an EP target.
/// @param mailbox  64-element piece array (LERF indexed)
/// @param from     origin square
/// @param to       destination square
/// @return EnPassantInfo with isCapture, capturedPawnRow, nextEpRow/Col
inline EnPassantInfo checkEnPassant(const Piece mailbox[], Square from,
                                    Square to) {
  Piece movedPiece = mailbox[from];
  Piece targetPiece = mailbox[to];

  EnPassantInfo info{};
  info.capturedPawnRow = -1;
  info.nextEpRow = -1;
  info.nextEpCol = -1;

  bool isPawn = piece::pieceType(movedPiece) == PieceType::PAWN;

  // En passant: pawn captures diagonally to an empty square
  info.isCapture = isPawn && colOf(from) != colOf(to) &&
                   targetPiece == Piece::NONE;
  if (info.isCapture)
    info.capturedPawnRow =
        rowOf(to) - piece::pawnDirection(piece::pieceColor(movedPiece));

  // Pawn double-push creates an EP target for the opponent
  int rowDiff = rowOf(to) - rowOf(from);
  if (isPawn && (rowDiff == 2 || rowDiff == -2)) {
    info.nextEpRow = (rowOf(from) + rowOf(to)) / 2;
    info.nextEpCol = colOf(from);
  }

  return info;
}

/// Detect whether a move is castling and determine rook source/dest columns.
/// @param mailbox  64-element piece array (LERF indexed)
/// @param from     origin square (king's square)
/// @param to       destination square
/// @return CastlingInfo with isCastling, rookFromCol, rookToCol
inline CastlingInfo checkCastling(const Piece mailbox[], Square from,
                                  Square to) {
  Piece movedPiece = mailbox[from];

  CastlingInfo info{};
  info.rookFromCol = -1;
  info.rookToCol = -1;

  int colDiff = colOf(to) - colOf(from);
  info.isCastling = piece::pieceType(movedPiece) == PieceType::KING &&
                    rowOf(from) == rowOf(to) &&
                    (colDiff == 2 || colDiff == -2);
  if (info.isCastling) {
    info.rookFromCol = (colDiff == 2) ? 7 : 0;
    info.rookToCol = (colDiff == 2) ? 5 : 3;
  }

  return info;
}

// --- General-purpose board queries ---

// Is (row, col) within the 8×8 board?
inline constexpr bool isValidSquare(int row, int col) {
  return (unsigned)row < 8 && (unsigned)col < 8;
}

// Is char a valid promotion piece letter? (case-insensitive: q, r, b, n)
inline bool isValidPromotionChar(char c) {
  char lower = static_cast<char>(tolower(c));
  return lower == 'q' || lower == 'r' || lower == 'b' || lower == 'n';
}

}  // namespace utils
}  // namespace LibreChess

#endif  // CORE_UTILS_H
