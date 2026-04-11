---
applyTo: "lib/core/src/trace.*"
description: "Trace extraction for offline tuning (#ifdef TUNING only). Use when editing trace.h or trace.cpp."
---

# Trace (`lib/core/src/trace.h/cpp`)

Tuning infrastructure for Texel's tuning method.  Lives in `lib/core/src/` alongside `evaluation.h` (like Stockfish keeps its tuning infrastructure with the engine).  Everything is guarded by `#ifdef TUNING` — production and test builds compile these files to nothing.  The standalone optimizer binary (`tools/tune/tune.cpp`) includes `trace.h` for all tuning needs.

Consolidated from 4 former files (`tools/tune/trace.h`, `trace.cpp`, `tune_registry.h`, `tune_registry.cpp`) into 2 files.

## Public API

- `TraceEntry` — `idx: int16_t`, `coeff: float` (one nonzero in sparse vector)
- `Trace` — `entries: vector<TraceEntry>`, `bias: float`, `add(idx, coeff)` (skips `idx < 0` or `coeff == 0`)
- `TrainingPosition` — `trace`, `result: double` (1.0 = white win, 0.5 = draw, 0.0 = black win)
- `extractTrace(bb) → Trace` — mirrors `evaluatePosition()`, recording per-parameter contributions
- `buildParamMap()` — initialize tuning registry and pointer/name index maps
- `findParam(name) → int` — lookup by name (utility, available for debugging/testing)
- `tuning::ScalarParam`, `MobilityTableDef`, `PstDef` — descriptor structs for registry metadata
- `tuning::scalarParams(count)`, `mobilityDefs(count)`, `pstDefs(count)` — static descriptor arrays
- `tuning::paramCount()`, `getName(i)`, `getValue(i)`, `setValue(i, v)`, `getDefault(i)`, `getMin/Max/Step(i)` — registry accessor API
- Eval param `extern` declarations — all 50+ params re-declared for cross-TU access under TUNING (C++17 `inline` variables would eliminate these but require GCC 7+; the tuner toolchain is GCC 5.1)

## Pointer-based index lookup

`extractTrace()` references eval params directly by address via `pIdx(&EVAL_PARAM)`, which returns the registry index (or -1 if not registered). This eliminates:
- The former `TraceIndices` struct (40+ manual field declarations)
- The former `initTraceIndices()` function (100+ manual `findParam()` calls)

For array-based params (PST, mobility, king danger, passed rank), the same pattern applies: `pIdx(&MOBILITY_KNIGHT_MG[nMob])`, `pIdx(PST_DATA[table] + sq)`, etc. Unregistered entries (e.g. mobility[0], passed rank[0]/[7]) return -1 and are safely skipped by `Trace::add`.

## Testing

No dedicated test file — trace correctness verified indirectly via eval regression tests in `test/test_core/test_eval_regression.cpp`. The `extractTrace()` function must mirror `evaluatePosition()` exactly; any drift is caught by eval regression baselines. See `testing.instructions.md` for details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `evaluation.instructions.md` | `extractTrace()` must mirror `evaluatePosition()` exactly — every eval term needs a trace entry |
| `tuner.instructions.md` | Trace is the primary input to the offline tuner |
| `testing.instructions.md` | No dedicated test file — trace correctness verified indirectly via eval regression tests |
