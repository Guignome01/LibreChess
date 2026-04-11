---
applyTo: "tools/tune/**"
description: "Offline tuner: Texel's tuning method with Adam optimizer. Eval parameter optimization using EPD corpus and trace extraction."
---

# Tuner — Texel's Tuning Method with Adam Optimizer

Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method

## Overview

Offline gradient-descent optimizer for `EVAL_CONST` evaluation parameters. Compiles `lib/core/` with `-DTUNING` (making `EVAL_CONST` expand to empty instead of `constexpr`), enabling runtime parameter mutation. The tuner uses precomputed sparse traces (feature vectors) and the Adam optimizer to minimize the MSE between predicted game outcomes (sigmoid of evaluation score) and actual game results from a labeled corpus.

## Architecture

### Data Flow

```
corpus.epd ─┬─ loadCorpus()       → RawEntry[]  (bitboards + results)
             ├─ extractTrace()     → Trace[]     (sparse feature vectors)
             ├─ findOptimalK()     → K           (sigmoid scaling constant)
             ├─ adamOptimize()     → params[]    (float → int at end)
             └─ printResults()     → C++ output  (copy-paste into evaluation.cpp)
```

### Key Types (from `lib/core/src/trace.h`)

| Type | Purpose |
|------|---------|
| `TraceEntry` | One nonzero coefficient: `{int16_t idx, float coeff}` |
| `Trace` | Sparse vector of `TraceEntry` — dot product with params = position score |
| `TrainingPosition` | `Trace` + game `result` (1.0 = white win, 0.5 = draw, 0.0 = black win) |

### Registry (descriptor-driven, `#ifdef TUNING`)

Param metadata (name, pointer, bounds) is owned by `lib/core/src/trace.h/cpp`
via descriptor getters.  `buildRegistry()` iterates the descriptors.

| Component | Location | Purpose |
|-----------|----------|----------|
| `EVAL_CONST` macro | `eval_params.h` | Expands to empty under `-DTUNING`, `constexpr` in production |
| Descriptor structs | `trace.h` | `ScalarParam`, `MobilityTableDef`, `PstDef` — metadata types |
| `scalarParams(count)` | `trace.cpp` | Returns array of 77 scalar descriptors (name, ptr, min, max, step) |
| `mobilityDefs(count)` | `trace.cpp` | Returns array of 4 mobility table descriptors (prefix, MG/EG data, size, bounds) |
| `pstDefs(count)` | `trace.cpp` | Returns array of 12 PST descriptors (prefix, data, isPawn, bounds) |
| `TuneEntry` | `trace.cpp` | `{name, ptr, defaultVal, min, max, step}` — runtime registry entry |
| `buildRegistry()` | `trace.cpp` | Iterates descriptor getters to build 953 entries: 77 scalar + 124 mobility + 752 PST |
| `namespace tuning` | `trace.h` | API: `paramCount()`, `getName(i)`, `getValue(i)`, `setValue(i, v)`, `getDefault(i)`, `getMin(i)`, `getMax(i)` |
| Param externs | `trace.h` | All 50+ `extern` declarations for eval params (needed for GCC 5.1 — inline variables would eliminate these) |

## Core Algorithm

### Error Function (CPW §Method)

$$E = \frac{1}{N} \sum_{i=1}^{N} \left( R_i - \sigma(q_i, K) \right)^2$$

where $\sigma(s, K) = \frac{1}{1 + 10^{-Ks/400}}$, $R_i$ is the game result, and $q_i$ is the evaluation score from the trace dot product.

### Optimizer: Adam with Float Accumulators

Parameters accumulate in `double` throughout optimization. Integer rounding and bounds clamping happen **only once at the end** after all epochs complete. This prevents sub-integer gradients from being destroyed by per-epoch rounding — the root cause of parameters getting stuck at their initial values.

### Sigmoid Scaling Constant K

Found via ternary search over [0.1, 3.0] at startup. Recalculated every 50 epochs during optimization to track parameter drift. As parameters change, the optimal scaling shifts — periodic recalculation keeps the sigmoid well-calibrated throughout training.

### Cosine Annealing Learning Rate

$\text{lr}(t) = \text{LR}_{\text{base}} \cdot 0.5 \cdot (1 + \cos(\pi \cdot t / T))$

Gradually reduces LR from `ADAM_LR` to ~0 over the training run. Maintains aggressive early learning while reducing oscillation in later epochs. Reference: Loshchilov & Hutter, "SGDR: Stochastic Gradient Descent with Warm Restarts", 2017.

### Early Stopping

Tracks test MSE every 10 epochs. If no improvement for 50 epochs (patience), training stops and restores the best parameters (lowest test MSE). Prevents overfitting and saves time when convergence stalls.

### Gradient Validation

Before training starts, validates analytical gradients against central finite-difference approximation for 10 randomly sampled parameters (ε=1e-4, relative tolerance=1e-3). Catches chain-rule bugs in the gradient computation early.

## Hyperparameters

| Constant | Value | Notes |
|----------|-------|-------|
| `ADAM_LR` | 0.1 | Base learning rate (cosine-annealed during training). Conservative — gradients compound properly with float accumulators. |
| `ADAM_BETA1` | 0.9 | First moment decay. |
| `ADAM_BETA2` | 0.999 | Second moment decay. |
| `ADAM_EPS` | 1e-8 | Numerical stability. |
| `L2_LAMBDA` | 1e-7 | L2 regularization for PST parameters only (prevents PST drift). |
| `K_RECALC_INTERVAL` | 50 | Recalculate sigmoid scaling K every N epochs. |
| `PATIENCE` | 50 | Early stopping patience (epochs without test MSE improvement). |

## Corpus Format

EPD lines with a `c9` opcode carrying the game result:

```
rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - c9 "1/2-1/2";
```

### Accepted Result Formats

| Format | Value | Source |
|--------|-------|--------|
| `1-0` / `"1-0"` | 1.0 | Standard PGN |
| `0-1` / `"0-1"` | 0.0 | Standard PGN |
| `1/2-1/2` / `"1/2-1/2"` | 0.5 | Standard PGN |
| `1.0` | 1.0 | Decimal (c-chess-cli, Lichess) |
| `0.0` | 0.0 | Decimal |
| `0.5` | 0.5 | Decimal |

## Build & Run

### Prerequisites

- g++ (MinGW-W64 from PlatformIO works): add `$env:USERPROFILE\.platformio\packages\toolchain-gccmingw32\bin` to PATH
- Corpus file (e.g. `quiet-labeled.epd`) in `tools/tune/`

### Commands

```bash
cd tools/tune
make clean && make       # Build the tuner
./tune quiet-labeled.epd           # 500 epochs (default)
./tune quiet-labeled.epd 200       # Custom epoch count
make pipeline                      # Build + run in one step
```

### Output

- **stderr**: Progress (epoch number, train/test MSE, learning rate), gradient validation results, K recalculation updates, early stopping notification
- **stdout**: Two sections:
  1. *Changed parameter values* — summary of params that differ from defaults, with old/new values
  2. *Copy-paste block* — complete `eval_params.h`-ready output: material arrays (`MAT_ELEM`), PST arrays (`PST_ELEM`), pawn structure arrays & scalars, piece bonuses, rook bonuses, mobility tables, king safety, king proximity, space — all with correct `EVAL_CONST` macros and variable names, organized by section
- **tune.txt**: Machine-readable log of all parameter values (key=value format)

## Tuning Iteration Workflow

1. Run the tuner on a labeled corpus
2. Review the changed parameter values (first stdout section) for sanity
3. Copy-paste the formatted block (second stdout section) into `eval_params.h`, replacing the corresponding `EVAL_CONST` definitions between the `namespace eval {` opening and the final `EVAL_FIXED` declarations
4. Rebuild the tuner (`make clean && make`) — defaults are now the new values
5. Run again — the next iteration starts from updated defaults
6. Repeat until MSE improvement plateaus

## Design Constraints

- **Trace linearity**: The evaluation must be linear in all tuned parameters. `extractTrace()` mirrors `evaluatePosition()` exactly — any new eval term that uses a tunable parameter must have a corresponding trace entry added.
- **King danger table**: Table entries are tuned directly as linear params. `KING_DANGER_WEIGHT[]` is `constexpr` (not tunable) — keeps all parameters linear for exact analytical gradients.
- **MAT_PAWN pinned at 100**: Defines the centipawn unit. Search pruning margins (futility, delta, razor) are calibrated for 100cp/pawn. Not tunable.
- **PST frozen squares**: Pawn PST squares on rank 1 and rank 8 (where pawns can never be) are excluded from tuning. 752 PST entries, not 768.

## Modifying the Tuner

When adding new eval terms:
1. Add `EVAL_CONST` parameter(s) in `eval_params.h`
2. Add `extern` declaration(s) in `trace.h` (inside the extern block)
3. Add descriptor entry to the appropriate getter in `trace.cpp` (`scalarParams`, `mobilityDefs`, or `pstDefs`) — this handles registration automatically
4. Add trace logic in `extractTrace()` (in `lib/core/src/trace.cpp`) using `pIdx(&PARAM)` — coefficients must exactly mirror how `evaluatePosition()` uses the parameter
5. Rebuild and run

No changes to `buildRegistry()` or index initialization are needed — both auto-discover params via the descriptor getters and pointer-based index maps.

When changing hyperparameters, adjust the constants at the top of `tune.cpp`. The learning rate (`ADAM_LR`) is the most sensitive — if MSE oscillates, lower it; if convergence is too slow, raise it.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `trace.instructions.md` | `TraceEntry`, `Trace`, `TrainingPosition` types — trace is the primary input |
| `evaluation.instructions.md` | Tuner reads/writes eval parameters defined in `eval_params.h` |
| `epd.instructions.md` | Corpus format uses EPD parser types |
