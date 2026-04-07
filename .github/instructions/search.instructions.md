---
applyTo: "lib/engine/src/search.*, lib/engine/src/stats.*"
description: "Search algorithm: negamax + alpha-beta + quiescence, move ordering, pruning, TT, SearchState. Use when editing search.h, search.cpp, or stats.h."
---

# Search (`lib/engine/src/search.h/cpp` + `stats.h`)

Fail-soft negamax + alpha-beta + quiescence with iterative deepening. Stateless namespace — all per-search state in `SearchState`.

## Public API

**Entry point**: `findBestMove(pos, limits, timeFunc, ...) → SearchResult`
- Takes `Position&` (by ref — uses make/unmake directly), `SearchLimits`, optional TT/pawnHash/evalHash pointers
- Returns `SearchResult {bestMove, score, depth, nodes, pv[MAX_PV_LEN], pvLength}`

**Types**:
- `SearchLimits` — `maxDepth`, `softTimeMs`, `hardTimeMs`, `stop: atomic<bool>*`
- `TimeFunc = uint32_t (*)(void)` — platform-agnostic time function
- `InfoCallback = void (*)(const SearchResult&)` — per-iteration callback

**Transposition Table**:
- `TTEntry` — `key32`, `score: int16_t`, `bestMove: PackedMove`, `depth: int8_t`, `flag: TTFlag`, `generation: uint8_t`
- `TranspositionTable` — `resize`, `free`, `clear`, `newGeneration`, `probe`, `store`
- `PackedMove` (uint16_t) — `packMove(m)`, `unpackMove(pm)`
- Mate scores adjusted per ply (`scoreToTT`/`scoreFromTT`). Size = power-of-two.

**Constants**: `MATE_SCORE=30000`, `INF_SCORE=31000`, `DRAW_SCORE=0`, `MAX_PLY=64`, `MAX_PV_LEN=24`

## SearchState (~11 KiB, heap-allocated per search)

- `killers[MAX_PLY][2]` — PackedMove per ply
- `history[2][6][64]` — `int16_t`, piece-to indexing `[color][pieceType-1][toSq]`
- `captureHistory[6][6][64]` — `int16_t`, `[attackerType-1][victimType-1][toSq]`
- `countermoves[12][64]` — PackedMove, `[pieceIndex][toSquare]`
- `staticEvals[MAX_PLY]` — `int16_t`, for improving flag
- `pv/pvLength` — triangular PV table (PackedMove format)

## MovePicker (staged move ordering)

TT move → good captures (MVV-LVA + captureHistory, lazy SEE ≥ 0) → killer moves (2/ply) → countermove → history → bad captures (SEE < 0). Score arrays use `int16_t` (~1.7 KiB saved per ply). `pickBestInRange()` — selection sort, O(N) per move.

## Search Techniques

| Technique | Details |
|-----------|---------|
| Null move pruning | R = NMP_REDUCTION + depth/4 + min(3, evalSurplus/200) |
| LMR | `LMR_TABLE[depth][moveIndex]`, +1 hist<−500, −1 hist>1500, +1 non-improving, +1 non-PV |
| LMP | Skip late quiets at shallow depths, threshold +2 when improving |
| History pruning | Pre-make skip: hist < −HISTORY_PRUNE_THRESHOLD × depth |
| Reverse futility | staticEval − RFP_MARGIN × depth / (1+improving) ≥ beta, depth ≤ 6 |
| Razoring | Drop to quiescence when eval far below alpha at shallow depth |
| Aspiration windows | Gradual doubling on fail-low/fail-high |
| Singular extensions | Exclusion search at TT-hit nodes, depth ≥ 6; singularBeta = ttScore − 2×depth |
| Recapture extensions | Same target square, depth ≥ 2, lazy SEE ≥ 0 |
| Delta pruning | QS: skip captures that can't raise alpha |
| Futility pruning | Shallow negamax: skip when eval + margin below alpha |
| Lazy eval | Material-only shortcut when score far outside window |
| History gravity | `h += bonus − h × |bonus| / HISTORY_MAX` |
| Mate distance pruning | Tighten alpha/beta to best possible mate at current ply |
| IIR | Reduce depth at PV nodes without TT hit |
| Pawn-defended-pawn QS pruning | Skip non-pawn × defended-pawn captures in quiescence |

## Key File-Local Helpers

- `scoreMVVLVA()` — MVV-LVA scoring (used by MovePicker + quiescence)
- `collectPV()` — triangular PV memcpy
- `computeLMRReduction()` — base table + history/improving/PV adjustments
- `updateCaptureCutoffHistory()` / `updateQuietCutoffHeuristics()` — beta-cutoff updates
- `reorderRootMoves()` — promote best move to index 0

## `stats.h` — Search Statistics (`-DSTATS` only)

- `SearchStats` struct — TT probes/hits, pruning counts, extension counts, node counts, cutoff stats
- `STAT_INC(field)` — increment macro (no-op without `-DSTATS`)
- `resetStats()`, `getStats()`

## Testing

Mirror test file: `test/test_engine/test_search.cpp` (suite: `test_engine`). Also validated by `test_positions_depth` (WAC baseline), `test_benchmarks/test_regression.cpp` (node count regression), and `test_statistics` (search stats). When changing search techniques, update tests and check regression baselines. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `engine-library.instructions.md` | Parent library — shared conventions |
| `position.instructions.md` | `make()`/`unmake()`/`makeNullMove()`, all position queries |
| `movegen.instructions.md` | `LegalityContext`, staged generation (`generateCaptures`/`generateQuiets`) |
| `evaluation.instructions.md` | `evaluatePosition()` is the leaf node scorer |
| `attacks.instructions.md` | `see()` for capture ordering and pruning decisions |
| `notation.instructions.md` | PV display uses coordinate notation |
| `engine-facade.instructions.md` | `Engine` facade wraps `findBestMove()` |
| `core-headers.instructions.md` | `Move`, `MoveList`, fundamental types |
| `testing.instructions.md` | Test architecture and `test_search.cpp` group description |
