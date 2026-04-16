---
applyTo: "lib/core/src/trace.*"
description: "Trace extraction for offline tuning (#ifdef TUNING only). Use when editing trace.h or trace.cpp."
---

# Trace (`lib/core/src/trace.h/cpp`)

Tuning infrastructure for Texel's tuning method.  Lives in `lib/core/src/` alongside `evaluation.h` (like Stockfish keeps its tuning infrastructure with the engine).  Everything is guarded by `#ifdef TUNING` — production and test builds compile these files to nothing.  The standalone optimizer binary (`tools/tune/tune.cpp`) includes `trace.h` for all tuning needs.

Consolidated from 4 former files (`tools/tune/trace.h`, `trace.cpp`, `tune_registry.h`, `tune_registry.cpp`) into 2 files.

## Public API

- `TraceEntry` — `idx: int16_t`, `coeff: float` (one nonzero in sparse vector).
- `Trace` — `entries: vector<TraceEntry>`, `bias: float`, `hasOCB: bool` (opposite-color bishop flag), `add(idx, coeff)` (skips `idx < 0` or `coeff == 0`)
- `TrainingPosition` — `trace`, `result: double` (1.0 = white win, 0.5 = draw, 0.0 = black win)
- `extractTrace(bb) → Trace` — mirrors `evaluatePosition()`, recording per-parameter contributions
- `buildParamMap()` — initialize tuning registry and pointer/name index maps
- `findParam(name) → int` — lookup by name (utility, available for debugging/testing)
- `tuning::ScalarParam`, `PstDef` — descriptor structs for registry metadata
- `tuning::scalarParams(count)`, `pstDefs(count)` — static descriptor arrays (mobility excluded — loop-generated in `buildRegistry()`)
- `tuning::paramCount()`, `getName(i)`, `getValue(i)`, `setValue(i, v)`, `getDefault(i)` — registry accessor API
- Eval param `extern` declarations — all EVAL_CONST params re-declared for cross-TU access under TUNING.  EVAL_FIXED params are NOT declared here (internal linkage under `const` — inaccessible via extern).

## Pointer-based index lookup

`extractTrace()` references eval params directly by address via `pIdx(&EVAL_PARAM)`, which returns the registry index (or -1 if not registered). This eliminates:
- The former `TraceIndices` struct (40+ manual field declarations)
- The former `initTraceIndices()` function (100+ manual `findParam()` calls)

For array-based params (PST, mobility, king danger, passed rank, connected), the same pattern applies: `pIdx(&MOBILITY_KNIGHT_MG[nMob])`, `pIdx(PST_DATA[table] + sq)`, etc. Unregistered entries (e.g. mobility[0], passed rank[0]/[7]) return -1 and are safely skipped by `Trace::add`.  **Important**: every array index that the eval actually accesses with a nonzero value MUST be registered, otherwise the trace silently drops the contribution.

## Non-Tunable Parameters → Bias

When a parameter is deliberately excluded from the registry (e.g. pawn material pinned at 100), its contribution MUST be added to `Trace::bias`.  The material loop checks `pIdx()` result: if >= 0, the coefficient goes into a tunable trace entry; if -1, the fixed contribution (`diff * phaseWeight * paramValue`) goes into bias.  **Failing to add unregistered but active parameters to bias** causes trace/eval divergence proportional to the missing term's magnitude — for pawn material, this was ~100cp per pawn imbalance, causing 58% of validation positions to mismatch and the tuner to systematically distort all other parameters to compensate.

## Shared Compute Helpers

`extractTrace()` uses the same feature extraction helpers as `evaluatePosition()` (declared in `evaluation.h`):
- `computeThreats(bb, info, c)` → `ThreatCounts` — then emits coefficients for each threat type
- `computeMobility(bb, info, c)` → `MobilityCounts` — then emits mob table coefficients
- `computeKingDanger(bb, info, c)` → `KingDangerInfo` — then emits safe check coefficients
- `computeSpace(bb, c, openFiles)` → `SpaceInfo` — then emits space bonus coefficient
- `countOpenFiles(bb)`, `isOutpostSquare(sq, c, ...)` — shared outpost/space helpers
- `computeMaterial(bb)`, `centerManhattanDist(sq)`, `chebyshevDistance(a, b)` — used by mop-up section (threshold gate + distance coefficients)

This eliminates ~200 lines of duplicated intermediate computation.  Each helper computes intermediate values; the eval applies weights to get scores, while trace emits gradient coefficients referencing the same parameter addresses.

`KING_SAFETY_TABLE` entries are `EVAL_FIXED` (not tunable), so the full table penalty (base + all active safe checks) is linearized at the **combined** operating point.  `kingDangerScore()` accessor exposes the table without including `eval_params.h`.  The shared local slope `(table[tw+1] - table[tw-1]) / 2` gives each active check the same per-unit marginal coefficient, while `bias += totalPenalty - Σ slope * val_i` absorbs the constant offset.  Reconstruction is exact: `bias + Σ coeff_i * val_i = table[totalWeight]`.

## Data Flow: passedPawns Bitboards

The pawn structure trace builds `passedPawns[2]` (one per color) containing only non-doubled passed pawns (`passed && !doubled`), matching `evalPawnStructure()`.  Downstream sections that reference passed pawns — **king proximity** and **rook behind passer** — MUST iterate `passedPawns[]`, not re-scan all pawns with `isPassed()`.  Re-scanning would include doubled passed pawns, causing eval/trace divergence.

## OCB Handling

Opposite-color bishop scaling is stored as a boolean flag (`Trace.hasOCB`) rather than distributed per-coefficient.  The eval applies `score * 3/4` **after** tapering as a single integer operation; distributing `× 0.75` per-coefficient would create irreconcilable truncation mismatches.  `traceScore()` in `tune.cpp` applies `× 0.75` post-hoc to match the eval's operation order.  The gradient computation must include the `0.75` chain-rule factor for OCB positions.

## Testing

No dedicated test file — trace correctness verified indirectly via eval regression tests in `test/test_core/test_eval_regression.cpp`.  The tuner's `extractTrace()` mismatch detection (prints `MISMATCH pos N: eval=X trace=Y`) catches eval/trace drift.  Validation threshold is `> 4cp`, accepting small mismatches from integer truncation (eval tapering `(mg*phase + eg*(24-phase)) / 24`, OCB `score * 3/4`, space order-of-operations `SPACE_WEIGHT * bonus * w² / 16`).  Current status: 5000/5000 positions pass with 0 mismatches.  See `testing.instructions.md` for details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `evaluation.instructions.md` | `extractTrace()` must mirror `evaluatePosition()` exactly — every eval term needs a trace entry |
| `tuner.instructions.md` | Trace is the primary input to the offline tuner |
| `testing.instructions.md` | No dedicated test file — trace correctness verified indirectly via eval regression tests |
