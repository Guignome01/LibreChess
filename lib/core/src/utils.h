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

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

// LERF-native helpers (canonical — use in new code).
// file 0 = 'a', rank 0 = '1'.
inline constexpr char fileChar(int file) { return 'a' + file; }
inline constexpr char rankCharFromRank(int rank) { return '1' + rank; }
inline constexpr int fileIndex(char file) { return file - 'a'; }
inline constexpr int rankIndexFromChar(char rank) { return rank - '1'; }

/// Parse an algebraic square ("e4" style) into a LERF Square.
/// Returns true on valid input (file in 'a'..'h', rank in '1'..'8');
/// returns false without writing `sq` otherwise.  Shared by coordinate-move
/// parsing (notation.cpp) and FEN en-passant-square parsing (fen.cpp).
inline bool parseSquareAlgebraic(char file, char rank, Square& sq) {
  if (file < 'a' || file > 'h' || rank < '1' || rank > '8') return false;
  sq = makeSquare(rankIndexFromChar(rank), fileIndex(file));
  return true;
}

/// Square name from LERF Square (e.g. SQ_E4 → "e4").
/// Reference: https://www.chessprogramming.org/Square_Mapping_Considerations
inline std::string squareName(Square sq) {
  return {fileChar(fileOf(sq)), rankCharFromRank(rankOf(sq))};
}

// Display-coordinate converters (rowColToSquare, squareToRow, squareToCol,
// rankChar, squareName) live in game/types.h.

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
/// @return EnPassantInfo with isCapture, capturedPawnSq, nextEpSquare
/// Reference: https://www.chessprogramming.org/En_passant
inline EnPassantInfo checkEnPassant(const Piece mailbox[], Square from,
                                    Square to) {
  Piece movedPiece = mailbox[from];
  Piece targetPiece = mailbox[to];

  EnPassantInfo info{};

  bool isPawn = piece::pieceType(movedPiece) == PieceType::PAWN;

  // En passant: pawn captures diagonally to an empty square
  info.isCapture = isPawn && fileOf(from) != fileOf(to) &&
                   targetPiece == Piece::NONE;
  if (info.isCapture) {
    // Captured pawn is one rank behind the EP target (opposite of pawn direction)
    int capturedRank = rankOf(to) +
        (piece::pieceColor(movedPiece) == Color::WHITE ? -1 : 1);
    info.capturedPawnSq = makeSquare(capturedRank, fileOf(to));
  }

  // Pawn double-push creates an EP target for the opponent
  int rankDiff = rankOf(to) - rankOf(from);
  if (isPawn && (rankDiff == 2 || rankDiff == -2)) {
    int epRank = (rankOf(from) + rankOf(to)) / 2;
    info.nextEpSquare = makeSquare(epRank, fileOf(from));
  }

  return info;
}

/// Detect whether a move is castling and determine rook source/dest squares.
/// @param mailbox  64-element piece array (LERF indexed)
/// @param from     origin square (king's square)
/// @param to       destination square
/// @return CastlingInfo with isCastling, rookFromSq, rookToSq
/// Reference: https://www.chessprogramming.org/Castling
inline CastlingInfo checkCastling(const Piece mailbox[], Square from,
                                  Square to) {
  Piece movedPiece = mailbox[from];

  CastlingInfo info{};

  int fileDiff = fileOf(to) - fileOf(from);
  info.isCastling = piece::pieceType(movedPiece) == PieceType::KING &&
                    rankOf(from) == rankOf(to) &&
                    (fileDiff == 2 || fileDiff == -2);
  if (info.isCastling) {
    int rank = rankOf(from);
    // Kingside: rook h-file (7) → f-file (5)
    // Queenside: rook a-file (0) → d-file (3)
    int rookFromFile = (fileDiff == 2) ? 7 : 0;
    int rookToFile   = (fileDiff == 2) ? 5 : 3;
    info.rookFromSq = makeSquare(rank, rookFromFile);
    info.rookToSq   = makeSquare(rank, rookToFile);
  }

  return info;
}

// ---------------------------------------------------------------------------
// King square resolution — finds the king square for a given color.
//
// The king is located via the bitboard set.  Returns false (and leaves
// `kingSq` unmodified) if no king is present for the given color.
//
// This is the single source of truth for the repeated "find king → lsb"
// pattern used throughout movegen and search.
//
// Reference: https://www.chessprogramming.org/King_Pattern
// ---------------------------------------------------------------------------
inline bool resolveKingSquare(const BitboardSet& bb, Color color,
                              Square& kingSq) {
  int kidx = piece::pieceIndex(color, PieceType::KING);
  Bitboard kingBB = bb.byPiece[kidx];
  if (!kingBB) return false;
  kingSq = lsb(kingBB);
  return true;
}

// Is char a valid promotion piece letter? (case-insensitive: q, r, b, n)
inline bool isValidPromotionChar(char c) {
  char lower = static_cast<char>(tolower(c));
  return lower == 'q' || lower == 'r' || lower == 'b' || lower == 'n';
}

// Round down to the nearest power of 2.  Used by TranspositionTable,
// PawnHashTable, and EvalHashTable for fast modular indexing.
inline int roundDownPow2(int n) {
  if (n <= 0) return 0;
  int v = 1;
  while (v * 2 <= n) v *= 2;
  return v;
}

// ---------------------------------------------------------------------------
// Board iteration helpers — bitboard-based traversal.
//
// forEachSquare: iterates all 64 squares reading from the mailbox.
// forEachPiece:  iterates only occupied squares via popLsb serialization.
//
// Callbacks receive (Square sq, Piece piece) using LERF square indexing.
// ---------------------------------------------------------------------------

// Iterate all 64 squares in LERF order (a1=0 → h8=63).
// Callback: fn(Square sq, Piece piece)
template <typename Fn>
inline void forEachSquare(const Piece mailbox[], Fn&& fn) {
  for (Square sq = 0; sq < 64; ++sq)
    fn(sq, mailbox[sq]);
}

// Iterate only occupied squares via bitboard serialization.
// Callback: fn(Square sq, Piece piece)
template <typename Fn>
inline void forEachPiece(const BitboardSet& bb,
                         const Piece mailbox[], Fn&& fn) {
  Bitboard occ = bb.occupied;
  while (occ) {
    Square sq = popLsb(occ);
    fn(sq, mailbox[sq]);
  }
}

}  // namespace utils
}  // namespace LibreChess

#endif  // CORE_UTILS_H
