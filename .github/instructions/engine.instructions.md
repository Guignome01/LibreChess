---
applyTo: "src/engine/**"
---

# Engine Provider Architecture

## Overview

`EngineProvider` (abstract base) → `StockfishProvider` | `LichessProvider` | `LibreChessProvider`. Each provider handles all computation/communication in FreeRTOS background tasks and returns data only — providers never touch `Game`, `BoardDriver`, or any hardware.

Providers are composed into `BotMode` via pointer injection (`BotMode` owns the pointer).

## EngineProvider (base class)

### Lifecycle
1. `initialize(result)` — called once during `BotMode::begin()`. May block (HTTP calls). Returns `false` on failure.
2. `requestMove(fen)` — spawns a background FreeRTOS task. Non-blocking.
3. `checkResult(result)` — polls for task completion. Non-blocking. Returns `true` when result is ready.
4. `cancelRequest()` — sets `cancel` flag, waits up to 2s for task to finish, then deletes context.

### Logger
`EngineProvider` accepts an optional `ILogger*` via constructor. The logger is stored as `logger_` (`Log` proxy, protected) and automatically propagated to `BaseTaskContext::logger` in `spawnTask()`. FreeRTOS tasks use `ctx->logger` for thread-safe logging. The `Log` proxy handles null internally — no manual null guards needed.

### FreeRTOS Task Helpers (protected)
- `spawnTask(ctx, name, taskFn, stackSize)` — cancels any running task, stores context, creates FreeRTOS task.
- `pollResult(result)` — checks `ready` flag, copies result, **deletes** context. Use when no extra fields needed.
- `peekResult(result)` — like `pollResult` but does NOT delete context. Caller reads provider-specific fields from the derived `TaskContext`, then calls `finishTask()`.
- `finishTask()` — deletes context after `peekResult()`. **Must** be called if `peekResult()` returned `true`.

### Optional Hooks
- `onPlayerMoveApplied(moveCoord)` — called after a local move is applied. Lichess sends the move to the server. Returns `false` on failure.
- `onResignConfirmed()` — called after resign. Lichess resigns on server.
- `getEvaluation()` — returns engine eval in centipawns (`int`) for web UI. Default 0 (BotMode falls back to material count).

## StockfishProvider

One-shot HTTP provider. Constructor: `StockfishProvider(settings, playerColor, logger)`. Forwards `logger` to `EngineProvider`. Each `requestMove()` spawns a FreeRTOS task that calls the Stockfish API, parses the JSON response, and stores the best move + centipawn evaluation. `checkResult()` uses `pollResult()` (no extra fields beyond base). `initialize()` always succeeds (no server handshake needed).

Configuration via `StockfishSettings` — `depth` (1–15), `timeoutMs`, `maxRetries`. Has 8 named presets (`beginner` through `master`) and a `fromLevel(1–8)` factory.

## LichessProvider

Streaming provider. Constructor: `LichessProvider(config, logger)`. Forwards `logger` to `EngineProvider`. Holds a `LichessAPI api_` instance member for main-thread calls. `initialize()` blocks during game discovery (token verification + active game search). `requestMove()` spawns a persistent NDJSON streaming task that reads opponent moves and game-end events. The `TaskContext` carries a `LichessConfig` copy by value; the task creates a local `LichessAPI(ctx->config, ctx->logger)` instance for thread-safe stream operations. `checkResult()` uses `peekResult()` + `finishTask()` to read extra fields from the derived context. `onPlayerMoveApplied()` sends moves to Lichess via HTTP POST.

Configuration via `LichessConfig` — just an OAuth `apiToken`.

## LibreChessProvider

On-board engine provider. Constructor: `LibreChessProvider(depth, moveTimeMs, playerColor, logger)`. Forwards `logger` to `EngineProvider`. Uses the `Engine` facade from `lib/engine/` — no network required. `initialize()` always succeeds (no handshake needed), sets `mode = GameModeId::BOT`, `canResume = true`.

Each `requestMove()` spawns a FreeRTOS task (16 KiB stack) that:
1. Sizes the TT based on available heap (`heap_caps_get_free_size / 4`, capped at 128 KiB, minimum 64 entries)
2. Creates an `Engine` instance with the computed TT size, sets `millis` as the time function
3. Wires `ctx->cancel` → `engine.setExternalStop()` for cooperative cancellation
4. Builds `SearchLimits` (depth and/or moveTime) and calls `engine.calculateMove(fen, limits)`
5. Extracts best move coordinate via `notation::toCoordinate()` and evaluation from `SearchResult`

`checkResult()` uses `peekResult()` + `finishTask()` to read the evaluation before cleanup. `getEvaluation()` returns the last search score for the web UI eval bar.

## API Layer

Each provider has a companion API module in its subdirectory:
- `stockfish/stockfish_api.h/.cpp` — static utility class. Pure parsing (builds request URLs, parses JSON responses). No state, no logging, no network I/O.
- `lichess/lichess_api.h/.cpp` — instance class. Constructor: `LichessAPI(config, logger)`. Holds a `const LichessConfig&` and `ILogger*`. Handles all Lichess HTTP requests (game stream, move submission, resign, game discovery) using `config_.apiToken` for auth. Token management (set/get/has) was removed — the token lives solely in `LichessConfig`.

API modules handle raw HTTP + TLS. Providers handle chess-domain logic and FreeRTOS lifecycle.

## Design Decisions

- **Providers never touch hardware** — providers return `EngineResult` structs only. All LED, sensor, and animation logic stays in `BotMode`. This means providers can be tested or replaced without any hardware dependency, and `BotMode` controls the full user interaction sequence.

- **Heap-allocated task contexts** — `BaseTaskContext` is always `new`'d before `spawnTask()` and `delete`'d in `pollResult()`/`finishTask()`. Never stack-allocate: the FreeRTOS task outlives the spawning function's scope. The base class destructor handles cleanup if the provider is destroyed mid-task.

- **One active task at a time** — `spawnTask()` cancels any existing task before starting a new one. This simplifies state management: there's never ambiguity about which result is current. It also means `cancelRequest()` is always safe to call redundantly.

- **Cooperative cancellation with timeout** — tasks check `ctx->cancel` periodically and exit early. The 2s timeout in `cancelRequest()` is a safety net for tasks stuck in blocking HTTP calls. If the task doesn't finish in 2s, the context is deleted anyway (the orphaned task will crash on its next context access, but this is preferred over a deadlock).

- **`peekResult()` vs `pollResult()`** — `pollResult()` deletes the context immediately. `peekResult()` lets the caller read provider-specific fields from the derived `TaskContext` first. Lichess needs this to extract `lastKnownMoveCount` before the context is freed. Stockfish only needs the base `EngineResult`, so it uses `pollResult()`.

- **Lichess sets `canResume = false`** — Lichess game state comes from the server, not from flash. If the device reboots mid-game, `initialize()` re-discovers the active game from the Lichess API. Stockfish sets `canResume = true` because the game state is local.

- **Lichess reconnects with exponential backoff** — on stream disconnect, the task retries with 1s→2s→4s→8s delays, up to 5 attempts. The game pauses (player can still interact with the board) during reconnection. If all attempts fail, the game is aborted rather than left in a broken state.
