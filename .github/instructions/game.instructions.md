---
applyTo: "lib/game/src/game.*"
description: "Game orchestrator: central entry point for firmware, composes Position + History, owns game lifecycle. Use when editing game.h or game.cpp."
---

# Game (`lib/game/src/game.h/cpp`)

Central game orchestrator — the ONLY entry point for firmware to access chess logic. Composes `Position` + `History`. Firmware must never include `Position`, `History`, `movegen`, `piece`, or `utils` directly.

## Public API

**Lifecycle**: `Game(storage, observer, logger)`, `newGame()`, `startNewGame(playerColor, meta)`, `endGame(result, winner)`, `discardRecording()`

**Moves** (dual overloads — Square-native primary, row/col for firmware):
- `makeMove(from, to, promo) → MoveResult` (Square-native)
- `makeMove(fromRow, fromCol, toRow, toCol, promo) → MoveResult` (display coords)
- `makeMove(string) → MoveResult` (auto-detect notation format)
- `loadFEN(fen) → bool`

**Undo/Redo**: `undoMove()`, `redoMove()`, `canUndo()`, `canRedo()`, `currentMoveIndex()`, `moveCount()`, `getHistory(out, maxMoves, format)`

**Resume**: `resumeGame()`, `hasActiveGame()`, `getActiveGameInfo(...)`

**Re-exported queries** (from Position): `bitboards()`, `mailbox()`, `getSquare()`, `sideToMove()`, `getCastlingRights()`, `positionState()`, `getFen()`, `getEvaluation()`, `getPossibleMoves()`, `isDraw()`, `boardToText()`, `forEachSquare(fn)`, `checkEnPassant(...)`, `checkCastling(...)`, `board()`

**Re-exported statics** (from piece/utils): `isEmptySquare()`, `pieceColor()`, `pieceType()`, `pieceToChar()`, `colorName()`, `squareName()`, `fileChar()`, `rankChar()`

**Notation statics**: `toCoordinate(row, col, ...)`, `parseCoordinate(str, ...)`

**Batching**: `beginBatch()` / `endBatch()` — suppress observer notifications

## Data Flow: makeMove

1. `Position::makeMove()` — validate, mutate board, detect game-end
2. Log move description via `ILogger`
3. Record in `History` (auto-persist if recording)
4. Check threefold repetition → auto-end
5. Log game-end or check/turn
6. Notify `IGameObserver` with FEN + evaluation
7. Auto-save recording if game ended

Steps 2–7 are atomic from the caller's perspective.

## Design Notes

- **Firmware abstraction boundary** — `Game` re-exports everything firmware needs. Display-coordinate helpers (`rankChar`, `squareName(row,col)`) live in `game/types.h`.
- **Dirty-flag caching** — FEN/eval cached, recomputed only when `fenDirty_`/`evalDirty_` set.
- **Composition over inheritance** — `Game` composes `Position` + `History`, no inheritance.
- **Nullable DI** — storage, observer, logger all pointer-injected. Logger uses `Log` proxy.
- **Undo clears game-over** — `undoMove()` re-opens finished games for web UI navigation.

## Testing

Mirror test file: `test/test_game/test_game.cpp` (suite: `test_game`). When changing Game orchestration, update tests in the same change. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game-library.instructions.md` | Parent library — shared conventions, data flow diagram |
| `position.instructions.md` | Composes `Position`, delegates all chess logic |
| `history.instructions.md` | Composes `History`, delegates undo/redo/recording |
| `game-headers.instructions.md` | Uses `IGameStorage`, `IGameObserver`, `GameHeader`, display-coord helpers |
| `notation.instructions.md` | `getHistory()` uses notation for SAN/LAN output |
| `evaluation.instructions.md` | `getEvaluation()` delegates to `eval::evaluatePosition()` |
| `testing.instructions.md` | Test architecture, helpers, and `test_game.cpp` group description |
