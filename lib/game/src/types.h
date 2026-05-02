#ifndef LIBRECHESS_GAME_TYPES_H
#define LIBRECHESS_GAME_TYPES_H

// Game-level type definitions for the LibreChess game library.
//
// Contains the on-disk recording header and recording constants.
// Core chess types (Color, Piece, GameResult, etc.) are in
// lib/core/src/types.h and are available here transitively through piece.h.
//
// NOTE: This file shares the name "types.h" with lib/core/src/types.h.
// From within lib/game/src/, #include "types.h" resolves here (local).
// From src/ and test/, #include "types.h" resolves to core's types.h
// (first in PlatformIO's include path).  Game types are accessed through
// game.h, history.h, or storage.h — never via bare #include "types.h"
// from outside this library.

#include <cstdint>
#include <string>

#include "bitboard.h"  // rankOf, fileOf, makeSquare, Square
#include "piece.h"     // Core types (Color, Piece, GameResult, etc.)

namespace LibreChess {

// ---------------------------------------------------------------------------
// Coordinate bridge — LERF Square ↔ display row/col.
//
// Core operates with LERF Squares (a1=0, h8=63).  Firmware uses a
// display-oriented (row, col) system where row 0 = rank 8 and col 0 = a-file.
// The game layer is the sole bridge between these two systems.
//
//   row = 7 - rankOf(sq)       col = fileOf(sq)
//   sq  = makeSquare(7 - row, col)
// ---------------------------------------------------------------------------

/// Convert LERF Square to display row (0 = rank 8, 7 = rank 1).
inline constexpr int squareToRow(Square sq) { return 7 - rankOf(sq); }

/// Convert LERF Square to display column (0 = a-file, 7 = h-file).
inline constexpr int squareToCol(Square sq) { return fileOf(sq); }

/// Convert display (row, col) to LERF Square.
inline constexpr Square rowColToSquare(int row, int col) {
  return makeSquare(7 - row, col);
}

// ---------------------------------------------------------------------------
// Display-oriented coordinate helpers.
//
// row 0 = rank 8 (black back rank), col 0 = file 'a'.
// Used at display boundaries (game layer, firmware readouts).
// Core internals use rankOf/fileOf/makeSquare instead.
// ---------------------------------------------------------------------------

/// Display-rank character from display row (row 0 → '8', row 7 → '1').
inline constexpr char rankChar(int row) { return '1' + (7 - row); }

/// Square name from display (row, col) — e.g. (0, 0) → "a8".
inline std::string squareName(int row, int col) {
  return {static_cast<char>('a' + col), rankChar(row)};
}

// ---------------------------------------------------------------------------
// Recording types
// ---------------------------------------------------------------------------

/// Number of opaque metadata bytes in GameHeader.
/// Firmware defines semantic meaning (e.g. game mode, difficulty);
/// the library stores and returns them without interpretation.
static constexpr int GAME_META_SIZE = 3;

// Binary file header for recorded games (on-disk format).
#pragma pack(push, 1)
struct GameHeader {
  GameResult result;              // Game outcome
  uint8_t winnerColor;            // 'w', 'b', 'd' (draw), '?' (in-progress)
  uint8_t playerColor;            // Human's color ('w'/'b'), '?' if unset
  uint16_t moveCount;             // Number of 2-byte entries (incl. FEN markers)
  uint16_t fenEntryCnt;           // Number of FEN table entries
  uint16_t lastFenOffset;         // Byte offset of the last FEN entry within the FEN table
  uint32_t timestamp;             // Unix epoch (from NTP, 0 if unavailable)
  uint8_t meta[GAME_META_SIZE];   // Opaque firmware metadata
};
#pragma pack(pop)
static_assert(sizeof(GameHeader) == 16, "GameHeader must be 16 bytes");

// On-disk wire format is little-endian, matching the raw memcpy performed by
// the LittleFSStorage implementation in src/storage/littrefs.cpp and the
// `getUint16/32(offset, /*littleEndian=*/true)` calls in the web frontend
// (src/web/board.html). ESP32 and the native test host
// are both LE, so the reinterpret_cast-based I/O is a zero-cost identity.
// If this ever fires, either the firmware moved to a BE target or the wire
// format needs explicit serialization helpers.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "GameHeader wire format requires a little-endian host");
#endif

// Recording constants.
static constexpr uint16_t FEN_MARKER = 0xFFFF;
static constexpr int MAX_GAMES = 50;
static constexpr float MAX_USAGE_PERCENT = 0.80f;

}  // namespace LibreChess

#endif  // LIBRECHESS_GAME_TYPES_H
