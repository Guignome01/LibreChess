---
applyTo: "lib/core/src/piece.h, lib/core/src/utils.h, lib/core/src/bitboard.h, lib/core/src/types.h, lib/core/src/move.h, lib/core/src/logger.h, lib/core/src/hash_table.h"
description: "Core header-only files: piece, utils, bitboard, types, move, logger, hash_table. Foundational types and helpers shared across all core modules."
---

# Core Headers (`lib/core/src/`)

Header-only foundations shared by all core modules.

## `types.h` — Foundational Enums & Structs

- `Color` (WHITE=0, BLACK=1), `PieceType` (NONE..KING), `Piece` (bit-packed `Color<<3 | PieceType`)
- `GameResult`, `MoveFormat` (COORDINATE, SAN, LAN)
- `PositionState` — castlingRights, epSquare, halfmoveClock, fullmoveClock
- `Square` (`uint8_t`, SQ_NONE=255, LERF 0–63)
- `HashHistory` — `keys[128]`, `count` (threefold detection)
- `EnPassantInfo`, `CastlingInfo` — result structs for EP/castling detection
- `gameResultName(GameResult) → const char*`

## `move.h` — Move Representation

- `Move` — 3 bytes: `from`, `to`, `flags` (MOVE_CAPTURE, MOVE_EP, MOVE_CASTLING, MOVE_PROMOTION + 2-bit promo index). Constexpr accessors: `isCapture()`, `isEP()`, `isCastling()`, `isPromotion()`, `promoIndex()`, `isTactical()` (capture or promotion), `isNull()` (from==0 && to==0). Promotions emit 4 variants per target square.
- `MoveResult` — packed `uint8_t flags` (MR_VALID..MR_CHECK) + `epCapturedSq`, `promotedTo`, `gameResult`, `winnerColor`. Constexpr accessors.
- `MoveEntry` — history entry reusing MR_* flag bits (skipping MR_VALID). Factory: `MoveEntry::build()` copies flags via `result.flags & ME_FLAG_MASK`.
- `MoveListBase<N>` — template: `Move[N]` + `count`. Bounds-checked `add()` (silently drops when full). Fixed-size (no `std::vector`).
- `MoveList = MoveListBase<MAX_MOVES>` (218) — standard move buffer for main search and movegen.
- `QSMoveList = MoveListBase<QS_MAX_MOVES>` (128) — smaller buffer for quiescence search, saves ~540B/ply.
- `ScoredMove` — `Move` + `int16_t score`.

## `piece.h` — Piece Operations (all constexpr)

- Extraction: `pieceType(p)`, `pieceColor(p)`, `isEmpty(p)`
- Construction: `makePiece(c, t)`, flip: `~color`, `~piece`
- Color-derived: `pawnForward(c)` (+8/−8), `homeRank(c)`, `promotionRank(c)`, `pawnStartRank(c)` — use these, not inline ternaries
- FEN chars: `charToPiece(c)`, `pieceToChar(p)`, `charToPieceType(c)`, `pieceTypeToChar(t)`
- Indexing: `pieceIndex(Color, PieceType)` → 0–11, overloads for `Piece` and `char`. Sentinel: `PIECE_IDX_NONE = -1`. `isValidPieceIndex()`.
- `colorName(c)` → "White"/"Black"
- No material values — those live in `eval::materialValue()`.

## `utils.h` — Board Helpers (header-only)

- Castling: `castlingCharToBit(c)` (switch → bitmask), `hasCastlingRight(rights, color, kingSide)` via `BIT[2][2]`, `castlingRightsToString/FromString`, `updateCastlingRights(rights, from, to)` via `CASTLING_MASK[64]` lookup table
- Coordinates (LERF): `fileChar(f)`, `rankCharFromRank(r)`, `fileIndex(c)`, `rankIndexFromChar(c)`, `squareName(sq)`
- EP/Castling detection: `checkEnPassant(mailbox, from, to)`, `checkCastling(mailbox, from, to)`
- King: `resolveKingSquare(bb, color, kingSq)` — inline king finder
- Sizing: `roundDownPow2(n)` — for TT/hash table sizing
- Iteration: `forEachSquare(mailbox, fn)`, `forEachPiece(bb, mailbox, fn)`

## `bitboard.h` — Bitboard Types (header-only)

- `Bitboard = uint64_t`, `Square = uint8_t`
- Coordinate: `rankOf(sq)`, `fileOf(sq)`, `makeSquare(rank, file)`, `squareBB(sq)`
- Bit ops: `popcount`, `lsb`, `msb`, `popLsb`
- Masks: file/rank, `DARK_SQUARES`, `LIGHT_SQUARES`
- Shifts: compass rose (NORTH, SOUTH, EAST, WEST, diagonals)
- Named squares: `SQ_E1`, `SQ_G1`, etc.
- `BitboardSet` — 12 piece + 2 color + occupancy. Mutations: `setPiece`, `removePiece`, `movePiece`.

## `logger.h` — Logging Interface

- `ILogger` — `info(msg)`, `error(msg)`, formatted helpers `infof`/`errorf`
- `Log` — null-safe value-type proxy wrapping `ILogger*`. Eliminates manual null guards.

## `hash_table.h` — Generic Hash Table Base

- `HashTableBase<Entry>` template: `entries` pointer, `size`, `mask` (size-1 for fast index)
- `resize(bytes)` — rounds down to power-of-two entry count via `utils::roundDownPow2`, allocates with `new(std::nothrow)`
- `free()` — `delete[]` + null
- `clear()` — `memset` zero
- Inherited by `eval::PawnHashTable`, `eval::EvalHashTable` (core), and `search::TranspositionTable` (engine)
- DRY: eliminates duplicate resize/free/clear across all hash table types

## Testing

Core header types are tested across multiple files: `test_piece.cpp` (piece), `test_bitboard.cpp` (BitboardSet), `test_utils.cpp` (utils), `test_position.cpp` (Move, MoveList, HashHistory, MoveEntry). Suite: `test_core`. When changing types or helpers, update the corresponding test file. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions, fixed-size array rule |
| `position.instructions.md` | Primary consumer of `BitboardSet`, `PositionState`, `Move`, `MoveResult`, `HashHistory` |
| `movegen.instructions.md` | Uses `Move`, `MoveList`, `BitboardSet` |
| `evaluation.instructions.md` | Uses `PieceType`, `Color`, `Square` for PST lookup |
| `game-headers.instructions.md` | `game/types.h` bridges LERF ↔ row/col using `Square` from `core/types.h` |
| `testing.instructions.md` | Test architecture, helpers, and group descriptions for `test_piece.cpp`, `test_bitboard.cpp`, `test_utils.cpp` |
