---
applyTo: "lib/game/src/types.h, lib/game/src/storage.h, lib/game/src/observer.h"
description: "Game-layer types, storage interface, observer interface. Use when editing game types.h, storage.h, or observer.h."
---

# Game Headers (`lib/game/src/`)

## `types.h` — Game-Layer Types

**Coordinate bridge** (LERF ↔ display row/col):
- `squareToRow(sq)` → `7 - rank` (row 0 = rank 8)
- `squareToCol(sq)` → file
- `rowColToSquare(row, col) → Square`
- `rankChar(row)`, `squareName(row, col)`

**Game recording**:
- `GameHeader` — 16 bytes packed: `result`, `winnerColor`, `playerColor`, `moveCount`, `fenEntryCnt`, `lastFenOffset`, `timestamp`, `meta[GAME_META_SIZE]`
- `meta[]` is opaque — firmware defines semantic overlay (`GameModeId`, engineId, difficulty) in `game_mode.h`; library stores bytes without interpretation
- Constants: `GAME_META_SIZE = 3`, `FEN_MARKER = 0xFFFF`, `MAX_GAMES = 50`, `MAX_USAGE_PERCENT = 0.80f`

## `storage.h` — `IGameStorage` Interface

Persistence DI interface. Implemented by `LittleFSStorage` in firmware.
- Lifecycle: `initialize()`, `beginGame(header)`, `finalizeGame(header)`, `discardGame()`, `hasActiveGame()`
- Moves: `appendMoveData(data, len)`, `truncateMoveData(offset)`
- FEN: `appendFenEntry(fen) → size_t`, `readFenAt(offset, fen) → bool`
- Header: `updateHeader(header)`, `readHeader(header) → bool`
- Query: `readMoveData(data) → bool`
- Management: `deleteGame(id) → bool`, `enforceStorageLimits()`

## `observer.h` — `IGameObserver` Interface

Board-state notification. Implemented by `WiFiManagerESP32` in firmware.
- `onBoardStateChanged(fen, evaluation)` — called on makeMove, loadFEN, newGame, endGame. Evaluation in centipawns (side-to-move relative).

## Testing

Game header types are tested via `test/test_game/test_game.cpp` and `test_history_persistence.cpp` (suite: `test_game`). `IGameStorage`/`IGameObserver` are tested through mock implementations. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game-library.instructions.md` | Parent library — shared conventions |
| `game.instructions.md` | `Game` uses `IGameStorage`/`IGameObserver` via nullable DI |
| `game-mode.instructions.md` | Firmware defines `meta[]` semantic overlay (`GameModeId`, engineId, difficulty) |
| `wifi-manager.instructions.md` | Implements `IGameObserver` |
| `core-headers.instructions.md` | `Square`, `Color`, `GameResult` types from `core/types.h` |
| `testing.instructions.md` | Test architecture and game test group descriptions |
