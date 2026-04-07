---
applyTo: "lib/core/src/evaluation.*"
description: "Tapered evaluation: material, PSTs, pawn structure, positional terms, hash tables. Use when editing evaluation.h or evaluation.cpp."
---

# Evaluation (`lib/core/src/evaluation.h/cpp`)

Tapered evaluation returning centipawns (`int`), white-relative. Interpolates MG and EG scores by game phase.

## Public API

**Main overloads**:
- `evaluatePosition(bb)` — full computation
- `evaluatePosition(bb, mgMatPST, egMatPST, pawnHash)` — with pre-computed material+PST
- `evaluatePosition(bb, mgMatPST, egMatPST, phase, pawnHash)` — with pre-computed phase (hot path from search)

**Material + PST**:
- `PSQTPair pieceSquareMGEG(pieceIdx, sq)` — single lookup from flat `PSQT_MG/EG[12][64]`
- `PSQTPair computeMaterialPST(bb)` — full board scan, returns MG+EG
- `computeMaterial(bb)` — white-relative material balance
- `materialValue(PieceType)` — single piece centipawn value (king = 20000 sentinel)
- `computeGamePhase(bb)` — N=1, B=1, R=2, Q=4; max 24
- `invalidatePSQT()` — force PSQT rebuild (tuning builds only)

**Hash tables** (1024 entries × 8B = 8 KiB each, always-replace):
- `PawnHashTable` — caches pawn structure MG/EG keyed by `computePawnHash()`. ~95%+ hit rate.
- `EvalHashTable` — caches full evaluation keyed by position Zobrist hash.

## Evaluation Terms

| Category | Terms |
|----------|-------|
| Material | `MATERIAL[]` (MG), `MATERIAL_EG[]` (EG), pawn MG fixed at 100cp |
| PSTs | Per-piece-type MG/EG tables, pre-combined into `PSQT_MG/EG[12][64]` (material + PST + color sign) |
| Pawn structure | Passed (rank-based exponential `PASSED_RANK_BONUS_MG/EG[8]`), isolated, doubled, backward, connected passers, protected passer (MG only) |
| Passed pawn king dist | EG only, not pawn-hashed: own king proximity bonus, enemy king proximity penalty |
| Bishop pair | MG/EG bonus |
| Bad bishop | Penalty per own pawn on same color complex (MG/EG) |
| Rook on open/semi-open | MG/EG split |
| Rook on 7th rank | Enemy king on back rank or enemy pawns on starting rank |
| Rook behind passer | Tarrasch Rule, EG only |
| Mobility | MG/EG split weights per piece type, computed from `AttackInfo` |
| King safety | Pawn shield rank-indexed (`SHIELD_ADV_RANK3`, `SHIELD_ADV_RANK4PLUS`) |
| King danger | Unified zone attack counting, nonlinear `KING_DANGER_TABLE[13]`, MG only |
| Knight outposts | MG/EG split |
| Space | MG only (`SPACE_BONUS_MG`) |
| Trapped pieces | Penalty for trapped bishops/rooks |
| Threats | Pawn→minor/rook/queen, minor→rook/queen, rook→queen (all MG only) |
| OCB scaling | Opposite-color bishop scaling via `OCB_SCALE_NUM/OCB_SCALE_DENOM`, EG only, phase ≤ `OCB_PHASE_THRESHOLD` |

## Key Patterns

- **Color-parameterized loops**: `for (int c = 0; c < 2; ++c)` with `sign = (c == 0) ? 1 : -1` and `c * 6` offset into `byPiece[]`. Applied across ALL bilateral eval terms. New eval functions MUST follow this pattern — never duplicate white/black code.
- **Flat PSQT lookup** — `PSQT_MG/EG[12][64]` combine material + PST + color sign. Lazy-built, `invalidatePSQT()` for tuning.
- **Pawn-structure masks** — lazy-initialized `pawnPassedMask[64]`, `pawnForwardMask[64]` (white-only, file-scoped; black derived via `byteSwap64(mask[sq^56])`). `adjacentFilesMask()` inline for isolated detection.
- **Passed pawns collected once** — `evaluateImpl` stores `passedPawns[2]` bitboards, shared by `evalPassedPawnKingDist()` and `evalRookBehindPasser()`.
- **Tempo bonus** — applied in search layer, not in eval.

## Testing

Mirror test files: `test/test_core/test_evaluation.cpp` + `test_eval_regression.cpp` (suite: `test_core`). When changing eval terms, update both the unit tests and the regression baselines. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `attacks.instructions.md` | Uses `AttackInfo` from `computeAll()` for mobility, king danger, threats, outposts, space; SEE delegates to `eval::materialValue()` |
| `zobrist.instructions.md` | `computePawnHash()` keys the `PawnHashTable` |
| `position.instructions.md` | Incremental PST/material/phase accumulators maintained by `Position::make()` |
| `trace.instructions.md` | `extractTrace()` must mirror `evaluatePosition()` exactly |
| `core-headers.instructions.md` | `BitboardSet`, `PieceType`, `Color`, `Square` |
| `testing.instructions.md` | Test architecture and `test_evaluation.cpp`/`test_eval_regression.cpp` group descriptions |
