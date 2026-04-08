---
applyTo: "lib/core/src/movegen.*"
description: "Legal move generation: per-piece, bulk, staged (LegalityContext), pin-aware filtering. Use when editing movegen.h or movegen.cpp."
---

# Move Generation (`lib/core/src/movegen.h/cpp`)

Stateless namespace — all context (board, turn, state) passed as decomposed parameters. Bitboard-based with pin-aware filtering.

## Public API

**Per-piece**: `getPossibleMoves(bb, mailbox, sq, state, moves)` — legal moves for one piece
**Bulk**: `generateAllMoves(...)`, `generateCaptures(...)` — self-contained (build own LegalityContext). Overloaded for both `MoveList` and `QSMoveList`.
**Staged** (reuse context): `buildLegalityContext(bb, color, kingSq) → LegalityContext`, then `generateCaptures(bb, mailbox, color, state, ctx, moves)` + `generateQuiets(bb, mailbox, color, state, ctx, moves)` — avoids double pin/check computation in staged move pickers.
**Append-mode**: `generateQuietsAppend(bb, mailbox, color, state, ctx, moves)` — appends quiet moves to existing `MoveList` without clearing (used by `MovePicker::initQuiets()`).
**Validation**: `isValidMove(bb, mailbox, from, to, state)`, overload with `kingSq` — builds `LegalityContext` and delegates to `filterPieceMoves` with early-exit handler
**Queries**: `hasAnyLegalMove(...)`, `hasLegalEnPassantCapture(...)`

## Key Structs

- `PinData` — up to 8 pin rays (`pinned` bitboard, `pinRay[8]`, `pinnedSq[8]`, `count`)
- `LegalityContext` — `kingSq`, `checkMask`, `pinData`, `checkerCount`
- `FilterMode` — `ALL`, `CAPTURES_PROMOS`, `QUIETS`

## Move Generation Architecture

1. **LegalityContext computed once** — checker info + pin data + check mask per call
2. **Non-king/non-EP moves** filtered via `pinRayFor(pinData, sq) & checkMask` applied at the bitboard level — illegal targets never enumerated
3. **King moves and EP captures** still use copy-make (`leavesInCheck`) for correctness
4. **`filterPieceMoves()` template** centralizes ALL per-piece legality filtering as a direct-emit pipeline — generates attack bitboards inline, masks with legalMask before iterating bits, and emits legal moves directly to the handler. No intermediate `MoveList` buffer.
5. **Both `enumerateLegalMoves()` and `getPossibleMoves()`** delegate to `filterPieceMoves()`
6. **`isValidMove()`** builds a `LegalityContext` and calls `filterPieceMoves` with an early-exit handler, checking if the target square appears among legal moves

## File-Local Helpers

- `pinRayFor(pinData, sq)` — ray mask for pinned piece
- `computePinData(bb, kingSq, color)` — compute pin rays
- `leavesInCheck(bb, mailbox, from, to, kingSq, movingColor)` — copy-make legality check. For non-castling king moves, uses `isSquareUnderAttackOcc` with synthetic occupancy to avoid BitboardSet copy. For castling and EP, copies `BitboardSet` (~120 bytes) and applies move on copy.
- `filterPieceMoves<Handler>(...)` — direct-emit template: generates attack bitboards per piece type inline, applies `legalMask` (pin-ray & check-mask) at the bitboard level before enumerating targets, emits legal moves directly to handler. King moves use `leavesInCheck` per target; castling is inlined (at most 2 candidates). No intermediate `MoveList` buffer — ~24% faster than the previous generate-then-filter approach.

## Design Notes

- **Stateless** — decomposed params `(BitboardSet&, Piece[], PositionState&)` prevent circular dependency with `position.h`.
- **Copy-make for legality** — `leavesInCheck()` copies `BitboardSet` (~120 bytes), applies move on copy, checks king. Lightweight flat struct copy. Used for castling and en passant moves.
- **King-move fast path** — non-castling king moves bypass BitboardSet copy: computes `occ = (bb.occupied ^ squareBB(from)) | squareBB(to)` and calls `isSquareUnderAttackOcc(bb, to, color, occ)`. Avoids ~120-byte copy per king move. Castling still uses the copy-make path (must verify rook transit squares). The delta check `delta != 2 && delta != -2` distinguishes castling from normal king moves without `std::abs`.
- **Direct-emit legal generation** — `filterPieceMoves` generates attack bitboards per piece type and masks with `legalMask` at the bitboard level before iterating. For sliding/leaper pieces: `atk &= ~friendly; atk &= legalMask;` then iterate bits. For pawns: push targets checked against `legalMask` individually; capture targets masked as `& enemy & legalMask`. Eliminates the intermediate `MoveList` buffer (~654 bytes stack per piece) and skips Move construction for targets filtered out by the legal mask.
- **Promotions** — emit 4 `Move` variants per target square (one per piece type: N, B, R, Q).

## Testing

Mirror test file: `test/test_core/test_movegen.cpp` (suite: `test_core`). When changing move generation, update tests in the same change. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — decomposed-params pattern |
| `attacks.instructions.md` | Uses `attacks::knight()`, `attacks::bishop()`, `attacks::rook()`, `attacks::PAWN[][]`, `attacks::KING[]` for move generation |
| `position.instructions.md` | `leavesInCheck()` copies `BitboardSet` for king/EP legality; `Position::make()` calls `isValidMove()` |
| `core-headers.instructions.md` | `Move`, `MoveList`, `BitboardSet`, `PositionState` |
| `testing.instructions.md` | Test architecture, helpers, and `test_movegen.cpp` group description |
