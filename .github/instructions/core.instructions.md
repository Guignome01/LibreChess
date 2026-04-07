---
applyTo: "lib/core/**"
description: "Core chess library: shared conventions, dependency model, and cross-cutting patterns for all files in lib/core/. Per-file details live in dedicated instruction files."
---

# Core Library (`lib/core/`) — General

Board representation, move generation, evaluation, notation, FEN, EPD parsing — the foundation layer with zero Arduino dependencies. Dependency model: `core ← game`, `core ← engine`. Core never imports game or engine.

Pure C++ — uses `std::string` (not Arduino `String`); firmware bridges with `.c_str()` / `std::string()`.

## Per-File Instruction Files

Detailed API, design decisions, and patterns for each module live in dedicated files (auto-loaded via `applyTo` when editing the corresponding source):

| File | Covers |
|------|--------|
| `position.instructions.md` | `Position` — state, move execution, game-end detection, incremental tracking |
| `evaluation.instructions.md` | `eval` — tapered evaluation, PSTs, pawn hash, eval hash |
| `movegen.instructions.md` | `movegen` — staged generation, LegalityContext, pin-aware filtering |
| `attacks.instructions.md` | `attacks` — leaper tables, HQ sliders, AttackInfo, SEE |
| `notation.instructions.md` | `notation` — SAN/LAN/coordinate conversion |
| `fen.instructions.md` | `fen` — parse/serialize/validate |
| `zobrist.instructions.md` | `zobrist` — constexpr key gen, incremental hashing |
| `epd.instructions.md` | `epd` — EPD parser |
| `trace.instructions.md` | `trace` — tuning trace extraction |
| `core-headers.instructions.md` | `piece.h`, `utils.h`, `bitboard.h`, `types.h`, `move.h`, `logger.h` |

## Cross-Cutting Design Rules

- **Decomposed parameters prevent circular dependencies** — functions in `movegen`, `notation`, `attacks`, `eval`, and `zobrist` accept `(const BitboardSet&, const Piece[], const PositionState&)` instead of `const Position&`. This prevents circular header dependencies: `movegen.h` cannot include `position.h` because `position.cpp` includes `movegen.h`.

- **Position has no lifecycle state** — `gameOver_`, `gameResult_`, `winnerColor_` live in `Game`, not `Position`. Position is a replayable position container.

- **Stateless namespaces** — `movegen`, `notation`, `eval`, `attacks`, `zobrist`, `fen`, `epd`, `trace` are all stateless. All context passed as parameters. Safe to call from any context.

- **Standalone hash tables** — `TranspositionTable`, `PawnHashTable`, and `EvalHashTable` are independent structs. No template base (different entry types/policies, only 3 instances).

- **Fixed-size arrays** — `MoveEntry[300]`, `HashHistory(128)`, `MoveList(218)`, no `std::vector`. ESP32 heap fragmentation constraint.

## Cross-Cutting Patterns

- **Color-derived helpers**: `pawnForward()`, `homeRank()`, `promotionRank()`, `pawnStartRank()` (LERF-native), `~color` (opponent), `makePiece()` — use these, not inline ternaries.
- **Cohesive data types**: `MoveList` (Move[218] + count), `Move` (3-byte from/to/flags), `MoveResult` (packed flags + accessors), `MoveEntry` (MR_* flags, factory `build()`), `HashHistory` (keys + count), `BitboardSet` (12+2+1 bitboards).
- **Bitboard serialization**: `while (bb) { sq = popLsb(bb); ... }` for extracting set bits.
- **Check/checkmate suffixes added by caller** — `notation` omits `+`/`#`. `Game::getHistory()` appends them via replay.

## Completion Checklist

Every change to `lib/core/` MUST include:

1. **Tests** — add/update in `test/test_core/`, register in `test_all.cpp`
2. **Per-file instruction file** — update the relevant per-file `.instructions.md` if API, design decisions, or patterns change
3. **This file** — update `core.instructions.md` if cross-cutting conventions or the component listing change
4. **Architecture doc** — update `docs/development/architecture.md` if APIs/state/relationships change
5. **Project structure doc** — update `docs/development/project-structure.md` if files added/removed
6. **Testing instructions** — update `.github/instructions/testing.instructions.md` if test groups change
7. **Top-level instructions** — update `.github/copilot-instructions.md` if new patterns/conventions

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
| `trace.instructions.md` | Per-file — tuning trace extraction |
| `core-headers.instructions.md` | Per-file — piece, utils, bitboard, types, move, logger |
| `game-library.instructions.md` | Downstream consumer (`core ← game`) |
| `engine-library.instructions.md` | Downstream consumer (`core ← engine`) |
| `testing.instructions.md` | Test architecture and per-group details |
