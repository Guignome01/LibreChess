#ifndef LIBRECHESS_TYPES_H
#define LIBRECHESS_TYPES_H

// Core type definitions for the LibreChess chess library.
//
// Foundation layer — leaf of the dependency tree. No includes beyond <cstdint>.
// Defines the three fundamental enums (Color, PieceType, Piece) and their
// raw() unwrap helpers, plus GameResult, gameResultName(), PositionState,
// HashHistory, and MoveFormat.
//
// Game-management types (GameHeader, recording constants) live
// in lib/game/src/types.h.

#include <cstdint>

namespace LibreChess {

// ---------------------------------------------------------------------------
// Square type (used by PositionState, EnPassantInfo, CastlingInfo)
// ---------------------------------------------------------------------------
// Defined here (foundation layer) so bitboard.h can include piece.h → types.h
// without circular dependencies.  Canonical definition; bitboard.h re-uses it.
//
// uint8_t is sufficient for 0-63 (valid squares) plus the SQ_NONE sentinel.
// Arithmetic on squares (e.g. sq + NORTH) promotes to int automatically;
// cast back to Square on assignment when needed.

using Square = uint8_t;  // 0-63 (LERF), or SQ_NONE (255)

constexpr Square SQ_NONE = 255;

// ---------------------------------------------------------------------------
// Core chess enums
// ---------------------------------------------------------------------------

enum class Color : uint8_t { WHITE = 0, BLACK = 1 };

enum class PieceType : uint8_t {
  NONE = 0,
  PAWN = 1,
  KNIGHT = 2,
  BISHOP = 3,
  ROOK = 4,
  QUEEN = 5,
  KING = 6
};

// Unwrap enum to underlying uint8_t — eliminates static_cast noise.
constexpr uint8_t raw(Color c) { return static_cast<uint8_t>(c); }
constexpr uint8_t raw(PieceType t) { return static_cast<uint8_t>(t); }

// Color bit offset for Piece composition.
constexpr uint8_t BLACK_BIT = raw(Color::BLACK) << 3;  // = 8

// Piece = (Color << 3) | PieceType.
// Values 7, 8, 15 are unused gaps — intentional for bit extraction.
enum class Piece : uint8_t {
  NONE = 0,
  W_PAWN = raw(PieceType::PAWN),
  W_KNIGHT = raw(PieceType::KNIGHT),
  W_BISHOP = raw(PieceType::BISHOP),
  W_ROOK = raw(PieceType::ROOK),
  W_QUEEN = raw(PieceType::QUEEN),
  W_KING = raw(PieceType::KING),
  B_PAWN = BLACK_BIT | raw(PieceType::PAWN),
  B_KNIGHT = BLACK_BIT | raw(PieceType::KNIGHT),
  B_BISHOP = BLACK_BIT | raw(PieceType::BISHOP),
  B_ROOK = BLACK_BIT | raw(PieceType::ROOK),
  B_QUEEN = BLACK_BIT | raw(PieceType::QUEEN),
  B_KING = BLACK_BIT | raw(PieceType::KING),
};

// Deferred definition (needs complete Piece type).
constexpr uint8_t raw(Piece p) { return static_cast<uint8_t>(p); }

// Color flip operator.
constexpr Color operator~(Color c) {
  return static_cast<Color>(raw(c) ^ 1);
}

// Piece color flip (toggle bit 3). Only meaningful for non-NONE pieces.
constexpr Piece operator~(Piece p) {
  return static_cast<Piece>(raw(p) ^ BLACK_BIT);
}

// ---------------------------------------------------------------------------
// Game-level enums
// ---------------------------------------------------------------------------

// Game result codes — stored in game recording binary format.
// New values must be appended so older recording files remain readable.
enum class GameResult : uint8_t {
  IN_PROGRESS = 0,
  CHECKMATE = 1,
  STALEMATE = 2,
  DRAW_50 = 3,
  DRAW_3FOLD = 4,
  RESIGNATION = 5,
  DRAW_INSUFFICIENT = 6,
  DRAW_AGREEMENT = 7,
  TIMEOUT = 8,
  ABORTED = 9
};

/// Maps a GameResult to a human-readable display string.
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

// ---------------------------------------------------------------------------
// Position state
// ---------------------------------------------------------------------------

// Complete position state for chess operations.
// Position's static methods are stateless; the caller supplies a PositionState for
// position-dependent queries (castling rights, en passant target).
// Position owns the authoritative instance and also uses
// halfmoveClock / fullmoveClock for FEN serialization and draw detection.
struct PositionState {
  uint8_t castlingRights = 0x0F;  // KQkq bitmask (bits 0-3)
  Square epSquare = SQ_NONE;      // en passant target square (SQ_NONE if none)
  uint8_t halfmoveClock = 0;      // half-move clock (50-move rule, max 100)
  uint16_t fullmoveClock = 1;     // full-move counter (starts at 1)

  // Standard starting position state (identical to default construction).
  static PositionState initial() { return {}; }
};

// Fixed-capacity Zobrist hash history for repetition detection.
// Must be large enough to hold the longest plausible sequence of reversible
// moves from both the game (halfmove clock saturates at the 100 half-move
// rule limit) and the search tree (MAX_PLY deep). 256 entries = 2 KiB,
// leaving room for the reversible-move window plus search plies.
struct HashHistory {
  static constexpr int MAX_SIZE = 256;

  uint64_t keys[MAX_SIZE];
  int count = 0;
};

// Move notation format identifiers — used by Game::getHistory().
enum class MoveFormat : uint8_t {
  COORDINATE = 0,  // "e2e4", "e7e8q"  (UCI protocol notation)
  SAN = 1,         // "e4", "Nxf3", "O-O", "e8=Q+"  (Standard Algebraic)
  LAN = 2          // "e2-e4", "Ng1xf3", "O-O", "e7-e8=Q+"  (Long Algebraic)
};

// ---------------------------------------------------------------------------
// En passant analysis — combines EP-capture detection and EP-target setting
// into one return value so callers don't scatter multiple inline checks.
// Reference: https://www.chessprogramming.org/En_passant
// ---------------------------------------------------------------------------

struct EnPassantInfo {
  bool isCapture;                        // This move is an EP capture
  Square capturedPawnSq = SQ_NONE;       // Square of captured EP pawn
  Square nextEpSquare = SQ_NONE;         // EP target square for the *next* move
};

// ---------------------------------------------------------------------------
// Castling analysis — combines castling detection + rook squares so the
// board layer can apply the move in one pass.
// Reference: https://www.chessprogramming.org/Castling
// ---------------------------------------------------------------------------

struct CastlingInfo {
  bool isCastling;                     // This move is castling
  Square rookFromSq = SQ_NONE;         // Rook source square
  Square rookToSq = SQ_NONE;           // Rook destination square
};

}  // namespace LibreChess

#endif  // LIBRECHESS_TYPES_H
