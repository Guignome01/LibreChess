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

#include "piece.h"  // Core types (Color, Piece, GameResult, etc.)

namespace LibreChess {

// ---------------------------------------------------------------------------
// Recording types
// ---------------------------------------------------------------------------

/// Number of opaque metadata bytes in GameHeader.
/// Firmware defines semantic meaning (e.g. game mode, difficulty);
/// the library stores and returns them without interpretation.
static constexpr int GAME_META_SIZE = 2;

// Binary file header for recorded games (on-disk format).
#pragma pack(push, 1)
struct GameHeader {
  uint8_t version;                // Format version (currently 1)
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

// Recording constants.
static constexpr uint8_t FORMAT_VERSION = 1;
static constexpr uint16_t FEN_MARKER = 0xFFFF;
static constexpr int MAX_GAMES = 50;
static constexpr float MAX_USAGE_PERCENT = 0.80f;

}  // namespace LibreChess

#endif  // LIBRECHESS_GAME_TYPES_H
