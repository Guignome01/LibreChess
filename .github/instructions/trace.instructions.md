---
applyTo: "lib/core/src/trace.*"
description: "Trace extraction for offline tuning (#ifdef TUNING only). Use when editing trace.h or trace.cpp."
---

# Trace (`lib/core/src/trace.h/cpp`)

Trace extraction for offline tuning. Compiled only with `-DTUNING`. Stateless namespace.

## Public API

- `TraceEntry` — `idx: int16_t`, `coeff: float` (one nonzero in sparse vector)
- `Trace` — `entries: vector<TraceEntry>`, `bias: float`, `add(idx, coeff)`
- `TrainingPosition` — `trace`, `result: double` (1.0 = white win, 0.5 = draw, 0.0 = black win)
- `extractTrace(bb) → Trace` — mirrors `evaluatePosition()`, recording per-parameter contributions
- `buildParamMap()` — initialize tuning registry
- `findParam(name) → int` — lookup by name

## Testing

No dedicated test file — trace correctness verified indirectly via eval regression tests in `test/test_core/test_eval_regression.cpp`. The `extractTrace()` function must mirror `evaluatePosition()` exactly; any drift is caught by eval regression baselines. See `testing.instructions.md` for details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `evaluation.instructions.md` | `extractTrace()` must mirror `evaluatePosition()` exactly — every eval term needs a trace entry |
| `tuner.instructions.md` | Trace is the primary input to the offline tuner |
| `testing.instructions.md` | No dedicated test file — trace correctness verified indirectly via eval regression tests |
