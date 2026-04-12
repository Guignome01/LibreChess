---
applyTo: "lib/core/**"
description: "Core chess library: shared conventions, dependency model, and cross-cutting patterns for all files in lib/core/. Per-file details live in dedicated instruction files."
---

# Core Library (`lib/core/`) — General

Chess engine library: board representation, move generation, evaluation, search algorithm, UCI protocol, time management, notation, FEN, EPD parsing — with zero Arduino dependencies. Dependency model: `core ← game`. Core never imports game.

Pure C++ — uses `std::string` (not Arduino `String`); firmware bridges with `.c_str()` / `std::string()`.

## Per-File Instruction Files

Detailed API, design decisions, and patterns for each module live in dedicated files (auto-loaded via `applyTo` when editing the corresponding source):

| File | Covers |
|------|--------|
| `position.instructions.md` | `Position` — state, move execution, game-end detection, incremental tracking |
| `evaluation.instructions.md` | `eval` — tapered evaluation, PSTs, pawn hash, eval hash, shared feature extraction helpers |
| `movegen.instructions.md` | `movegen` — staged generation, LegalityContext, pin-aware filtering |
| `attacks.instructions.md` | `attacks` — leaper tables, HQ sliders, AttackInfo, SEE |
| `notation.instructions.md` | `notation` — SAN/LAN/coordinate conversion |
| `fen.instructions.md` | `fen` — parse/serialize/validate |
| `zobrist.instructions.md` | `zobrist` — constexpr key gen, incremental hashing |
| `epd.instructions.md` | `epd` — EPD parser |
| `trace.instructions.md` | `trace` — tuning infrastructure (`#ifdef TUNING`, compiles to nothing in production) |
| `core-headers.instructions.md` | `piece.h`, `utils.h`, `bitboard.h`, `types.h`, `move.h`, `logger.h`, `hash_table.h` |
| `search.instructions.md` | `search.h/cpp`, `move_picker.h`, `stats.h` — search algorithm, TT, MovePicker |
| `uci.instructions.md` | `uci.h/cpp` — UCI protocol handler, UCIState resource bundle |
| `time-management.instructions.md` | `time_management.h` — time control computation |

## Cross-Cutting Design Rules

- **Decomposed parameters prevent circular dependencies** — functions in `movegen`, `notation`, `attacks`, `eval`, and `zobrist` accept `(const BitboardSet&, const Piece[], const PositionState&)` instead of `const Position&`. This prevents circular header dependencies: `movegen.h` cannot include `position.h` because `position.cpp` includes `movegen.h`.

- **Position has no lifecycle state** — `gameOver_`, `gameResult_`, `winnerColor_` live in `Game`, not `Position`. Position is a replayable position container.

- **Stateless namespaces** — `movegen`, `notation`, `eval`, `attacks`, `zobrist`, `fen`, `epd` are all stateless. All context passed as parameters. Safe to call from any context. (`trace.h/cpp` is also in the `eval` namespace but guarded by `#ifdef TUNING` — compiles to nothing in production.)

- **Standalone hash tables** — `TranspositionTable`, `PawnHashTable`, and `EvalHashTable` inherit `HashTableBase<Entry>` from `hash_table.h` for common `resize`/`free`/`clear`. Each adds its own `probe`/`store` and any extra state (e.g. TT adds `generation`). Three instances with shared base, specialized behavior.

- **Search is a stateless namespace** — `search::findBestMove()` takes `Position&`, `SearchLimits`, `SearchState&` (required), optional `InfoCallback`. Infrastructure pointers (`timeFunc`, `tt`, `pawnHash`, `evalHash`) are set via `SearchState` constructor. All per-search state in `SearchState`. Safe to run from any context.

- **Game optionally owns search infrastructure** — `Game::initSearch()` allocates TT, PawnHash, EvalHash, SearchState on heap. `Game::calculateMove()` calls `findBestMove()` on its own Position. Only initialized for bot mode; player-only games skip it entirely.

- **UCI is a stateless dispatcher** — `uci::UCIState` owns Position + TT + hash tables + SearchState for the CLI path. `uci::loop()` / `uci::processLine()` are free functions. No classes, no inheritance.

- **Platform time abstraction** — `TimeFunc` function pointer for `millis()` (ESP32) vs `nativeMillis()` (native tests).

- **Fixed-size arrays** — `MoveEntry[300]`, `HashHistory(128)`, `MoveList(218)`, no `std::vector`. ESP32 heap fragmentation constraint.

## Cross-Cutting Patterns

- **Color-derived helpers**: `pawnForward()`, `homeRank()`, `promotionRank()`, `pawnStartRank()` (LERF-native), `~color` (opponent), `makePiece()` — use these, not inline ternaries.
- **Cohesive data types**: `MoveList` (Move[218] + count), `Move` (3-byte from/to/flags), `MoveResult` (packed flags + accessors), `MoveEntry` (MR_* flags, factory `build()`), `HashHistory` (keys + count), `BitboardSet` (12+2+1 bitboards).
- **Bitboard serialization**: `while (bb) { sq = popLsb(bb); ... }` for extracting set bits.
- **Check/checkmate suffixes added by caller** — `notation` omits `+`/`#`. `Game::getHistory()` appends them via replay.

## Completion Checklist

Every change to `lib/core/` MUST include:

1. **Tests** — add/update in `test/test_core/`, register in `test_all.cpp`
2. **Regression tests** — run `test_benchmarks` to verify node count and eval regression baselines are not exceeded (applies to evaluation, movegen, position, and attacks changes)
3. **Per-file instruction file** — update the relevant per-file `.instructions.md` if API, design decisions, or patterns change
4. **This file** — update `core.instructions.md` if cross-cutting conventions or the component listing change
5. **Architecture doc** — update `docs/development/architecture.md` if APIs/state/relationships change
6. **Project structure doc** — update `docs/development/project-structure.md` if files added/removed
7. **Testing instructions** — update `.github/instructions/testing.instructions.md` if test groups change
8. **Top-level instructions** — update `.github/copilot-instructions.md` if new patterns/conventions

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `position.instructions.md` | Per-file — Position class |
| `evaluation.instructions.md` | Per-file — tapered evaluation |
| `movegen.instructions.md` | Per-file — legal move generation |
| `attacks.instructions.md` | Per-file — attack tables, SEE |
| `notation.instructions.md` | Per-file — SAN/LAN/coordinate |
| `fen.instructions.md` | Per-file — FEN parse/serialize |
| `zobrist.instructions.md` | Per-file — Zobrist hashing |
| `epd.instructions.md` | Per-file — EPD parser |
| `trace.instructions.md` | Per-file — tuning trace extraction + registry (`#ifdef TUNING`) |
| `core-headers.instructions.md` | Per-file — piece, utils, bitboard, types, move, logger, hash_table |
| `search.instructions.md` | Per-file — search algorithm, MovePicker, SearchState |
| `uci.instructions.md` | Per-file — UCI protocol handler |
| `time-management.instructions.md` | Per-file — time control computation |
| `game-library.instructions.md` | Downstream consumer (`core ← game`) |
| `testing.instructions.md` | Test architecture and per-group details |
