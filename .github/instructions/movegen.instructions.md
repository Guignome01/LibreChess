---
applyTo: "lib/core/src/movegen.*"
description: "Legal move generation: per-piece, bulk, staged (LegalityContext), pin-aware filtering. Use when editing movegen.h or movegen.cpp."
---

# Move Generation (`lib/core/src/movegen.h/cpp`)

Stateless namespace — all context (board, turn, state) passed as decomposed parameters. Bitboard-based with pin-aware filtering.

## Public API

**Per-piece**: `getPossibleMoves(bb, mailbox, sq, state, moves)` — legal moves for one piece
**Bulk**: `generateAllMoves(...)`, `generateCaptures(...)` — self-contained (build own LegalityContext)
**Staged** (reuse context): `buildLegalityContext(bb, color, kingSq) → LegalityContext`, then `generateCaptures(bb, mailbox, color, state, ctx, moves)` + `generateQuiets(bb, mailbox, color, state, ctx, moves)` — avoids double pin/check computation in staged move pickers.
**Validation**: `isValidMove(bb, mailbox, from, to, state)`, overload with `kingSq`
**Queries**: `hasAnyLegalMove(...)`, `hasLegalEnPassantCapture(...)`

## Key Structs

- `PinData` — up to 8 pin rays (`pinned` bitboard, `pinRay[8]`, `pinnedSq[8]`, `count`)
- `LegalityContext` — `kingSq`, `checkMask`, `pinData`, `checkerCount`
- `FilterMode` — `ALL`, `CAPTURES_PROMOS`, `QUIETS`

## Move Generation Architecture

1. **LegalityContext computed once** — checker info + pin data + check mask per call
2. **Non-king/non-EP moves** filtered via `pinRayFor(pinData, sq) & checkMask` — no `leavesInCheck` call
3. **King moves and EP captures** still use copy-make (`leavesInCheck`) for correctness
4. **`filterPieceMoves()` template** centralizes ALL per-piece legality filtering — handles king moves (full `leavesInCheck`), EP captures, and standard pin/check mask filtering
5. **Both `enumerateLegalMoves()` and `getPossibleMoves()`** delegate to `filterPieceMoves()`

## File-Local Helpers

- `pinRayFor(pinData, sq)` — ray mask for pinned piece
- `computePinData(bb, kingSq, color)` — compute pin rays
- `filterPieceMoves<FilterMode>(...)` — template for legality filtering
- `addPieceMoves(attacks, sq, bb, color, moves)` — shared helper for non-king piece generation (filter friendly, tag captures, emit). Used by `addRookMoves`, `addBishopMoves`, `addQueenMoves`, `addKnightMoves` — each is a one-liner.

## Design Notes

- **Stateless** — decomposed params `(BitboardSet&, Piece[], PositionState&)` prevent circular dependency with `position.h`.
- **Copy-make for legality** — `leavesInCheck()` copies `BitboardSet` (~120 bytes), applies move on copy, checks king. Lightweight flat struct copy.
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
