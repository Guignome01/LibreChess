---
applyTo: "lib/game/src/game.*"
description: "Game orchestrator: central entry point for firmware, composes Position + History, owns game lifecycle. Use when editing game.h or game.cpp."
---

# Game (`lib/game/src/game.h/cpp`)

Central game orchestrator — the ONLY entry point for firmware to access chess logic. Composes `Position` + `History`. Firmware must never include `Position`, `History`, `movegen`, `piece`, or `utils` directly.

## Public API

**Lifecycle**: `Game(storage, observer, logger)`, `~Game()`, `newGame()`, `startNewGame(playerColor, meta)`, `endGame(result, winner)`, `discardRecording()`

**Search** (optional — initialized for bot mode via `initSearch`, skipped for player-only games):
- `initSearch(ttSize)` — allocates TT, PawnHash, EvalHash, SearchState on heap (idempotent)
- `calculateMove(limits) → SearchResult` — copies the current `Position` and runs `findBestMove()` on that snapshot, preserving the live board while search make/unmake recursion runs
- `rankCandidateTargets(fromRow, fromCol, targets, timeLimitMs, scores, maxDepth=MAX_PLY) → bool` — builds legal root candidates from a lifted source square, runs one root-filtered search on a snapshot, and returns per-destination scores from the current side-to-move perspective
- `setTimeFunc(fn)` — platform time abstraction: firmware passes `millis()`, CLI passes `nativeMillis()`
- `setExternalStop(flag)` — wire an external `std::atomic<bool>*` for cooperative cancellation
- `searchInitialized()` / `searchHashTablesReady()` / `searchHashTableAllocationFailed()` — firmware-visible diagnostics for heap-pressure handling after `initSearch()`

**Moves** (dual overloads — Square-native primary, row/col for firmware):
- `makeMove(from, to, promo) → MoveResult` (Square-native)
- `makeMove(fromRow, fromCol, toRow, toCol, promo) → MoveResult` (display coords)
- `makeMove(string) → MoveResult` (auto-detect notation format)
- `loadFEN(fen) → bool`

**Undo/Redo**: `undoMove()`, `redoMove()`, `canUndo()`, `canRedo()`, `currentMoveIndex()`, `moveCount()`, `getHistory(out, maxMoves, format)`

**Resume**: `resumeGame()`, `hasActiveGame()`, `getActiveGameInfo(...)`

**Re-exported queries** (from Position): `bitboards()`, `mailbox()`, `getSquare()`, `sideToMove()`, `getCastlingRights()`, `positionState()`, `getFen()`, `getEvaluation()`, `getPossibleMoves()`, `isDraw()`, `boardToText()`, `forEachSquare(fn)`, `checkEnPassant(...)`, `checkCastling(...)`, `board()`

**Candidate scoring/ranking**: `scoreCandidateMove(fromRow, fromCol, toRow, toCol, scoreOut, promo)` remains the lightweight one-ply static fallback. `rankCandidateTargets(...)` is the searched path for BEST_MOVE assistance: it filters root moves to the lifted piece's requested targets, uses the caller's time/depth budget, and leaves the live board/history/cache state unchanged.

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
- **Dirty-flag caching** — FEN/eval cached, recomputed only when `fenDirty_`/`evalDirty_` set. Any mutation path that changes `board_`, including `resumeGame()` replay, must call `invalidateCache()` before notifying observers or returning FEN to engines/UI.
- **Composition over inheritance** — `Game` composes `Position` + `History`, no inheritance.
- **Engine composition** — `Game` optionally composes an `Engine*` (heap-allocated by `initSearch()` with `new(std::nothrow)`, deleted in destructor).  `calculateMove()` snapshots `board_` and delegates the copy to the engine, so background search never mutates the live game position.  `setTimeFunc`/`setExternalStop` delegate directly. Search diagnostics expose initialization and hash-allocation status to firmware. `newGame()` calls `engine_->clearState()` when initialized.  Player-only games never allocate an engine.
- **Candidate scoring snapshot** — `scoreCandidateMove()` is a lightweight, synchronous query for UI assistance. It copies `board_`, applies the candidate through `Position::makeMove()`, handles terminal draw/mate results, and flips `eval::evaluatePosition()` into the moving side's perspective. It must not notify observers or alter dirty caches.
- **Candidate search snapshot** — `rankCandidateTargets()` uses `board_.getPossibleMoves(from)` to preserve legal flags/promotions, filters to requested display-coordinate targets, then calls the core search through `Engine` with root score output under the caller's time/depth budget. Promotion alternatives for the same target collapse to the best score for that target. It must not notify observers, append history, or alter dirty caches.
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
| `engine.instructions.md` | Engine facade composed by Game for bot-mode search |
| `evaluation.instructions.md` | `getEvaluation()` delegates to `eval::evaluatePosition()` |
| `testing.instructions.md` | Test architecture, helpers, and `test_game.cpp` group description |
