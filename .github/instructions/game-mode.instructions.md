---
applyTo: "src/game_mode/**"
description: "Firmware game modes: GameMode base, PlayerMode, BotMode. State machine, resign gesture, board setup, engine composition."
---

# Game Mode Architecture

## Class Hierarchy

`GameMode` (abstract base) → `PlayerMode` (human vs human) | `BotMode` (human vs engine, composes `EngineProvider*`).

## GameMode (base)

Central fields injected via constructor: `gameplay_` (`BoardGameProgram*`, the board-facing physical chess program contract carried by `Board::startProgram("game").game`), `wifiManager_`, `chess_` (`Game` orchestrator), and `logger_` (`Log` proxy, wraps optional `ILogger*`). Game modes do not hold raw `Board*`, do not create board programs, and do not own assistance providers; `Board` owns the active `BoardAssistanceProvider` and the runner-owned game program services it. All log output uses `logger_.info/infof/error/errorf(...)` directly — the `Log` proxy handles null internally. No direct `Serial` calls.

`board_adapter.*` is the only GameMode-side mapper from `LibreChess::Game`,
`MoveList`, `MoveResult`, `CastlingInfo`, and core `Color`/`Piece` types into
board-owned DTOs from `board/types.h` plus the `BoardGameRules` contract in
`board/programs/game/game_rules.h`. Its concrete adapter class is
`BoardAdapter::GameRules`. `src/board/` must not include `game.h`, `move.h`, or
concrete engine provider headers.

### Lifecycle
- `begin()` — pure virtual. Subclasses set up the game (resume or new), then call `waitForBoardSetup()`.
- `update()` — pure virtual. Called repeatedly from the main loop. Must be **non-blocking**.
- `isGameOver()` — delegates to `chess_->isGameOver()`.
- `isNavigationAllowed()` — virtual, default `true`. BotMode blocks navigation during engine thinking.

### Core Move Flow
1. `tryPlayerMove(playerColor, ...)` — builds a `BoardAdapter::GameRules`, delegates physical lift/placement/capture-removal detection to `BoardGameProgram::tryPlayerMove()`, then maps any board-local resign color back to core `Color` before calling `completeResign()`.
2. `applyMove(from, to, promotion, isRemoteMove)` — cancels stale assistance, maps pre-move castling info, calls `chess_->makeMove()` exactly once (which handles all move/game-end/check logging), maps `MoveResult` to board completion/feedback DTOs, then delegates physical completion and visuals to `BoardGameProgram::completeAppliedMove()`.
3. `applyMove(string)` — coordinate-string overload, parses then delegates with `isRemoteMove = true`.

### Assistance

`serviceAssistance()` delegates to `BoardGameProgram::serviceAssistance()` and is
called only while a local player can act (`PlayerMode::update()` and
`BotMode::PLAYER_TURN`). `NONE` and `LEGAL_MOVES` never call an engine-backed
provider; `BEST_MOVE` requests/polls the board-owned `BoardAssistanceProvider`
until a hint is ready, then the board displays it. `cancelAssistance()` runs before
move application, board edits, resign confirmation, mode destruction, and other
state changes that would make a pending hint stale.

### Resume Support
`tryResumeGame()` — checks `chess_->hasActiveGame()`, calls `resumeGame()`. Returns `true` if a live game was resumed from flash.

### Board Setup
`waitForBoardSetup()` — delegates through `BoardGameProgram` to board-owned setup assistance, which runs the blocking LED guidance loop for the required position (piece color = place here, red = remove this). Ends with firework animation.

### Resign System
Three-phase king-based physical resign gesture managed through `BoardGameProgram`: hold king off square (3s) → 2 quick lift-and-returns with escalating orange brightness → board confirm dialog. `GameMode::completeResign()` remains the mutation boundary: it runs virtual hooks, asks the game program to show winner feedback, and calls `chess_->endGame(RESIGN, ...)` only after board confirmation succeeds.

Virtual hooks let subclasses customize: `isFlipped()`, `onBeforeResignConfirm()`, `onResignCancelled()`, `onResignConfirmed(color)`. BotMode uses these to cancel/restart the engine and notify the provider.

Web resign: `setResignPending(true)` → `processResign()` checks the flag at the start of `update()`.

### Remote Move Guidance
`BoardGameProgram::completeAppliedMove(...)` handles remote physical completion for already-applied engine moves. It delegates through board-owned visual/assistance services for LED guidance until the player physically executes the move on the board, then shows the normal move-result feedback. BotMode no longer owns a remote-guidance override.

## PlayerMode

Minimal subclass — `begin()` resumes or starts a `GameModeId::PLAYER` game. `update()` services assistance, then calls `tryPlayerMove(sideToMove)` for alternating colors.

## BotMode

Composes an `EngineProvider*` (strategy pattern, owned — deleted in destructor).

### State Machine (`BotState`)
- `PLAYER_TURN` — services independent assistance, then `tryPlayerMove()` polls sensors. On valid move: `applyMove()`, notify provider via `onPlayerMoveApplied()`, then if game continues and it's engine's turn → transition to `ENGINE_THINKING`.
- `ENGINE_THINKING` — `provider_->checkResult()` polls the background task. On result: `stopThinking()`, apply engine move or handle remote game-end, transition back to `PLAYER_TURN`.

### begin() Flow
1. Check WiFi → abort if disconnected.
2. Show waiting animation → `provider_->initialize()` (may block for HTTP).
3. Stop animation → check init result.
4. Resume or start new game with `initResult.playerColor`, `GameMeta` (mode + engineId + difficulty).
5. `waitForBoardSetup()`.
6. If engine's turn first → immediately `requestMove()` + enter `ENGINE_THINKING`.

### Navigation
`isNavigationAllowed()` returns `false` during `ENGINE_THINKING` (prevents menu access while engine is working).

### Error Handling
`abortWithError(message)` — red flash + `endGame(ABORTED, ' ')`. Used for WiFi failure, init failure, and provider errors.

## Design Decisions

- **Resign is split by responsibility** — `BoardGameplay` owns the physical king gesture and board confirmation because those are sensor/visual program concerns. `GameMode::completeResign()` owns the chess mutation and virtual hooks because both PlayerMode and BotMode need the same lifecycle boundary. The hooks (`onBeforeResignConfirm`, `onResignCancelled`, `onResignConfirmed`) let BotMode add engine-specific behavior (cancel request, restart thinking) without duplicating the 3-phase gesture flow.

- **`BoardGameProgram::tryPlayerMove()` is blocking within a non-blocking loop** — once the gameplay-owned transition snapshot identifies a piece lift, the physical interaction mode enters a blocking wait for placement or capture-removal transitions. This is intentional: the physical state during piece-in-hand requires continuous polling for the target square. The outer `update()` loop remains non-blocking because `GameMode::tryPlayerMove()` returns `false` (no lift detected) on most ticks.

- **`applyMove()` is the chess mutation boundary; the active game program owns physical completion** — `GameMode::applyMove()` calls `Game::makeMove()` exactly once, then calls `BoardGameProgram::completeAppliedMove()` for remote physical prompts, castling guidance, and move-result visuals. Game-end and check/turn *logging* is handled by `Game` (not duplicated here). Subclasses don't override this. This centralizes the chess flow while keeping board-specific interaction sequences out of BotMode/PlayerMode.

- **Remote moves use shared gameplay guidance** — when BotMode applies an engine move, `BoardGameProgram::completeAppliedMove()` delegates to board assistance for LED cues (cyan = pick up, white/red = destination) until the player physically executes the move. This bridges the gap between software state (already applied) and physical board state (player must move the piece) without a BotMode-specific override.

- **Assistance is independent from the opponent engine** — `BotMode` owns only the opponent `EngineProvider*`; `Board` owns the active `BoardAssistanceProvider` used for hints. BEST_MOVE can therefore be backed by LibreChess while the opponent engine is Stockfish, Lichess, or LibreChess. Legal-move-only assistance uses the rules adapter and never calls an engine provider.

- **BotMode owns the provider** — `BotMode` deletes the `EngineProvider*` in its destructor. This makes game mode transitions clean: destroying a `BotMode` automatically cancels any running engine task and frees the provider. The provider is never shared between modes.

- **Navigation blocked during engine thinking** — `isNavigationAllowed()` returns `false` in `ENGINE_THINKING`. Without this, a menu navigation during an active FreeRTOS task would corrupt state. The web UI gets `409 Conflict` from `POST /nav` during this window.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game.instructions.md` | `GameMode` holds a `Game*` — sole interface to chess logic |
| `board-driver.instructions.md` | `GameMode` uses `BoardGameProgram`, while concrete physical board interaction remains inside `src/board/programs/game/` |
| `engine.instructions.md` | `BotMode` composes `EngineProvider*` |
| `game-headers.instructions.md` | `meta[]` semantic overlay (`GameModeId`, engineId, difficulty) |
| `wifi-manager.instructions.md` | `GameMode` holds a `WiFiManagerESP32*` |
| `api.instructions.md` | `POST /gameselect` params map to mode selection |
