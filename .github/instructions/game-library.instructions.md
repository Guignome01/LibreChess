---
applyTo: "lib/game/**"
description: "Game library: shared conventions, dependency model, and cross-cutting patterns for all files in lib/game/. Per-file details live in dedicated instruction files."
---

# Game Library (`lib/game/`) — General

Central game orchestrator composing `Position` (from core), `History` (in-memory move log + persistent recording), and optionally `IGameObserver`. Optionally composes an `Engine` (from core) for bot-mode search. The library also owns the pure `provider.h` engine contract shared by firmware game modes. All chess-state mutations flow through `Game`. Dependency: `core ← game`.

Pure C++ — uses `std::string` (not Arduino `String`); firmware bridges with `.c_str()` / `std::string()`.

## Per-File Instruction Files

| File | Covers |
|------|--------|
| `game.instructions.md` | `Game` — orchestrator, dual overloads, re-exported APIs, data flow |
| `history.instructions.md` | `History` — move log, persistent recording, branch-on-undo |
| `game-headers.instructions.md` | `types.h`, `storage.h` (IGameStorage), `observer.h` (IGameObserver) |
| `provider.h` | Pure `EngineProvider` contract/data; no firmware, Arduino, FreeRTOS, or board dependencies |

## Cross-Cutting Design Rules

- **Game is the only firmware entry point** — firmware (`src/`) must never include `Position`, `History`, `movegen`, `piece`, or `utils` directly. `Game` re-exports all necessary query and utility wrappers. Native tests may include internal `lib/core/` headers.

- **Undo clears game-over** — `undoMove()` re-opens a finished game for web UI navigation. Code must re-check `isGameOver()` after undo.

- **Composition over inheritance** — `Game` composes `Position` + `History`. No inheritance hierarchy.

- **Recording replay is byte-aligned, not pointer-aligned** — persisted move data is a byte stream. `History::replayInto()` must decode entries with byte copies so ESP32 resume never depends on `uint8_t` buffer alignment.

- **Snapshot search** — bot-mode search enters through `Game::calculateMove()`, which copies the current `Position` and searches the copy. Do not let firmware/background tasks run search make/unmake recursion on the live `board_` instance.

- **Snapshot candidate scoring/ranking** — UI assistance that needs a quick fallback can use `Game::scoreCandidateMove()`. Stronger lifted-piece assistance uses `Game::rankCandidateTargets()`, which searches only the requested legal target squares on a private snapshot and reports side-to-move-relative scores without mutating history, observers, or caches.

- **Non-throwing search allocation** — `Game::initSearch()` must tolerate ESP32 heap pressure. It uses `new(std::nothrow)`, logs allocation failures, and exposes search/hash diagnostics so firmware can continue with fallback behavior.

- **Nullable DI** — Storage, observer, and logger are pointer-injected. All nullable — storage/observer guard with `if (ptr_)`, logger uses `Log` proxy (no manual guards).

- **Provider contract stays pure** — `provider.h` may expose game-layer data such as `GameResult`, but it must not interpret firmware metadata. `EngineInitResult::mode` is an opaque byte that firmware maps to `GameModeId`/`GameMeta` in `src/game_mode/`.

- **Disambiguate core `engine.h`** — firmware engine folders use local `engine.h` filenames. When `lib/game` needs the core search facade, include it explicitly as `../../core/src/engine.h` so PlatformIO include paths cannot resolve to a firmware engine header.

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
| `engine.instructions.md` | Firmware engines implementing the pure `EngineProvider` contract |
| `core.instructions.md` | Upstream dependency (`core ← game`) |
| `testing.instructions.md` | Test architecture and per-group details |
