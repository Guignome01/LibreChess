---
applyTo: "" # lib/engine/ was deleted — search/TT/MovePicker/stats/Engine all live in lib/core/
description: "[DEPRECATED] Engine library instructions. The lib/engine/ directory was removed; search, TT, MovePicker, stats, and Engine facade are in lib/core/src/."
---

# Engine Library (`lib/engine/`) — DEPRECATED

> **This library was deleted.** All search, TT, MovePicker, stats, and Engine facade files live in `lib/core/src/`. See `search.instructions.md` and `engine-facade.instructions.md` for current documentation.

## Per-File Instruction Files

| File | Covers |
|------|--------|
| `search.instructions.md` | `search.h/cpp` — negamax, alpha-beta, quiescence, SearchState, technique reference |
| `search.instructions.md` | `move_picker.h` — MovePicker, heuristic updates (same instruction file) |
| `search.instructions.md` | TT types (TTFlag, PackedMove, TTEntry, TranspositionTable) — now in `search.h` (same instruction file) |
| `search.instructions.md` | `stats.h` — SearchStats, STAT_INC (same instruction file) |
| `engine-facade.instructions.md` | `Engine` — facade owning Position + TT, calculateMove API, stop control |

## Cross-Cutting Design Rules

- **Search is a stateless namespace** — `search::findBestMove()` takes `Position&`, `SearchLimits`, `SearchState&` (required), optional `InfoCallback`. Infrastructure fields (`timeFunc`, `tt`, `pawnHash`, `evalHash`) are set on `SearchState` by the caller before calling. All per-search state in `SearchState`. Safe to run from any context.

- **Engine facade owns infrastructure** — `Engine` owns Position + TT + SearchState (direct member) + stop control. Thin wrapper around `findBestMove()`.

- **Dependency: core only** — engine imports from `lib/core/` only. Never imports game.

- **Platform time abstraction** — `TimeFunc` function pointer for `millis()` (ESP32) vs `nativeMillis()` (native tests).

## Completion Checklist

Every change to `lib/engine/` MUST include:

1. **Tests** — add/update in `test/test_engine/`, register in `test_all.cpp`
2. **Regression tests** — run `test_benchmarks` to verify node count and eval regression baselines are not exceeded
3. **Per-file instruction file** — update the relevant per-file `.instructions.md` if search techniques, facade API, or patterns change
4. **This file** — update `engine-library.instructions.md` if cross-cutting conventions change
5. **Architecture doc** — update `docs/development/architecture.md` if APIs/state/relationships change
6. **Project structure doc** — update `docs/development/project-structure.md` if files added/removed
7. **Testing instructions** — update `.github/instructions/testing.instructions.md` if test groups change
8. **Top-level instructions** — update `.github/copilot-instructions.md` if new patterns/conventions

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `search.instructions.md` | Per-file — search algorithm, MovePicker, SearchState |
| `engine-facade.instructions.md` | Per-file — Engine facade, calculateMove API |
| `core.instructions.md` | Upstream dependency (`core ← engine`) |
| `game-library.instructions.md` | Sibling library (no direct dependency) |
| `testing.instructions.md` | Test architecture and per-group details |
