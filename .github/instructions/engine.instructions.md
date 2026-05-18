---
applyTo: "src/engines/**"
description: "Firmware engine integrations: pure EngineProvider contract, AsyncEngineProvider task helper, StockfishEngine, LichessEngine, LibreChessEngine, colocated assistance providers."
---

# Engine Integration Architecture

## Overview

`EngineProvider` is the pure game-layer contract in `lib/game/src/provider.h`.
Firmware integrations under `src/engines/**` derive from `AsyncEngineProvider`
when they need FreeRTOS task lifecycle helpers. Concrete opponent engines are
`StockfishEngine`, `LichessEngine`, and `LibreChessEngine`. Each engine handles
computation/communication and returns data only. Engines never touch board
programs, `BoardDriver`, or any hardware; `LibreChessEngine` is the exception
that receives a non-owning `Game*` for on-board search through public Game APIs.
Best-move assistance providers are colocated with the engine they use, such as
`src/engines/librechess/assistance.*`, and implement the board-owned
`BoardAssistanceProvider` interface without owning LEDs/sensors.

Providers are composed into `BotMode` via pointer injection (`BotMode` owns the pointer).

Assistance providers are installed on `Board` separately from the opponent
provider. This allows combinations such as playing against Stockfish while using
LibreChess for hints, or playing against LibreChess with legal-move-only
assistance. Disabled and legal-move assistance are board-owned fixed providers
and must not call an engine.

## EngineProvider (pure contract)

`lib/game/src/provider.h` owns the firmware-neutral data contract:
`DifficultyLevel`, `EngineInitResult`, `EngineResult`, and abstract
`EngineProvider`. It may include game-layer types such as `GameResult`, but it
must not include Arduino, FreeRTOS, WiFi, board programs, or firmware metadata
types. `EngineInitResult::mode` is an opaque `uint8_t`; firmware maps it to
`GameModeId`/`GameMeta` at the `src/game_mode/` boundary.

### Lifecycle
1. `initialize(result)` — called once during `BotMode::begin()`. May block (HTTP calls). Returns `false` on failure.
2. `requestMove(fen)` — spawns a background FreeRTOS task. Non-blocking.
3. `checkResult(result)` — polls for task completion. Non-blocking. Returns `true` when result is ready.
4. `cancelRequest()` — sets `cancel` flag, waits up to 2s for task to finish, then deletes context.

### Logger
`AsyncEngineProvider` accepts an optional `ILogger*` via constructor. The logger
is stored as `logger_` (`Log` proxy, protected) and automatically propagated to
`BaseTaskContext::logger` in `spawnTask()`. FreeRTOS tasks use `ctx->logger` for
thread-safe logging. The `Log` proxy handles null internally — no manual null
guards needed.

## AsyncEngineProvider

`src/engines/async_provider.h` owns the firmware task mechanics that used to be
mixed into the provider contract. It derives from `EngineProvider` and exposes
the shared FreeRTOS helpers to concrete engines.

### FreeRTOS Task Helpers
- `spawnTask(ctx, name, taskFn, stackSize)` — cancels any running task, stores context, creates FreeRTOS task.
- `pollResult(result)` — checks `ready` flag, copies result, **deletes** context. Use when no extra fields needed.
- `peekResult(result)` — like `pollResult` but does NOT delete context. Caller reads provider-specific fields from the derived `TaskContext`, then calls `finishTask()`.
- `finishTask()` — deletes context after `peekResult()`. **Must** be called if `peekResult()` returned `true`.

### Optional Hooks
- `onPlayerMoveApplied(moveCoord)` — called after a local move is applied. Lichess sends the move to the server. Returns `false` on failure.
- `onResignConfirmed()` — called after resign. Lichess resigns on server.
- `getEvaluation()` — returns engine eval in centipawns (`int`) for web UI. Default 0 (BotMode falls back to material count).

## StockfishEngine

One-shot HTTP engine. Constructor: `StockfishEngine(level, playerColor, logger)`. Level (1–8) selects from `StockfishEngine::LEVELS[8]` (depths 6–16, matching the stockfish.online API's valid range). Timeout scales with depth. Forwards `logger` to `AsyncEngineProvider`. Each `requestMove()` spawns a FreeRTOS task that calls the Stockfish API, parses the JSON response, and stores the best move + centipawn evaluation. `checkResult()` uses `pollResult()` (no extra fields beyond base). `initialize()` always succeeds (no server handshake needed).

Configuration via `StockfishSettings` — `depth`, `timeoutMs`, `maxRetries`. Settings are computed internally from the difficulty level; the preset factory and named presets were removed.

## LichessEngine

Streaming engine. Constructor: `LichessEngine(config, logger)`. Forwards `logger` to `AsyncEngineProvider`. Holds a `LichessAPI api_` instance member for main-thread calls. `initialize()` blocks during game discovery (token verification + active game search). `requestMove()` spawns a persistent NDJSON streaming task that reads opponent moves and game-end events. The `TaskContext` carries a `LichessConfig` copy by value; the task creates a local `LichessAPI(ctx->config, ctx->logger)` instance for thread-safe stream operations. `checkResult()` uses `peekResult()` + `finishTask()` to read extra fields from the derived context. `onPlayerMoveApplied()` sends moves to Lichess via HTTP POST.

Configuration via `LichessConfig` — just an OAuth `apiToken`.

## LibreChessEngine

On-board engine integration. Constructor: `LibreChessEngine(game, level, playerColor, logger)`. Takes a `Game*` (non-owning — Game outlives provider). Level (1–8) selects from `LibreChessEngine::LEVELS[8]` (depths 1–8). Forwards `logger` to `AsyncEngineProvider`. Uses `Game::calculateMove()` — no network, no string serialization. `initialize()` always succeeds (no handshake needed), sets `mode = GameModeId::BOT`, `canResume = true`.

`initialize()` calls `game->initSearch(ttEntries)` with a heap-sized TT (capped at 64 KiB) and `game->setTimeFunc(millis)`. If another LibreChess-backed component has already initialized search resources on the same `Game`, initialization reuses them instead of re-running heap sizing. The search resources (TT, pawn hash, eval hash, SearchState) persist inside Game across moves — no per-move heap fragmentation. Heap sizing uses unified file-scope constants: `MIN_FREE_HEAP` (32 KiB), `EVAL_HASH_OVERHEAD` (12 KiB), `SEARCH_OVERHEAD` (16 KiB). After initialization, LibreChessEngine checks `Game` search/hash diagnostics and logs degraded heap-pressure cases while preserving fallback-move behavior.

Each `requestMove()` spawns a FreeRTOS task (64 KiB stack) that:
1. Wires `ctx->cancel` → `game->setExternalStop()` for cooperative cancellation
2. Builds `SearchLimits` (depth-based) and calls `game->calculateMove(limits)` — Game snapshots its current `Position` before delegating to the engine, so search make/unmake recursion never mutates the live board used by firmware/UI state
3. Extracts best move coordinate via `notation::toCoordinate()` and evaluation from `SearchResult`

`checkResult()` uses `peekResult()` + `finishTask()` to read the evaluation before cleanup. `getEvaluation()` returns the last search score for the web UI eval bar.

## LibreChessAssistanceProvider

`src/engines/librechess/assistance.*` implements `BoardAssistanceProvider` for
BEST_MOVE board assistance. It receives the lifted source square plus the
board-generated legal target list, initializes/reuses Game-owned search
resources, and runs one root-filtered search with a fixed 1 second budget via
resources, and runs one root-filtered search with a difficulty-scaled time/depth
budget via `Game::rankCandidateTargets()`. The provider remains synchronous from
`BoardGame`'s point of view, but the actual search runs inside a short-lived
`lcAssist` FreeRTOS task with a 64 KiB stack so negamax recursion never runs on
the Arduino loop stack. It returns a
`BoardMoveTargetRanking` for the best and worst destinations from the lifted
piece's legal targets, not a global best move for the whole position. If search
resources cannot be initialized or no searched scores are produced, it falls
back to `Game::scoreCandidateMove()` for one-ply static ranking. It is owned by
`Board`, not by `BotMode`, so it is independent from the engine currently
playing the opponent side.

## API Layer

Each provider has a companion API module in its subdirectory:
- `stockfish/api.h/.cpp` — static utility class. Pure parsing (builds request URLs, parses JSON responses). No state, no logging, no network I/O.
- `lichess/api.h/.cpp` — instance class. Constructor: `LichessAPI(config, logger)`. Holds a `const LichessConfig&` from `lichess/config.h` and `ILogger*`. Handles all Lichess HTTP requests (game stream, move submission, resign, game discovery) using `config_.apiToken` for auth. Token management (set/get/has) was removed — the token lives solely in `LichessConfig`.

API modules handle raw HTTP + TLS. Providers handle chess-domain logic and FreeRTOS lifecycle.

## Memory

`LibreChessEngine` runs the search in a FreeRTOS task (`lcTask`) with a 64 KiB stack. Search resources are owned by `Game` (allocated once via `initSearch()`) and persist for the game's lifetime (TT, hash tables, SearchState all reuse across moves). Major allocations:

- **SearchState** (~10 KiB: `history[2][6][64]` = 1.5 KiB piece-to history, `captureHistory[6][6][64]` = 4.5 KiB, `killers[48][2]` = 192 B via `PackedMove`, `countermoves[12][64]` = 1.5 KiB, `staticEvals[48]` = 96 B, PV table 48×24×2 = 2.3 KiB via `PackedMove`, `pvLength[48]` = 48 B) — **pre-allocated** in `Game::initSearch()`, reused across searches. `findBestMove()` resets `nodes`/`stopped` per search. Eliminates per-search heap alloc/free cycle.
- **Position snapshot** (~2.3 KiB with `HashHistory[256]`) — stack-local copy made by `Game::calculateMove()` so the FreeRTOS task searches a private board state while preserving repetition history.
- **Transposition table** — heap-allocated (`new TTEntry[]`), dynamically sized to available heap, capped at 64 KiB (`MAX_TT_BYTES`).
- **Pawn hash table** — 6 KiB (256 entries × 24B `PawnEntry`), heap-allocated by `Game::initSearch()`. Caches pawn structure MG/EG scores + passed pawn bitboards; ~92%+ hit rate.
- **Eval hash table** — 4 KiB (1024 entries × 4B `EvalEntry`), heap-allocated by `Game::initSearch()`. Caches full `evaluatePosition()` results. Compact 16-bit key.
- **Per-ply negamax** — ~1,500 B per ply (MovePicker with MoveList 658B + int16_t scores[218] 436B + other fields + PackedMove quietsSearched[32] + capturesSearched[32] 128B + UndoInfo + locals). Uses `int16_t` scores array. SEE cached in scores[] when reclassifying bad captures.
- **Per-ply quiescence** — ~600 B per ply (QSMoveList 390B + int16_t capScores[128] 256B + UndoInfo + locals).

Max depth 8 + extensions (~6) + 16 QS plies ≈ 45 KiB (fits in 64 KiB). See `docs/development/additional-topics.md` for the full budget breakdown.

BEST_MOVE assistance uses the same 64 KiB stack budget in its transient
`lcAssist` task. Do not call `Game::rankCandidateTargets()` directly from the
main loop or board gesture path: root-filtered assistance search can exceed the
Arduino loop stack.

## Design Decisions

- **Providers never touch hardware** — opponent providers return `EngineResult` structs, and assistance providers return board DTOs such as `BoardMoveTargetRanking` through the board assistance interface. All LED, sensor, and animation logic stays in the board subsystem. This means providers can be tested or replaced without any hardware dependency, and game modes control the flow without exposing hardware to providers.

- **Heap-allocated task contexts** — `BaseTaskContext` is always allocated with `new(std::nothrow)` before `spawnTask()` and `delete`'d in `pollResult()`/`finishTask()`. Never stack-allocate: the FreeRTOS task outlives the spawning function's scope. Allocation or `xTaskCreate()` failure publishes an immediate `EngineResult::NONE` so `BotMode` can retry/abort instead of waiting forever.

- **One active task at a time** — `spawnTask()` cancels any existing task before starting a new one. This simplifies state management: there's never ambiguity about which result is current. It also means `cancelRequest()` is always safe to call redundantly.

- **Local `engine.h` guards must be path-unique** — per-engine folders intentionally use role filenames such as `engine.h`, but header guards must include the folder path (`ENGINES_LIBRECHESS_ENGINE_H`, etc.). Do not reuse core guards such as `LIBRECHESS_ENGINE_H`; that blocks `lib/core/src/engine.h` from defining `LibreChess::Engine` when both headers are included.

- **Cooperative cancellation with timeout** — tasks check `ctx->cancel` periodically and exit early. The 2s timeout in `cancelRequest()` is a safety net for tasks stuck in blocking HTTP calls. If the task doesn't finish in 2s, the context is deleted anyway (the orphaned task will crash on its next context access, but this is preferred over a deadlock).

- **External search stop pointer lifetime** — any provider task that calls
	`Game::setExternalStop(&ctx->cancel)` must clear it with
	`Game::setExternalStop(nullptr)` immediately after `calculateMove()` /
	`rankCandidateTargets()` returns. Task contexts are heap-owned and deleted
	after completion, so leaving Engine's external stop pointer aimed at a task
	context creates a dangling pointer for the next search.

- **`peekResult()` vs `pollResult()`** — `pollResult()` deletes the context immediately. `peekResult()` lets the caller read provider-specific fields from the derived `TaskContext` first. Lichess needs this to extract `lastKnownMoveCount` before the context is freed. Stockfish only needs the base `EngineResult`, so it uses `pollResult()`.

- **Lichess sets `canResume = false`** — Lichess game state comes from the server, not from flash. If the device reboots mid-game, `initialize()` re-discovers the active game from the Lichess API. Stockfish sets `canResume = true` because the game state is local.

- **Lichess reconnects with exponential backoff** — on stream disconnect, the task retries with 1s→2s→4s→8s delays, up to 5 attempts. The game pauses (player can still interact with the board) during reconnection. If all attempts fail, the game is aborted rather than left in a broken state.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game.instructions.md` | `LibreChessEngine` calls `Game::calculateMove()` for search |
| `search.instructions.md` | References `SearchLimits`, `SearchResult`, `SearchState` sizes |
| `game-mode.instructions.md` | `BotMode` composes `EngineProvider*`, drives the thinking state machine |
| `core.instructions.md` | The library containing the search and UCI protocol |
