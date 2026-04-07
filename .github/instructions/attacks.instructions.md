---
applyTo: "lib/core/src/attacks.*"
description: "Precomputed attack tables, slider functions (HQ), x-ray attacks, AttackInfo, SEE. Use when editing attacks.h or attacks.cpp."
---

# Attacks (`lib/core/src/attacks.h/cpp`)

Precomputed leaper tables and O(1) slider functions. Stateless namespace (~3 KiB tables, initialized once via `init()`).

## Public API

**Init**: `init()` — initialize leaper tables (idempotent)

**Leaper tables**: `KNIGHT[64]`, `KING[64]`, `PAWN[2][64]` (~2.5 KiB)

**Sliders** (O(1)):
- `rook(sq, occ)` — first-rank lookup table (512-byte `FIRST_RANK_ATTACKS[8][64]`) + Hyperbola Quintessence
- `bishop(sq, occ)` — HQ on diagonal masks (`DIAG[15]`, `ANTI_DIAG[15]`)
- `queen(sq, occ)` — rook + bishop

**X-ray**: `xrayRook(occ, friendly, sq)`, `xrayBishop(occ, friendly, sq)`
**Geometry**: `between(s1, s2)` — squares strictly between, exclusive

**Attack detection**:
- `isSquareUnderAttack(bb, sq, defendingColor)` — early-exit: leapers first, sliders second
- `attackersOfSquare(bb, sq, attackingColor)` — bitboard of all attackers

**Attack maps**:
- `AttackInfo` struct — `byPiece[2][7]` (indexed `[raw(Color)][raw(PieceType)]`), `byColor[2]`, `allAttacks`
- `computeAll(bb) → AttackInfo` — one pass: pawns via bulk shift, leapers via table, sliders via HQ. Called by `evaluateImpl()` for mobility, king danger, threats, outposts, space.

**Static Exchange Evaluation**:
- `see(bb, mailbox, move) → int` — swap algorithm, least-valuable-attacker iteration. Delegates to `eval::materialValue()` for piece values (king sentinel = 20000).

## Design Notes

- **Hyperbola Quintessence** — branchless `o^(o-2r)` subtraction trick with byte-swap for negative rays. [CPW — HQ](https://www.chessprogramming.org/Hyperbola_Quintessence)
- **First-rank table** — 512-byte `FIRST_RANK_ATTACKS[8][64]` indexed by file + 6-bit inner occupancy for rank attacks.
- **Diagonal masks** — `DIAG[15]`, `ANTI_DIAG[15]` indexed by `rank-file+7` and `rank+file`, precomputed at startup (~240 bytes).

## Testing

Mirror test file: `test/test_core/test_attacks.cpp` (suite: `test_core`). When changing attack tables or SEE, update tests in the same change. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `evaluation.instructions.md` | `AttackInfo`/`computeAll()` consumed by eval for mobility, king danger, threats, outposts, space; SEE delegates to `eval::materialValue()` |
| `movegen.instructions.md` | Move generation uses leaper tables and slider functions |
| `core-headers.instructions.md` | `BitboardSet`, `Square`, `PieceType` |
| `testing.instructions.md` | Test architecture and `test_attacks.cpp` group description |
