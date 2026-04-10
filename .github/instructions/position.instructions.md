---
applyTo: "lib/core/src/position.*"
description: "Position class: board representation (bitboard+mailbox), move execution, game-end detection, incremental tracking. Use when editing position.h or position.cpp."
---

# Position (`lib/core/src/position.h/cpp`)

Board representation and position-level chess logic — a pure position container with no lifecycle state.

## Internal State

| # | Field | Type | Role |
|---|-------|------|------|
| 1 | `bb_` | `BitboardSet` | 12 piece + 2 color + occupancy bitboards |
| 2 | `mailbox_[64]` | `Piece[]` | O(1) piece identity by square, lockstep with `bb_` |
| 3 | `currentTurn_` | `Color` | Side to move |
| 4 | `state_` | `PositionState` | Castling rights, EP target, halfmove/fullmove clocks |
| 5 | `kingSquare_[2]` | `Square[]` | O(1) king location, public via `kingSq(c)` |
| 6 | `hash_` | `uint64_t` | Incremental Zobrist hash (XOR deltas in `make()`) |
| 7 | `hashHistory_` | `HashHistory` | Past position hashes for repetition detection |
| 8 | `mgPST_`, `egPST_`, `material_` | `int16_t` | White-relative MG/EG/material accumulators |
| 9 | `epIsLegal_` | `bool` | Cached `hasLegalEnPassantCapture()` result |
| 10 | `phase_` | `int8_t` | Game phase (N=1, B=1, R=2, Q=4; max 24) |
| 11 | `undoCache_` | `UndoCache` | 1-deep cache for O(1) `reverseMove()` |

## Public API

**Lifecycle**: `Position()`, `newGame()`, `loadFEN(fen) → bool`

**Game-layer moves** (with validation + game-end detection):
- `makeMove(from, to, promo) → MoveResult`
- `reverseMove(MoveEntry&)` — undo via `UndoCache` (hash-validated O(1), recompute on miss)
- `applyMoveEntry(MoveEntry&) → MoveResult` — replay

**Search-layer moves** (raw, no validation):
- `make(Move) → UndoInfo`, `unmake(Move, UndoInfo)`
- `makeNullMove() → UndoInfo`, `unmakeNullMove(UndoInfo)`

**Static game-end detection**: `isCheck`, `isCheckmate`, `isStalemate`, `isInsufficientMaterial`, `isThreefoldRepetition`, `isFiftyMoveRule`, `isDraw`, `isGameOver` — all take decomposed params `(bb, mailbox, state, ...)`, not `Position&`.

**Queries**: `getSquare(sq)`, `sideToMove()`, `kingSq(c)`, `bitboards()`, `mailbox()`, `hash()`, `isRepetition()`, `mgPST()`, `egPST()`, `material()`, `phase()`, `getFen()`, `boardToText()`

**Delegated wrappers**: `getPossibleMoves(sq, moves)`, `inCheck()`, `isCheckmate()`, `isDraw()`, `checkEnPassant(from, to)`, `checkCastling(from, to)`

## Design Notes

- **No lifecycle state** — `gameOver_`, `gameResult_`, `winnerColor_` live in `Game`. Position is a replayable container.
- **Dual representation** (bitboard + mailbox) — bitboards for set operations, mailbox for O(1) piece identity. LERF mapping (a1=0, h8=63). [CPW — Bitboards](https://www.chessprogramming.org/Bitboards)
- **Decomposed parameters** — static methods accept `(BitboardSet&, Piece[], PositionState&)` to prevent circular headers (`position.h` ↔ `movegen.h`).
- **Incremental accumulators** — `make()` delegates to `updateAccumulators()` for capture/movement/castling-rook/promotion/phase deltas. Decomposed into `removeCapture()`, `moveCastlingRook()`, `applyPromotion()`. `unmake()` uses `unmakeCastlingRook()`.
- **`makeMove()` decomposition** — flag inference via `buildMoveFlags()`, result via `buildMoveResult()`, outcome via `detectGameEnd()`. Single code path for game and search.
- **Undo state helpers** — `saveUndoState()` and `restoreFromUndo()` factor out the 7-field save/restore pattern shared by `make()`/`makeNullMove()` and `unmake()`/`unmakeNullMove()`.
- **Castling rook square derivation** — the `rank + kingSide → rookFrom/rookTo` pattern appears in `moveCastlingRook()`, `unmakeCastlingRook()`, and `updateAccumulators()`. Kept inline at each site (4 lines) to avoid a struct return in the hot path.
- **1-deep UndoCache** — stores `Move` + `UndoInfo` + post-hash. Hash-validated O(1) hot path for `reverseMove()`; cold path falls back to manual reversal + `recomputeDerived()`. [CPW — Copy-Make](https://www.chessprogramming.org/Copy-Make)
- **EP legality caching** — `epIsLegal_` avoids redundant movegen in Zobrist make/unmake hot path.
- **Twofold vs threefold repetition** — a file-local `hasRepeated(hashes, halfmoveClock, minCount)` function is the single counting loop (walks same-side history bounded by `halfmoveClock`, early-exits at threshold). `isRepetition()` calls it with `minCount=2` (search: position seen before → draw is available). `isThreefoldRepetition(hashes, halfmoveClock)` calls it with `minCount=3` (FIDE game-end, used by `isDraw()`/`isGameOver()`). The `halfmoveClock` bound prevents scanning across irreversible moves and avoids false matches from stale entries left by the search’s make/unmake cycle. [CPW — Repetitions](https://www.chessprogramming.org/Repetitions)
- **`recordPosition()` sliding window** — appends the current hash, and when the array is full, compacts by keeping only the last `halfmoveClock + 1` entries (the window reachable by `hasRepeated()`). This prevents silent drops in long games (265+ half-moves) that would break repetition detection. After compaction, `make()` and `makeNullMove()` detect the stale `UndoInfo.historyCount` (saved pre-compaction) and adjust it to `hashHistory_.count - 1`, ensuring `unmake()` restores a consistent count. Compaction only triggers during game-level move replay (not during search), since after compaction the array has `~halfmoveClock + 1` entries and search adds at most `MAX_PLY = 48`, well within `MAX_SIZE = 256`. `loadFEN()` still resets `count = 0` (no undo path). [CPW — Repetitions](https://www.chessprogramming.org/Repetitions)

## Testing

Mirror test file: `test/test_core/test_position.cpp` (suite: `test_core`). When changing Position logic, update or add tests in the same change. See `testing.instructions.md` for test group details and helpers.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions, decomposed-params pattern |
| `movegen.instructions.md` | `make()` calls `isValidMove()`, game-end detection calls `hasAnyLegalMove()`, EP legality calls `hasLegalEnPassantCapture()` |
| `fen.instructions.md` | `loadFEN()` delegates to `fen::fenToBoard`/`validateFEN` |
| `zobrist.instructions.md` | Incremental hash in `make()`, `computeHash()` for debug verification |
| `attacks.instructions.md` | Check detection via `isSquareUnderAttack()` |
| `evaluation.instructions.md` | Incremental `mgPST_`/`egPST_`/`material_`/`phase_` tracking uses eval tables |
| `core-headers.instructions.md` | `BitboardSet`, `PositionState`, `Move`, `MoveResult`, `MoveEntry`, `HashHistory` |
| `testing.instructions.md` | Test architecture, helpers, and `test_position.cpp` group description |
