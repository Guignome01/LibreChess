---
applyTo: "lib/game/src/history.*"
description: "Move history: in-memory log + persistent recording, cursor-based undo/redo. Use when editing history.h or history.cpp."
---

# History (`lib/game/src/history.h/cpp`)

In-memory move log + persistent recording. Cursor-based undo/redo.

## Public API

**Move log**: `addMove(entry)`, `undoMove() → const MoveEntry*`, `redoMove() → const MoveEntry*`, `canUndo()`, `canRedo()`, `currentMoveIndex()`, `moveCount()`, `empty()`, `getMove(idx)`, `lastMove()`, `clear()`

**Recording**: `setHeader(header)`, `snapshotPosition(fen)`, `save(result, winner)`, `discard()`, `isRecording()`

**Resume**: `hasActiveGame()`, `getActiveGameInfo(playerColor, meta)`, `replayInto(Position&)`, `replayFen()`

**Move encoding**: `encodeMove(from, to, promo) → uint16_t`, `decodeMove(encoded, from, to, promo)` — 2-byte binary format (bits 15..10 = from, 9..4 = to, 3..0 = promo code)

**Constants**: `MAX_MOVES = 300`

## Design Notes

- **Two concerns unified** — in-memory log and persistent recording share the move cursor; branch-on-undo must truncate both atomically.
- **Branch-on-undo wipes future** — undo N + new move permanently deletes all undone moves from memory and storage. Binary format doesn't support branching.
- **Header flushes every move** — `GameHeader` is written after every persisted half-move so resume metadata and `moveCount` match the latest move bytes at any reboot point.
- **Byte-wise replay decoding** — `replayInto()` must read the 2-byte move stream with `memcpy`/byte access rather than casting `uint8_t*` to `uint16_t*`; ESP32 can crash on unaligned typed loads when resuming from LittleFS buffers.
- **MoveEntry factory** — `MoveEntry::build()` encapsulates flag copying via `ME_FLAG_MASK`. Used by `Game::makeMove()` and `History::replayInto()`.

## Testing

Mirror test files: `test/test_game/test_history.cpp` + `test_history_persistence.cpp` (suite: `test_game`). When changing history or recording logic, update both test files. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game-library.instructions.md` | Parent library — shared conventions |
| `game-headers.instructions.md` | Uses `IGameStorage`, `GameHeader`, recording constants |
| `position.instructions.md` | `replayInto(Position&)` replays moves into a Position |
| `core-headers.instructions.md` | Uses `MoveEntry`, `Square` |
| `testing.instructions.md` | Test architecture and `test_history.cpp`/`test_history_persistence.cpp` group descriptions |
