---
applyTo: "lib/core/src/evaluation.*"
description: "Tapered evaluation: material, PSTs, pawn structure, positional terms, hash tables. Use when editing evaluation.h or evaluation.cpp."
---

# Evaluation (`lib/core/src/evaluation.h/cpp`)

Tapered evaluation returning centipawns (`int`), white-relative. Interpolates MG and EG scores by game phase.

## Public API

**Main overloads**:
- `evaluatePosition(bb, pawnHash)` — full computation (material+PST from scratch)
- `evaluatePosition(bb, mgMatPST, egMatPST, phase, pawnHash)` — with pre-computed material+PST + phase (hot path from search)

**Extracted parameters** — all tunable evaluation constants (material values, PST tables, pawn structure bonuses, king safety weights, mobility tables, space terms) live in `eval_params.h`. The `EVAL_CONST`/`EVAL_FIXED`/`PST_ELEM`/`MAT_ELEM` macros are also defined there.

**Material + PST**:
- `PSQTPair pieceSquareMGEG(pieceIdx, sq)` — single lookup from flat `PSQT_MG/EG[12][64]` (production) or direct computation from raw arrays (TUNING)
- `PSQTPair computeMaterialPST(bb)` — full board scan via `pieceSquareMGEG`, returns MG+EG
- `computeMaterial(bb)` — white-relative material balance
- `materialValue(PieceType)` — single piece centipawn value (king = 20000 sentinel)
- `computeGamePhase(bb)` — N=1, B=1, R=2, Q=4; max 24
- `chebyshevDist(a, b)` — Chebyshev (king) distance between two LERF squares

**Tuning isolation** — evaluation.h has zero `#ifdef TUNING`. evaluation.cpp has two minimal `#ifdef TUNING` regions:
1. **Data definitions** — TUNING: PST pointer tables for direct computation from mutable params. Production: `static constexpr PSQT_MG/EG[12][64]`.
2. **pieceSquareMGEG** — TUNING: computes `MATERIAL[type] + PST[type][sq]` on each call (no caching). Production: reads constexpr PSQT.

All tuning metadata (descriptors, param externs, accessor API) lives in `lib/core/src/trace.h/cpp` (`#ifdef TUNING` — compiles to nothing in production). EVAL_FIXED extern linkage declarations live in `eval_params.h`.

**Hash tables** (both inherit `HashTableBase` from `hash_table.h`):
- `PawnHashTable` — caches pawn structure MG/EG + `passedPawns[2]` bitboards, keyed by `computePawnHash()`. Default 256 entries × 24B = 6 KiB. ~92%+ hit rate. Passed pawn bitboards are cached to avoid re-scanning pawns for king distance and rook-behind-passer evaluation.
- `EvalHashTable` — caches full evaluation keyed by position Zobrist hash. Default 1024 entries × 4B = 4 KiB. Compact `EvalEntry` layout: `uint16_t key` + `int16_t score` = 4B. The 16-bit key combined with the index mask provides ~26 effective bits of collision resistance — sufficient for a soft cache.

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
| Mobility | Nonlinear per-piece tables indexed by safe attack count (excludes friendly + enemy pawn attacks), computed from `AttackInfo` |
| King safety | Pawn shield (rank-indexed: `SHIELD_ADV_RANK3`, `SHIELD_ADV_RANK4PLUS`, `SHIELD_OPEN_FILE`) + pawn storm (`PAWN_STORM[8]` rank-indexed enemy advance penalty). Both MG only. |
| King danger | Unified zone attack counting, nonlinear `KING_DANGER_TABLE[13]`, MG only. Scaled by opponent material fraction (`oppPhase / STARTING_PHASE_ONE_SIDE`) to prevent overvaluation in endgames. |
| Knight outposts | MG/EG split |
| Space | MG only (`SPACE_BONUS_MG`) |
| Trapped pieces | Penalty for trapped bishops/rooks |

| OCB scaling | Opposite-color bishop scaling (3/4), EG only, phase ≤ 6. Constants are internal to evaluation.cpp |

## Key Patterns

- **Color-parameterized loops**: `for (int c = 0; c < 2; ++c)` with file-scope `SIDE_SIGN[c]` and `COLORS[c]` constexpr lookup tables (anonymous namespace).  `SIDE_SIGN[] = {1, -1}` maps color index to white-relative sign; `COLORS[] = {Color::WHITE, Color::BLACK}` maps index to enum.  All bilateral eval terms use these — never duplicate white/black code or use raw ternaries for sign/color.
- **Flat PSQT lookup** — `PSQT_MG/EG[12][64]` combine material + PST + color sign. Production: `static constexpr` arrays in rodata via macro-based aggregate initializers (`PSQT_R64_`, `PSQT_N64_`); `pieceSquareMGEG()` reads directly. TUNING: no cached tables — `pieceSquareMGEG()` computes from mutable eval params via `PST_MG_PTRS/PST_EG_PTRS[6]` pointer tables, avoiding cache invalidation.
- **Pawn-structure masks** — `static constexpr PawnMasks` struct in anonymous namespace, containing `passed[64]` and `forward[64]` arrays (white-only, placed in .rodata; black derived via `byteSwap64(mask[sq^56])`). `adjacentFilesMask()` inline for isolated detection (also reused by `isOutpostSquare`).
- **Passed pawns cached in pawn hash** — `PawnEntry` stores `Bitboard passedPawns[2]` alongside MG/EG scores. `evalPawnStructure` builds the bitboards during its pawn loop and stores them in the hash. On pawn hash hit, bitboards are retrieved without re-scanning. Shared by `evalPassedPawnKingDist()` and `evalRookBehindPasser()`.
- **Trapped pieces — 2D color-indexed trap arrays** — `BISHOP_TRAPS[2][4]` and `ROOK_TRAPS[2][2]` store per-color trap patterns. A single color loop handles both colors, using `pieceIndex(color, type)` for piece lookups. Bishop traps check own bishops blocked by enemy pawns; rook traps check own rooks hemmed by own king.
- **King danger sign convention** — `evalKingDanger` uses the standard `SIDE_SIGN[c]` with `mgScore -= sign * penalty` (subtraction makes the penalty semantics explicit). Other eval terms use `mgScore += sign * bonus`.
- **King safety architecture** — `evalKingSafety` computes both pawn shield and pawn storm in a single function, adding both for white and subtracting both for black. Shield evaluates defensive pawn positions near own king; storm evaluates enemy pawn advances toward own king. Both use the same shield-file selection via `selectShieldFiles()` (files a-c for kingside, f-h for queenside, skip center kings). Storm finds the most advanced enemy pawn per file (`lsb` for white defending, `msb` for black defending) and indexes `PAWN_STORM[relRank]`.
- **King danger scaling** — `evalKingDanger` multiplies the danger table output by `oppPhase / STARTING_PHASE_ONE_SIDE` (opponent's non-pawn material phase weight / 12). This prevents overvaluation of broken pawn shields and zone attacks when the opponent lacks sufficient material to exploit them. CPW: "Even TSCP uses the pawn shield and pawn storm score, scaled by the opponent's material."
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
