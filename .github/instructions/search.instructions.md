---
applyTo: "lib/core/src/search.*, lib/core/src/stats.*, lib/core/src/move_picker.h"
description: "Search algorithm: negamax + alpha-beta + quiescence, move ordering, pruning, TT, SearchState. Use when editing search.h, search.cpp, move_picker.h, or stats.h."
---

# Search (`lib/core/src/search.h/cpp` + supporting headers)

Fail-soft negamax + alpha-beta + quiescence with iterative deepening. Stateless namespace — all per-search state in `SearchState`.

## File Organization

| File | Purpose |
|------|---------|
| `search.h` | Public API, constants, `SearchLimits`, `SearchResult`, `SearchState`, `TTFlag`, `PackedMove` (lossless pack/unpack), `TTEntry`, `TranspositionTable` (inherits `HashTableBase<TTEntry>`) |
| `search.cpp` | Search algorithm (negamax, quiescence, findBestMove), PV collection, root reordering |
| `search_params.h` | Extracted search constants: pruning margins, reduction thresholds, constexpr LMR table, aspiration/futility/razor/LMP/RFP/SEE capture parameters, tempo bonus, time-management tuning (easy-move, instability factors), LMR history thresholds |
| `move_picker.h` | `MovePicker` struct (staged generation), MVV-LVA scoring, move validation, heuristic update functions (`updateKillers`, `updateHistory`, `updateCaptureCutoffHistory`, `updateQuietCutoffHeuristics`) |
| `stats.h` | `SearchStats` struct, `STAT_INC` macro (active under `-DSTATS` only) |

## Public API

**Entry point**: `findBestMove(pos, limits, state, info = nullptr) → SearchResult`
- Takes `Position&` (by ref — uses make/unmake directly), `SearchLimits`, `SearchState&` (required — caller must own, and must set infrastructure fields: `timeFunc`, `tt`, `pawnHash`, `evalHash` before calling), optional `InfoCallback`
- Returns `SearchResult {bestMove, score, depth, nodes, pv[MAX_PV_LEN], pvLength}`
- `limits.maxDepth` is normalized inside `findBestMove()` to `[1, MAX_PLY]`. External callers may still clamp earlier for UI/protocol feedback, but the search entry point is the final guard because LMR tables and search stacks are fixed-size.
- `limits.rootMoves/rootMoveCount` optionally restrict the root search to legal moves with matching from/to squares; flags are ignored so promotion alternatives for a target square remain searchable. Root-restricted searches skip the opening book because callers need searched scores, not a preselected book move.
- `limits.rootScores/rootScoreCapacity/rootScoreCount` optionally receives the latest completed iteration's root scores as `ScoredMove` entries. When root scores are requested, root moves are searched with full windows so candidate scores are comparable. This is used by Game-level lifted-piece assistance to rank candidate destinations with one shared time budget.

**Types**:
- `SearchLimits` — `maxDepth`, `softTimeMs`, `hardTimeMs`, `stop: atomic<bool>*`, optional root filter/scores (`rootMoves`, `rootMoveCount`, `rootScores`, `rootScoreCapacity`, `rootScoreCount`)
- `TimeFunc = uint32_t (*)(void)` — platform-agnostic time function
- `InfoCallback = void (*)(const SearchResult&)` — per-iteration callback

**Transposition Table** (in `search.h`):
- `TTEntry` — `key32`, `score: int16_t`, `bestMove: PackedMove`, `depth: int8_t`, `flag: TTFlag`, `generation: uint8_t`
- `TranspositionTable : HashTableBase<TTEntry>` — inherits `resize`/`free`/`clear` from `hash_table.h`, adds `newGeneration`, inline `probe`/`store`
- `PackedMove` (uint16_t) — lossless `packMove(m)` / `unpackMove(pm)`. Encoding: from(6) | to(6) | type(4) where type encodes the mutually-exclusive special-move class (0=quiet, 1=capture, 2=EP, 3=castling, 4–7=quiet promo+index, 8–11=capture promo+index). All flags including the 2-bit promotion piece index are preserved.
- Mate scores adjusted per ply (`scoreToTT`/`scoreFromTT` in search.cpp). Size = power-of-two.
- `DEFAULT_TT_SIZE` — 4096 entries (64 KiB). `LibreChessEngine` may further cap dynamically based on available heap

**Constants** (public): `MATE_SCORE=30000`, `MAX_PLY=48`, `MAX_PV_LEN=24`
**Score helpers** (public, constexpr in search.h): `isMateWin(score)`, `isMateLoss(score)`, `isMateScore(score)`, `mateMovesFromScore(score)` — centralise mate-range detection and mate-to-moves conversion. Used by search.cpp (`scoreToTT`/`scoreFromTT`, NMP clamp, early exit) and uci.cpp (info callbacks).
**Constants** (internal, in search.cpp): `INF_SCORE=31000`, `DRAW_SCORE=0`, `CHECK_INTERVAL=512`

**`checkTime()`**: Defined in search.cpp (not inline in header). Interval-masked internally — callers invoke unconditionally, the method returns early unless `nodes` is a multiple of `CHECK_INTERVAL`.

## SearchState (~10 KiB, always required)

Callers must own and pass a `SearchState&` to `findBestMove`. The `Engine` facade holds a direct `SearchState` member and passes it each call. Both `UCIState` and `Game` compose an `Engine` which owns the `SearchState`.

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
- `staticEvals`, `pvLength` — ply-indexed arrays reset each search (stale from previous tree)

**Heuristic tables** (persist across searches within a game, cleared only at game boundaries via `clearHeuristics()`):
- Cleared by `ucinewgame` (UCI path) and `Game::newGame()` (firmware path)
- History gravity naturally ages stale entries between searches

- `killers[MAX_PLY][2]` — PackedMove per ply
- `history[2][6][64]` — `int16_t`, piece-to indexing `[color][pieceType-1][toSq]`
- `captureHistory[6][6][64]` — `int16_t`, `[attackerType-1][victimType-1][toSq]`
- `countermoves[12][64]` — PackedMove, `[pieceIndex][toSquare]`
- `staticEvals[MAX_PLY]` — `int16_t`, for improving flag
- `pv/pvLength` — triangular PV table (PackedMove format)

**Opening book fields** (opt-in, used by `findBestMove`):
- `useBook` — `bool`, default `false`. Enabled by `Game::setTimeFunc()` and `UCIState` constructor
- `bookRng` — `uint64_t`, xorshift64 PRNG state for random book move selection, seeded per-game

## MovePicker (in `move_picker.h`, staged move ordering)

TT move → good captures (MVV-LVA + captureHistory, lazy SEE ≥ 0; cached SEE stored in scores[] for bad captures) → killer moves (2/ply) → countermove → history (append-mode quiet generation via `generateMovesAppend()`) → bad captures (ordered by cached SEE, least-negative first). Score arrays use `int16_t` (~1.7 KiB saved per ply). `pickBestInRange()` — selection sort, O(N) per move.

**Shared LegalityContext**: `MovePicker::init()` builds one `movegen::LegalityContext` eagerly using `pos.kingSq(side)` and stores it in `legalCtx`. The same context is reused by (a) `isMoveValid()` during TT / killer / countermove validation via the ctx-aware overload `movegen::isValidMove(bb, mailbox, from, to, state, ctx)`, and (b) `initCaptures()` / `initQuiets()` for staged generation. This eliminates the previous 2–4 redundant `buildLegalityContext` calls per node (one per validated pseudo-move plus one for generation).

Also contains heuristic update functions: `updateKillers`, `updateHistory` (gravity formula), `updateCaptureCutoffHistory`, `updateQuietCutoffHeuristics`.

## Search Techniques

| Technique | Details |
|-----------|---------|
| Null move pruning | R = NMP_REDUCTION + depth/NMP_DEPTH_DIVISOR + min(NMP_EVAL_BONUS_CAP, evalSurplus/NMP_EVAL_DIVISOR) |
| LMR | `LMR_TABLE.data[depth][moveIndex]`, +1 hist<`LMR_BAD_HIST_THRESHOLD`, −1 hist>`LMR_GOOD_HIST_THRESHOLD`, +1 non-improving, +1 non-PV |
| LMP | Skip late quiets at shallow depths, threshold +2 when improving |
| History pruning | Pre-make skip: hist < −HISTORY_PRUNE_THRESHOLD × depth |
| SEE capture pruning | Prune captures with SEE < −SEE_CAPTURE_PRUNE_MARGIN × depth at non-PV, non-check nodes. Uses MovePicker's cached SEE. |
| Reverse futility | staticEval − RFP_MARGIN × depth / (1+improving) ≥ beta, depth ≤ 6 |
| Razoring | Drop to quiescence when eval far below alpha at shallow depth |
| Aspiration windows | Gradual doubling on fail-low/fail-high |
| Singular extensions | Exclusion search at TT-hit nodes, depth ≥ 6; singularBeta = ttScore − 2×depth. After the exclusion search returns, `pvLength[ply]` is reset to 0 to prevent stale PV data from the same-ply recursive call from leaking into the main search's PV. |
| Recapture extensions | Same target square, depth ≥ 2, lazy SEE ≥ 0 |
| Pawn endgame extension | Extend by `PAWN_ENDGAME_EXTENSION` (2 plies) when a capture transitions to phase=0 (pure pawn endgame). Checked after `pos.make(m)` using `undo.phase > 0 && pos.phase() == 0`. Depth guard: `ply < MAX_PLY - 10`. |
| Delta pruning | QS: skip captures that can't raise alpha |
| Futility pruning | Shallow negamax: skip when eval + margin below alpha |
| Lazy eval | Material-only shortcut when score far outside window |
| History gravity | `h += bonus − h × |bonus| / HISTORY_MAX` |
| Mate distance pruning | Tighten alpha/beta to best possible mate at current ply |
| IIR | Reduce depth at PV nodes without TT hit |
| Pawn-defended-pawn QS pruning | Skip non-pawn × defended-pawn captures in quiescence |
| Draw detection | Twofold repetition (`isRepetition()` → file-local `hasRepeated(hashes, halfmoveClock, 2)`) + 50-move rule at ply > 0. Walk-back bounded by `halfmoveClock` to avoid stale entries from make/unmake corruption. Uses twofold (any single hash match) per [CPW — Repetitions](https://www.chessprogramming.org/Repetitions); game-end uses threefold (`isDraw()` / `isThreefoldRepetition()` → `hasRepeated(hashes, halfmoveClock, 3)`) |

## Key File-Local Helpers (search.cpp)

- `LMR_TABLE` — constexpr `LMRTable` struct (rodata segment, ~3 KiB). `.data[d][m]` holds the base LMR reduction. Uses atanh-based constexpr natural-log approximation. No runtime initialization needed.
- Depth indexes into `LMR_TABLE` are clamped before lookup, matching the public `findBestMove()` max-depth clamp and preventing protocol-supplied oversized depths from reaching fixed-size tables.
- `collectPV()` — triangular PV memcpy
- `validatePV()` — replays the extracted PV on the root position, truncating at the first illegal or drawn move. Uses `isMoveValid()` (from move_picker.h) to check full legality and reconstruct correct flags (EP, castling, promotion type) from the board state, then writes the corrected move back into the result PV. Truncates at twofold repetition (`isRepetition()`) or 50-move rule, matching the search's own draw detection. This catches stale PV entries (hash collisions, SE exclusion search leakage, inter-iteration staleness) and ensures UCI output has correct move notation. Called after PV extraction and before the info callback in both completed-iteration and stopped-mid-iteration paths.
- `computeLMRReduction()` — base table + history/improving/PV adjustments, clamped to `[1, max(1, depth-3)]`
- `reorderRootMoves()` — promote best move to index 0 (uses `Move::operator==` and `std::swap`)
- `scoreToTT()` / `scoreFromTT()` — mate score adjustments for TT storage
- `lazyEval()` / `evaluate()` — search-side evaluation wrappers
- Book probe — inserted in `findBestMove()` after root move generation, before iterative deepening. On hit: returns `SearchResult` with `depth=0`, `nodes=0`, `bestMove` from book.

## Stopped Search Safety

When the search is stopped mid-iteration:
- `negamax` returns 0 immediately; callers must check `state.stopped` before using the score or updating the PV/TT.
- `findBestMove`: if no completed iteration exists yet (result.bestMove is null) but partial results are available, the partial best move from the current iteration is committed. This prevents `bestmove 0000` (which would be an illegal move) and ensures an info line is emitted.
- As a final safety net, if no iteration produced any result, the first root move is returned with depth 0.

## Readability Patterns (search.cpp)

- `canPrune` — `!pvNode && !inCheck`, extracted once at negamax entry. Used by lazy eval, razoring, RFP, NMP, futility.
- `lateQuiet` — `movesSearched > 0 && !m.isTactical()`, extracted per move. Used by futility pruning, LMP, history pruning.
- `Move::isTactical()` — replaces `isCapture() || isPromotion()` compound checks throughout search.
- `Move::isNull()` — replaces manual `from==0 && to==0` null-move detection.

## `stats.h` — Search Statistics (`-DSTATS` only)

- `SearchStats` struct — TT probes/hits, pruning counts (including SEE capture prunes), extension counts, node counts, cutoff stats
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
