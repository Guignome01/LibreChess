---
applyTo: "lib/engine/**"
description: "Engine library: shared conventions, dependency model, and cross-cutting patterns for all files in lib/engine/. Per-file details live in dedicated instruction files."
---

# Engine Library (`lib/engine/`) — General

On-board chess engine: search algorithm and `Engine` facade. Dependency: `core ← engine`. Engine never imports game.

Pure C++ — uses `std::string` (not Arduino `String`); firmware bridges with `.c_str()` / `std::string()`.

## Per-File Instruction Files

| File | Covers |
|------|--------|
| `search.instructions.md` | `search` — negamax, alpha-beta, quiescence, MovePicker, SearchState, technique reference, stats.h |
| `engine-facade.instructions.md` | `Engine` — facade owning Position + TT, calculateMove API, stop control |

## Cross-Cutting Design Rules

- **Search is a stateless namespace** — `search::findBestMove()` takes `Position&`, `SearchLimits`, optional TT/hash pointers. All per-search state in `SearchState`. Safe to run from any context.

- **Engine facade owns infrastructure** — `Engine` owns Position + TT + stop control. Thin wrapper around `findBestMove()`.

- **Dependency: core only** — engine imports from `lib/core/` only. Never imports game.

- **Platform time abstraction** — `TimeFunc` function pointer for `millis()` (ESP32) vs `nativeMillis()` (native tests).

## Completion Checklist

Every change to `lib/engine/` MUST include:

1. **Tests** — add/update in `test/test_engine/`, register in `test_all.cpp`
2. **Per-file instruction file** — update the relevant per-file `.instructions.md` if search techniques, facade API, or patterns change
3. **This file** — update `engine-library.instructions.md` if cross-cutting conventions change
4. **Architecture doc** — update `docs/development/architecture.md` if APIs/state/relationships change
5. **Project structure doc** — update `docs/development/project-structure.md` if files added/removed
6. **Testing instructions** — update `.github/instructions/testing.instructions.md` if test groups change
7. **Top-level instructions** — update `.github/copilot-instructions.md` if new patterns/conventions

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `search.instructions.md` | Per-file — search algorithm, MovePicker, SearchState |
| `engine-facade.instructions.md` | Per-file — Engine facade, calculateMove API |
| `core.instructions.md` | Upstream dependency (`core ← engine`) |
| `game-library.instructions.md` | Sibling library (no direct dependency) |
| `testing.instructions.md` | Test architecture and per-group details |
