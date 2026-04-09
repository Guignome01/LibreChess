---
applyTo: "lib/core/src/search.*, lib/core/src/stats.*, lib/core/src/move_picker.h"
description: "Search algorithm: negamax + alpha-beta + quiescence, move ordering, pruning, TT, SearchState. Use when editing search.h, search.cpp, move_picker.h, or stats.h."
---

# Search (`lib/core/src/search.h/cpp` + supporting headers)

Fail-soft negamax + alpha-beta + quiescence with iterative deepening. Stateless namespace — all per-search state in `SearchState`.

## File Organization

| File | Purpose |
|------|---------|
| `search.h` | Public API, constants, `SearchLimits`, `SearchResult`, `SearchState`, `TTFlag`, `PackedMove` (pack/unpack), `TTEntry`, `TranspositionTable` (inherits `HashTableBase<TTEntry>`) |
| `search.cpp` | Search algorithm (negamax, quiescence, findBestMove), PV collection, root reordering |
| `search_params.h` | Extracted search constants: pruning margins, reduction thresholds, constexpr LMR table, aspiration/futility/razor/LMP/RFP parameters, tempo bonus |
| `move_picker.h` | `MovePicker` struct (staged generation), MVV-LVA scoring, move validation, heuristic update functions (`updateKillers`, `updateHistory`, `updateCaptureCutoffHistory`, `updateQuietCutoffHeuristics`) |
| `stats.h` | `SearchStats` struct, `STAT_INC` macro (active under `-DSTATS` only) |

## Public API

**Entry point**: `findBestMove(pos, limits, state, info = nullptr) → SearchResult`
- Takes `Position&` (by ref — uses make/unmake directly), `SearchLimits`, `SearchState&` (required — caller must own, and must set infrastructure fields: `timeFunc`, `tt`, `pawnHash`, `evalHash` before calling), optional `InfoCallback`
- Returns `SearchResult {bestMove, score, depth, nodes, pv[MAX_PV_LEN], pvLength}`

**Types**:
- `SearchLimits` — `maxDepth`, `softTimeMs`, `hardTimeMs`, `stop: atomic<bool>*`
- `TimeFunc = uint32_t (*)(void)` — platform-agnostic time function
- `InfoCallback = void (*)(const SearchResult&)` — per-iteration callback

**Transposition Table** (in `search.h`):
- `TTEntry` — `key32`, `score: int16_t`, `bestMove: PackedMove`, `depth: int8_t`, `flag: TTFlag`, `generation: uint8_t`
- `TranspositionTable : HashTableBase<TTEntry>` — inherits `resize`/`free`/`clear` from `hash_table.h`, adds `newGeneration`, inline `probe`/`store`
- `PackedMove` (uint16_t) — `packMove(m)`, `unpackMove(pm)`
- Mate scores adjusted per ply (`scoreToTT`/`scoreFromTT` in search.cpp). Size = power-of-two.
- `DEFAULT_TT_SIZE` — 4096 entries (64 KiB). `LibreChessProvider` may further cap dynamically based on available heap

**Constants** (public): `MATE_SCORE=30000`, `MAX_PLY=48`, `MAX_PV_LEN=24`
**Constants** (internal, in search.cpp): `INF_SCORE=31000`, `DRAW_SCORE=0`, `CHECK_INTERVAL=512`

**`checkTime()`**: Defined in search.cpp (not inline in header). Interval-masked internally — callers invoke unconditionally, the method returns early unless `nodes` is a multiple of `CHECK_INTERVAL`.

## SearchState (~10 KiB, always required)

Callers must own and pass a `SearchState&` to `findBestMove`. The `Engine` facade holds a direct `SearchState` member and passes it each call.

**Constructor**: `explicit SearchState(TimeFunc tf = nullptr, TranspositionTable* tt = nullptr, PawnHashTable* ph = nullptr, EvalHashTable* eh = nullptr)` — wires infrastructure pointers once at construction. All parameters optional (default nullptr). Eliminates manual field-by-field wiring.

**Infrastructure fields** (set via constructor, persist across calls):
- `timeFunc` — `TimeFunc` for platform-agnostic time
- `tt` — `TranspositionTable*`
- `pawnHash` — pawn hash table pointer
- `evalHash` — eval hash table pointer

**Per-search transient fields** (reset by `findBestMove` each call):
- `nodes`, `stopped` — reset at start
- `startTime`, `hardTimeMs` — derived from limits + timeFunc
- `externalStop` — set from `limits.stop`
- `clearHeuristics()` — called internally by findBestMove (not part of public API)

**Heuristic tables** (persist across calls for same instance):

- `killers[MAX_PLY][2]` — PackedMove per ply
- `history[2][6][64]` — `int16_t`, piece-to indexing `[color][pieceType-1][toSq]`
- `captureHistory[6][6][64]` — `int16_t`, `[attackerType-1][victimType-1][toSq]`
- `countermoves[12][64]` — PackedMove, `[pieceIndex][toSquare]`
- `staticEvals[MAX_PLY]` — `int16_t`, for improving flag
- `pv/pvLength` — triangular PV table (PackedMove format)

## MovePicker (in `move_picker.h`, staged move ordering)

TT move → good captures (MVV-LVA + captureHistory, lazy SEE ≥ 0; cached SEE stored in scores[] for bad captures) → killer moves (2/ply) → countermove → history (append-mode quiet generation via `generateMovesAppend()`) → bad captures (ordered by cached SEE, least-negative first). Score arrays use `int16_t` (~1.7 KiB saved per ply). `pickBestInRange()` — selection sort, O(N) per move.

Also contains heuristic update functions: `updateKillers`, `updateHistory` (gravity formula), `updateCaptureCutoffHistory`, `updateQuietCutoffHeuristics`.

## Search Techniques

| Technique | Details |
|-----------|---------|
| Null move pruning | R = NMP_REDUCTION + depth/4 + min(3, evalSurplus/200) |
| LMR | `LMR_TABLE.data[depth][moveIndex]`, +1 hist<−500, −1 hist>1500, +1 non-improving, +1 non-PV |
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

## Key File-Local Helpers (search.cpp)

- `LMR_TABLE` — constexpr `LMRTable` struct (rodata segment, ~3 KiB). `.data[d][m]` holds the base LMR reduction. Uses atanh-based constexpr natural-log approximation. No runtime initialization needed.
- `collectPV()` — triangular PV memcpy
- `computeLMRReduction()` — base table + history/improving/PV adjustments, clamped to `[1, max(1, depth-3)]`
- `reorderRootMoves()` — promote best move to index 0 (uses `Move::operator==` and `std::swap`)
- `scoreToTT()` / `scoreFromTT()` — mate score adjustments for TT storage
- `lazyEval()` / `evaluate()` — search-side evaluation wrappers

## Readability Patterns (search.cpp)

- `canPrune` — `!pvNode && !inCheck`, extracted once at negamax entry. Used by lazy eval, razoring, RFP, NMP, futility.
- `lateQuiet` — `movesSearched > 0 && !m.isTactical()`, extracted per move. Used by futility pruning, LMP, history pruning.
- `Move::isTactical()` — replaces `isCapture() || isPromotion()` compound checks throughout search.
- `Move::isNull()` — replaces manual `from==0 && to==0` null-move detection.

## `stats.h` — Search Statistics (`-DSTATS` only)

- `SearchStats` struct — TT probes/hits, pruning counts, extension counts, node counts, cutoff stats
- `STAT_INC(field)` — increment macro (no-op without `-DSTATS`)
- `resetStats()`, `getStats()`

## Testing

Mirror test file: `test/test_core/test_search.cpp` (suite: `test_core`). Also validated by `test_positions_depth` (WAC baseline), `test_benchmarks/test_regression.cpp` (node count regression), and `test_statistics` (search stats). When changing search techniques, update tests and check regression baselines. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `position.instructions.md` | `make()`/`unmake()`/`makeNullMove()`, all position queries |
| `movegen.instructions.md` | `LegalityContext`, staged generation (`generateMoves` with `FilterMode`) |
| `evaluation.instructions.md` | `evaluatePosition()` is the leaf node scorer |
| `attacks.instructions.md` | `see()` for capture ordering and pruning decisions |
| `notation.instructions.md` | PV display uses coordinate notation |
| `uci.instructions.md` | UCI protocol dispatcher calls `findBestMove()` |
| `time-management.instructions.md` | `computeTimeLimits()` converts UCI clock to `SearchLimits` |
| `core-headers.instructions.md` | `Move`, `MoveList`, fundamental types |
| `testing.instructions.md` | Test architecture and `test_search.cpp` group description |
