---
applyTo: "lib/core/**, lib/game/**, lib/engine/**, src/game_mode/**, src/engine/**"
description: "Core chess library classes — Game, Position, History, movegen/rules, piece, utils, fen, notation, zobrist, types, interfaces. Use for any work on lib/core/, lib/game/, lib/engine/, game modes, or engine providers including bug fixes, feature additions, refactoring, or test writing."
---

# Chess Libraries (`lib/`)

Three PlatformIO libraries with clean dependency boundaries: `core ← game`, `core ← engine`. Game never imports engine and vice versa.

Pure C++ with no hardware dependencies. Natively compilable for unit tests. Uses `std::string` (not Arduino `String`); firmware bridges with `.c_str()` / `std::string()`.

## Component Roles

### Foundation (`lib/core/`)

| Class/Namespace | Role | State |
|-----------------|------|-------|
| `Position` | Position container, move execution, game-end detection, null move support | Board array, turn, PositionState, king cache, HashHistory |
| `piece` | Type-safe piece representation: type/color extraction, construction, predicates, color-derived constants, FEN char conversion, Zobrist indexing (`pieceZobristIndex` returns 0–11 or `ZOBRIST_IDX_NONE`; `isValidZobristIndex()` predicate for bounds checking) | Stateless namespace (all constexpr), defined in `piece.h` |
| `movegen`/`rules` | Per-piece and bulk move generation, check/checkmate/stalemate detection | Stateless (all static) |
| `eval` | Tapered evaluation: material (MG via MATERIAL[], pawn MG fixed at 100cp; EG via MATERIAL_EG[], pawn EG tunable) + phase-specific PSTs (all six piece types have separate MG/EG tables) + pawn structure (rank-based exponential passed pawn scaling via `PASSED_RANK_BONUS_MG/EG[8]`, isolated MG/EG, doubled MG/EG, backward MG/EG, connected passers MG/EG, protected passer (MG only)) + passed pawn king distance (EG only, not pawn-hashed — bonus for own king proximity, penalty for enemy king proximity via `evalPassedPawnKingDist`) + positional terms (bishop pair, bad bishop (penalty per own pawn on same color complex, −3 MG / −5 EG each), rook on open/semi-open file (MG/EG split), rook on 7th rank (enemy king on back rank or enemy pawns on starting rank), rook behind passer (Tarrasch Rule, EG only via `evalRookBehindPasser`), mobility with MG/EG split weights per piece type via `AttackInfo`, king safety/pawn shield (rank-indexed penalties via `SHIELD_ADV_RANK3`, `SHIELD_ADV_RANK4PLUS`), king danger (unified zone attack counting + proximity, nonlinear `KING_DANGER_TABLE[13]` with per-piece-type `KING_DANGER_WEIGHT[]`, MG only), knight outposts (MG/EG split), space (MG only via `SPACE_BONUS_MG`), trapped pieces, threats (pawn→minor/rook/queen MG only, minor→rook/queen MG only, rook→queen MG only via `evalThreats`), opposite-color bishop scaling (×0.75 when each side has one bishop on different color complexes, endgame phase ≤ 6)). Tempo bonus applied in search layer. Game phase from non-pawn material (N=1, B=1, R=2, Q=4; max 24). Returns centipawns (`int`). Pawn hash table (`PawnHashTable`) caches pawn structure MG/EG scores; eval hash table (`EvalHashTable`) caches full evaluation results. Both are 8 KiB (1024 entries × 8B), owned by `Engine`, passed as optional pointers. | Stateless namespace |
| `utils` | Board-level helpers: coordinate helpers, castling/EP analysis, `gameResultName()` | Stateless namespace |
| `iterator` | Board iteration helpers: `forEachSquare`, `forEachPiece`, `somePiece`, `findPiece` | Stateless namespace (header-only) |
| `LibreChess` (bitboard) | Bitboard types, LERF square mapping, bit manipulation (`popcount`, `lsb`, `popLsb`), file/rank masks, square-color masks (`DARK_SQUARES`, `LIGHT_SQUARES`), directional shifts, `BitboardSet` (12 piece + 2 color + occupancy bitboards with `setPiece`/`removePiece`/`movePiece`) | Stateless namespace (header-only) |
| `attacks` | Precomputed leaper tables (`KNIGHT[64]`, `KING[64]`, `PAWN[2][64]`), O(1) slider functions (`rook` via first-rank table + Hyperbola Quintessence, `bishop` via HQ on diagonal masks, `queen` = rook+bishop), x-ray attack functions (`xrayRook`, `xrayBishop`), `between(s1, s2)` (strictly between, exclusive), `line(s1, s2)` (full line through both, inclusive, edge-to-edge), `AttackInfo` struct + `computeAll(bb)` (per-piece-type and per-color attack maps; **pre-built** for king safety, mobility, and move ordering), `isSquareUnderAttack(bb, sq, color)` (per-piece-type attack table lookups), `see(bb, mailbox, move)` (Static Exchange Evaluation — swap algorithm with least-valuable-attacker iteration) | Stateless namespace (~3 KiB tables, initialized once via `init()`) |
| `eval` | Pawn-structure masks (`pawnPassedMask`, `pawnIsolatedMask`, `pawnForwardMask` — file-scoped) and helper queries (`isPassed`, `isIsolated`, `isDoubled`, `isBackward`), lazy-initialized on first `evaluatePosition()` call via `initPawnMasks()` | Stateless namespace |
| `zobrist` | Zobrist key generation (constexpr xorshift64), piece-index mapping, full-board hash computation, pawn-only hash (`computePawnHash`) for pawn hash table | Stateless namespace (header-only) |
| `fen` | FEN parse/serialize/validate | Stateless namespace |
| `notation` | Coordinate/SAN/LAN conversion | Stateless namespace |
| `epd` | Generic EPD parser (`namespace LibreChess`): `EPDOperation`, `EPDRecord`, `parseEPDLine()`, `validateEPDLine()`. Supports standard opcodes (`bm`, `am`, `id`, `c0`, `c9`). Used by tactical test suites and offline tuner. | Stateless namespace |
| `eval` (trace) | Trace extraction for offline tuning (`#ifdef TUNING` only): `TraceEntry`, `Trace`, `TrainingPosition`, `extractTrace(bb)` (mirrors `evaluatePosition()` recording per-parameter contributions), `buildParamMap()`, `findParam()`. Defined in `trace.h`/`trace.cpp`. | Stateless namespace (compiled only with `-DTUNING`) |

### Game (`lib/game/`)

| Class/Namespace | Role | State |
|-----------------|------|-------|
| `Game` | Central orchestrator, sole owner of game lifecycle | Composes Position + History |
| `History` | In-memory move log + persistent recording | Cursor-based undo/redo, binary storage |

### Engine (`lib/engine/`)

| Class/Namespace | Role | State |
|-----------------|------|-------|
| `search` | On-board chess engine: fail-soft negamax + alpha-beta + quiescence (MVV-LVA ordered captures, pawn-defended-pawn pruning), iterative deepening, check extensions, recapture extensions (extend recaptures to same target square, guarded by depth ≥ 2 and cached SEE ≥ 0), singular extensions (exclusion search at TT-hit nodes with depth ≥ 6: search all moves except TT move at half depth with narrow window around ttScore − 2×depth; if nothing reaches singularBeta, extend the TT move by 1 ply), PVS, null move pruning (adaptive R = NMP_REDUCTION + depth/4 + min(3, evalSurplus/200)), late move reductions (logarithmic `LMR_TABLE[depth][moveIndex]` base + history-informed: hist < -500 → ++reduction, hist > 1500 → --reduction, +1 when not improving, +1 when non-PV), late move pruning (skip late quiet moves at shallow depths, threshold +2 when improving), history pruning (pre-make skip of quiet moves with deeply negative history at shallow depths: hist < -HISTORY_PRUNE_THRESHOLD × depth), reverse futility pruning (staticEval - RFP_MARGIN*depth/(1+improving) >= beta, depth ≤ 6), razoring (drop to quiescence when static eval is far below alpha at shallow depths), lazy evaluation (material-only shortcut when score is far outside alpha/beta window), aspiration windows (gradual doubling on fail-low/fail-high), root move reordering, internal iterative reductions (IIR), delta pruning (quiescence), futility pruning (shallow negamax), SEE-based capture ordering (losing captures demoted below quiets, SEE cached for recapture extension reuse), mate distance pruning (tighten alpha/beta to best possible mate at current ply), transposition table, move ordering (TT move → good captures (MVV-LVA, SEE≥0) → killers → countermove heuristic → history → bad captures (SEE<0)), improving flag (ply-2 eval comparison with ply-4 fallback), history gravity (unified bonus/penalty via gravity formula: `h += bonus − h × |bonus| / HISTORY_MAX`), triangular PV table (collects principal variation line for each completed iteration) | Stateless namespace (search state passed in/out) |
| `Engine` | Direct-call facade over `search::findBestMove()`. Owns Position, TranspositionTable, stop control. API: `calculateMove(fen, limits) → SearchResult` | Stateful (owns Position + TT) |

### Interfaces (DI)

| Interface | Purpose | Concrete impl |
|-----------|---------|---------------|
| `IGameStorage` | Persistence: live game files, FEN tables, finalize/discard | `LittleFSStorage` |
| `IGameObserver` | Board-state notification: `onBoardStateChanged(fen, evaluation)` (evaluation in centipawns) | `WiFiManagerESP32` |
| `ILogger` | Diagnostic output: `info()`, `error()`, formatted helpers | `SerialLogger` |
| `Log` | Null-safe logger proxy (value type wrapping `ILogger*`) | Defined in `logger.h` |

All nullable — core classes use `Log` proxy members instead of raw `ILogger*` pointers, eliminating manual null guards.

## Fundamental Concepts

Six independent concepts compose `Position`'s game state. Understanding these prevents confusion about what each field represents and why they're grouped the way they are.

| # | Concept | Storage | Role |
|---|---------|---------|------|
| 1 | Piece layout | `BitboardSet bb_` + `Piece mailbox_[64]` | Dual representation: 12 piece bitboards + 2 color + occupancy for fast set operations; flat mailbox for O(1) piece identity by square. Both updated in lockstep on every mutation. |
| 2 | Turn | `Color currentTurn_` | Whose move |
| 3 | Move-gen context | `PositionState.castlingRights`, `.epRow`, `.epCol` | Input to movegen/rules for move generation and Zobrist hashing |
| 4 | Game clocks | `PositionState.halfmoveClock`, `.fullmoveClock` | 50-move rule counter, FEN move numbering |
| 5 | King cache | `Square kingSquare_[2]` | Single LERF square index per color, derived from `bb_`, maintained incrementally for O(1) check detection. `kingRow(c)` / `kingCol(c)` convert via `rowOf()` / `colOf()`. |
| 6 | Zobrist hash | `uint64_t hash_` | Running Zobrist hash, updated incrementally on every move via XOR deltas (no full recompute). `computeHash()` kept for debug verification. |
| 7 | Hash history | `HashHistory hashHistory_` | Zobrist hashes of past positions for threefold repetition detection |

Concepts 3+4 are bundled in `PositionState` because `MoveEntry` stores the full state for undo — splitting would create friction at that boundary for minimal gain. Concept 5 is derived (redundant with `bb_`) but essential for performance. Concept 6 is derived but avoids expensive full-board hashing on every move. Concept 7 resets on irreversible moves (pawn push, capture).

## Data Flow: How a Move Works

Understanding this flow prevents bugs where steps get skipped or reordered:

1. **Firmware** calls `Game::makeMove(from, to, promotion)` — the only entry point for moves
2. **Game** delegates to `Position::makeMove()`, which:
   - Validates via `movegen::isValidMove()` (returns invalid `MoveResult` if illegal)
   - Applies the move via `applyMoveToBoard()`: updates `BitboardSet` + `mailbox_` in lockstep (piece movement, captures, castling rook, en passant removal, promotion), incrementally updates Zobrist hash via XOR deltas, updates `kingSquare_[]` cache
   - Updates `PositionState` (castling rights, EP target, halfmove/fullmove clocks)
   - Records position hash in `HashHistory`
   - Runs `movegen::isGameOver()` → sets `MoveResult.gameResult` if checkmate/stalemate/draw
3. **Game** logs the move description via `ILogger` (piece, type, from/to square, promotion)
4. **Game** records the move in `History` (which auto-persists if recording)
5. **Game** checks threefold repetition (Zobrist comparison), auto-ends game if detected
6. **Game** logs game-end events via `ILogger` (in `endGame()`), or logs check/turn if game continues (in `makeMove()`)
7. **Game** notifies `IGameObserver` with updated FEN + evaluation
8. **Game** auto-saves recording if the game just ended

**Key invariant**: Steps 2–8 are atomic from the caller's perspective. A valid `makeMove()` always completes all steps.

## Design Decisions

These explain *why* the architecture is the way it is — constraints that code alone doesn't communicate:

- **Game is the only firmware entry point** — firmware (`src/`) must never include `Position`, `History`, `movegen`/`rules`, or `iterator` directly. `Game` re-exports iterator helpers (`forEachSquare`, `forEachPiece`, `somePiece`). This guarantees recording, observer notification, and lifecycle tracking can't be bypassed. Native tests may include internal headers.

- **Position has no lifecycle state** — `gameOver_`, `gameResult_`, `winnerColor_` live in `Game`, not `Position`. This separation means `Position` is a replayable position container: you can undo past game-end, replay games into a scratch board, or query positions without lifecycle side effects.

- **movegen/rules and notation are stateless** — all context (board, turn, `PositionState`) is passed as parameters. This makes them safe to call from any context (temp boards for check detection, history replay, test assertions) without hidden state coupling.

- **Undo clears game-over** — `undoMove()` re-opens a finished game. This enables the web UI to navigate back through completed games. It means code must re-check `isGameOver()` after undo rather than assuming a game stays finished.

- **History has two concerns in one class** — `History` handles both the in-memory move log and persistent recording. These were unified because they share the move cursor and branch-on-undo must truncate both the log and storage atomically.

- **Branch-on-undo wipes future moves** — when you undo 3 moves and make a new one, all undone moves are permanently deleted from both memory and storage. This is intentional: the binary recording format doesn't support branching.

- **Header flushes every full turn** — `History` only writes the `GameHeader` to flash after black's move. This halves flash wear while still allowing mid-game resume. If the device loses power on white's move, resumption loses at most one move.

- **Bitboard + mailbox dual representation** — `Position` stores both a `BitboardSet` (12 piece bitboards + 2 color aggregates + occupancy) and a flat `Piece mailbox_[64]`. Both are updated in lockstep on every mutation. Bitboards enable fast set operations (attack detection via bitwise AND, material counting via `popcount`). The mailbox provides O(1) piece identity for any square (needed by FEN serialization, SAN disambiguation, etc.). LERF (Little-Endian Rank-File, a1=0, h8=63) is the square mapping, with `squareOf(row, col)` / `rowOf(sq)` / `colOf(sq)` bridging to the project's row/col coordinate system. See [Chess Programming Wiki — Bitboards](https://www.chessprogramming.org/Bitboards).

- **Optimized slider attacks** — slider attack generation uses two O(1) techniques: **first-rank lookup table** for rank attacks (512-byte `FIRST_RANK_ATTACKS[8][64]` table, indexed by file + 6-bit inner occupancy) and **Hyperbola Quintessence** for file/diagonal/anti-diagonal attacks (branchless `o^(o-2r)` subtraction trick with byte-swap for negative rays). Diagonal masks (`DIAG_MASK[64]`, `ANTI_DIAG_MASK[64]`) are precomputed at startup. Leaper attacks (knight, king, pawn) use precomputed lookup tables (`KNIGHT[64]`, `KING[64]`, `PAWN[2][64]` — ~2.5 KiB total). All tables initialized once via `attacks::init()`. Public API: `rook(sq, occ)`, `bishop(sq, occ)`, `queen(sq, occ)`. `computeAll(bb)` builds per-piece-type attack maps for both colors in one pass (pawns via bulk shift, leapers via table, sliders via HQ) — pre-built infrastructure for planned king safety, mobility, and move ordering terms (not yet called by production code). Returned as an `AttackInfo` struct: `byPiece[2][7]`, `byColor[2]`, `allAttacks`. `line(s1, s2)` is pre-built for SEE (Static Exchange Evaluation) x-ray attacker discovery. Reference: [Chess Programming Wiki — Hyperbola Quintessence](https://www.chessprogramming.org/Hyperbola_Quintessence).

- **Pawn-structure mask module** — `eval` precomputes pawn-structure masks lazily on first `evaluatePosition()` call (`pawnPassedMask`, `pawnIsolatedMask`, `pawnForwardMask` — file-scoped arrays) and exposes bitboard-only queries (`isPassed`, `isIsolated`, `isDoubled`, `isBackward`).

- **Pawn hash table** — `PawnHashTable` (in `evaluation.h`) caches pawn structure MG/EG scores keyed by `computePawnHash()`. Pawn structures change rarely during search (~1 pawn move per 30 plies), yielding ~95%+ hit rate. 1024 entries × 8B = 8 KiB. Always-replace. Passed as optional `PawnHashTable*` to `evaluatePosition()` → `evalPawnStructure()`.

- **Evaluation hash table** — `EvalHashTable` (in `evaluation.h`) caches full `evaluatePosition()` results keyed by position Zobrist hash. Avoids redundant evaluations for transpositions. 1024 entries × 8B = 8 KiB. Probed/stored in `search.cpp`'s `evaluate()` wrapper.

- **Incremental Zobrist hashing** — `applyMoveToBoard()` XORs Zobrist key deltas inline (`hash_ ^= pieceKey[from] ^ pieceKey[to]` plus castling/EP/side-to-move changes). `computeHash()` remains for debug verification but is never called in the hot path. `computePawnHash()` XORs piece keys for all white (index 0) and black (index 6) pawns — used as the lookup key for the pawn hash table.

- **FEN validation is two-step** — `validateFEN()` checks format (strict), `fenToBoard()` parses (lenient). `Position::loadFEN()` calls validate first, returns `false` on failure without modifying state. Additionally, `loadFEN()` rejects structurally valid FENs with missing kings (both kings must be present); state is fully restored on rejection. This means `fenToBoard()` alone will accept some invalid FEN — always validate first when accepting user input.

- **Check/checkmate suffixes added by caller** — `notation` output functions omit `+`/`#` suffixes. `Game::getHistory()` appends them by replaying moves on a temp board. This keeps notation logic pure (no need to apply moves to detect check).

- **Fixed-size arrays everywhere** — `MoveEntry[300]`, `HashHistory` (256 entries), `MoveList` (218 entries, stores `Move` structs), no `std::vector`. This is for ESP32: heap fragmentation from repeated vector growth is a real problem with 320KB RAM.

- **Search is a stateless namespace** — `search::findBestMove()` takes a `Position` (by ref), `SearchLimits`, and optional TT pointer. All per-search state (killers, history table, node counter, `staticEvals[]` for improving flag, triangular PV table) lives in `SearchState`, heap-allocated via `std::unique_ptr` in `findBestMove()` (~39 KiB — too large for the 16 KiB FreeRTOS task stack). The search does not own the position — it uses make/unmake (including `makeNullMove()`/`unmakeNullMove()` for NMP) directly on the passed-in position. This makes search safe to run from any context (FreeRTOS task, test, Engine facade). Both negamax and quiescence use fail-soft alpha-beta (return the actual best score, not clamped to window bounds). The negamax core uses mate distance pruning, check extensions, recapture extensions (SEE cached from move ordering), PVS, adaptive NMP (R scales with depth and eval surplus), logarithmic LMR table (history-informed, improving-aware, non-PV adjustment), history pruning (pre-make skip of deeply negative-history quiets), reverse futility pruning (margin/depth, halved when improving), history gravity (unified bonus/penalty via gravity formula), and lazy evaluation (material-only shortcut when score is far from the alpha/beta window); iterative deepening uses aspiration windows (gradual doubling on fail) and root move reordering; the principal variation is tracked via a triangular PV table and surfaced in `SearchResult::pv`. Quiescence search orders captures by MVV-LVA and skips non-pawn×defended-pawn captures (pawn-defended-pawn pruning).

- **Engine facade owns search infrastructure** — `Engine` owns a `Position`, `TranspositionTable`, and stop control. `calculateMove(fen, limits)` loads the FEN, wires stop flags, calls `findBestMove()`, and returns the structured `SearchResult`. Threading is the caller's responsibility (FreeRTOS task in `LibreChessProvider`). `setExternalStop()` wires an external cancellation flag (e.g. `ctx->cancel`) to `SearchLimits::stop` so the search cooperatively unwinds on cancellation.

- **TT entry is 12 bytes** — `TTEntry` stores `key32` (upper 32 bits of Zobrist), `int16_t score`, `PackedMove` (uint16_t from/to/flags), `int8_t depth`, `TTFlag`. Mate scores are adjusted relative to search ply on store/probe (`scoreToTT`/`scoreFromTT`) so TT entries are ply-independent. TT size is always rounded down to a power of two for fast modular indexing.

- **Move ordering during search** — four-tier ordering: TT move (30000) → MVV-LVA captures (10000 + victim*100 - attacker) → killer moves (9000/8000, 2 per ply) → history heuristic (depth² bonus, capped at 7000). `assignScores()` + `pickBest()` (selection sort — O(N) per move, avoids full sort since alpha-beta prunes most branches).

## Key Patterns

- **Dirty-flag caching**: `Game` caches FEN and evaluation, recomputes only when `fenDirty_`/`evalDirty_` are set by game-layer mutations. `Position` is a pure state container with no caching overhead.
- **Composition over inheritance**: `Game` composes `Position` + `History`. No inheritance hierarchy.
- **Nullable DI**: Storage, observer, and logger are pointer-injected. All nullable — storage and observer guard with `if (ptr_)`, logger uses `Log` proxy (no manual guards).
- **Compact 2-byte move encoding**: `encodeMove()`/`decodeMove()` — bits 15..10 = from (row*8+col), bits 9..4 = to, bits 3..0 = promo code. Used for binary storage.
- **Color-derived helpers**: `pawnDirection()`, `homeRow()`, `promotionRow()`, `~color` (opponent), `makePiece()` — in `piece`, use these instead of inline ternaries for color-dependent values.
- **Castling bit mapping**: `castlingCharToBit()` is the single source of truth for K/Q/k/q → bitmask. `hasCastlingRight()` wraps it with color+side semantics. All castling rights logic should use these.
- **MoveEntry factory**: `MoveEntry::build()` encapsulates captured-piece determination and all field assignments. Both `Game::makeMove()` and `History::replayInto()` use it.
- **Cohesive data types**: `MoveList` stores `Move[218]` + count — used by both per-piece `getPossibleMoves` (with UI adapter accessors `targetRow(i)`/`targetCol(i)`) and bulk `generateAllMoves`/`generateCaptures`. `Move` struct stores compact from/to/flags (3 bytes: capture, EP, castling, promotion + 2-bit promo piece type). Promotions emit 4 `Move` variants per target square (one per piece type). `ScoredMove` pairs a `Move` with `int16_t score` for future move ordering. `HashHistory` bundles Zobrist keys + count. `BitboardSet` bundles 12 piece + 2 color + 1 occupancy bitboards. All defined in `types.h` / `bitboard.h`. Game-management types (`GameHeader`, recording constants) live in `lib/game/src/types.h`. `GameHeader` contains an opaque `meta[GAME_META_SIZE]` byte array — firmware defines the semantic overlay (`GameModeId`, difficulty) in `game_mode.h`; the library stores and returns these bytes without interpretation.
- **Bitboard serialization via popLsb**: iterating pieces uses `while (bb) { sq = popLsb(bb); ... }` — the standard pattern for extracting set bits from a bitboard one at a time.
- **Attack detection via bitwise AND**: `attacks::isSquareUnderAttack` checks `attacks::KNIGHT[sq] & bb.byPiece[knightIdx]` etc. — a single AND per piece type replaces ray-walking loops.
- **Pin-aware move generation**: `movegen::getPossibleMoves`, `movegen::hasAnyLegalMove`, `generateAllMoves`, and `generateCaptures` compute a `checkMask` and `PinData` (up to 8 pins, one per direction) once per call. Non-king/non-EP moves are filtered via bitwise AND against `pinRayFor(pinData, sq) & checkMask` — no `leavesInCheck` call needed. Only king moves and EP captures still use copy-make (`leavesInCheck`). X-ray helpers (`xrayRook`, `xrayBishop`, `between`) in `attacks` support pin detection. File-local helpers in `movegen.cpp + rules.cpp` anonymous namespace: `PinData` struct, `pinRayFor`, `computePinData`. `attacks::attackersOfSquare()` computes the full attacker bitboard for check detection; `attacks::isSquareUnderAttack()` is a thin wrapper.
- **Copy-make for legality check**: `leavesInCheck()` copies `BitboardSet` (~120 bytes), applies the move on the copy, and checks the king square. Used for king moves, EP captures, and `isValidMove`. Lightweight because bitboard copy is a flat struct copy.

## Completion Checklist

Every change to `lib/core/`, `lib/game/`, or `lib/engine/` MUST include these steps before the work is considered done. Do not defer any of them to a follow-up.

1. **Tests** — add or update unit tests in the appropriate test suite (`test/test_core/`, `test/test_game/`, or `test/test_engine/`) covering the changed behavior. New public APIs, new structs, renamed parameters, moved functions, and new internal state (like caches) all need test coverage. Register new test functions in the suite's `test_all.cpp`.
2. **Scoped instructions** — if the change affects anything described in this file (`core-library.instructions.md`), update it: Component Roles table, Fundamental Concepts table, Data Flow steps, Design Decisions, Key Patterns.
3. **Architecture doc** — if the change affects class responsibilities, public APIs, internal state, or component relationships described in `docs/development/architecture.md`, update the relevant section.
4. **Project structure doc** — if files are added, removed, or their purpose changes, update `docs/development/project-structure.md`.
5. **Testing instructions** — if new test groups are added or existing groups change scope, update `.github/instructions/testing.instructions.md` (file structure listing, file mirroring table, test group descriptions).
6. **Top-level instructions** — if the change introduces a new engineering pattern or convention, update `.github/copilot-instructions.md`.
