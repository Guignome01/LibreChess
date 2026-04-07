---
applyTo: "lib/game/**"
description: "Game library: shared conventions, dependency model, and cross-cutting patterns for all files in lib/game/. Per-file details live in dedicated instruction files."
---

# Game Library (`lib/game/`) — General

Central game orchestrator composing `Position` (from core), `History` (in-memory move log + persistent recording), and optionally `IGameObserver`. All chess-state mutations flow through `Game`. Dependency: `core ← game`. Game never imports engine.

Pure C++ — uses `std::string` (not Arduino `String`); firmware bridges with `.c_str()` / `std::string()`.

## Per-File Instruction Files

| File | Covers |
|------|--------|
| `game.instructions.md` | `Game` — orchestrator, dual overloads, re-exported APIs, data flow |
| `history.instructions.md` | `History` — move log, persistent recording, branch-on-undo |
| `game-headers.instructions.md` | `types.h`, `storage.h` (IGameStorage), `observer.h` (IGameObserver) |

## Cross-Cutting Design Rules

- **Game is the only firmware entry point** — firmware (`src/`) must never include `Position`, `History`, `movegen`, `piece`, or `utils` directly. `Game` re-exports all necessary query and utility wrappers. Native tests may include internal `lib/core/` headers.

- **Undo clears game-over** — `undoMove()` re-opens a finished game for web UI navigation. Code must re-check `isGameOver()` after undo.

- **Composition over inheritance** — `Game` composes `Position` + `History`. No inheritance hierarchy.

- **Nullable DI** — Storage, observer, and logger are pointer-injected. All nullable — storage/observer guard with `if (ptr_)`, logger uses `Log` proxy (no manual guards).

## Data Flow: How a Move Works

1. **Firmware** calls `Game::makeMove(from, to, promotion)` — the only entry point for moves
2. **Game** delegates to `Position::makeMove()`, which validates, mutates, and detects game-end
3. **Game** logs the move description via `ILogger`
4. **Game** records the move in `History` (which auto-persists if recording)
5. **Game** checks threefold repetition (Zobrist comparison), auto-ends game if detected
6. **Game** logs game-end events via `ILogger`, or logs check/turn if game continues
7. **Game** notifies `IGameObserver` with updated FEN + evaluation
8. **Game** auto-saves recording if the game just ended

**Key invariant**: Steps 2–8 are atomic from the caller's perspective.

## Completion Checklist

Every change to `lib/game/` MUST include:

1. **Tests** — add/update in `test/test_game/`, register in `test_all.cpp`
2. **Per-file instruction file** — update the relevant per-file `.instructions.md` if API, design decisions, or patterns change
3. **This file** — update `game-library.instructions.md` if cross-cutting conventions change
4. **Architecture doc** — update `docs/development/architecture.md` if APIs/state/relationships change
5. **Project structure doc** — update `docs/development/project-structure.md` if files added/removed
6. **Testing instructions** — update `.github/instructions/testing.instructions.md` if test groups change
7. **Top-level instructions** — update `.github/copilot-instructions.md` if new patterns/conventions

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game.instructions.md` | Per-file — Game orchestrator |
| `history.instructions.md` | Per-file — History move log + recording |
| `game-headers.instructions.md` | Per-file — types.h, storage.h, observer.h |
| `core.instructions.md` | Upstream dependency (`core ← game`) |
| `engine-library.instructions.md` | Sibling library (no direct dependency) |
| `testing.instructions.md` | Test architecture and per-group details |
