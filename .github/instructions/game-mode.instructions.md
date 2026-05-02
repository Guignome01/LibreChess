---
applyTo: "src/game_mode/**"
description: "Firmware game modes: GameMode base, PlayerMode, BotMode. State machine, resign gesture, board setup, engine composition."
---

# Game Mode Architecture

## Class Hierarchy

`GameMode` (abstract base) → `PlayerMode` (human vs human) | `BotMode` (human vs engine, composes `EngineProvider*`).

## GameMode (base)

Central fields injected via constructor: `board_` (`Board` facade), `wifiManager_`, `chess_` (`Game` orchestrator), `logger_` (`Log` proxy, wraps optional `ILogger*`). All log output uses `logger_.info/infof/error/errorf(...)` directly — the `Log` proxy handles null internally. No direct `Serial` calls.

### Lifecycle
- `begin()` — pure virtual. Subclasses set up the game (resume or new), then call `waitForBoardSetup()`.
- `update()` — pure virtual. Called repeatedly from the main loop. Must be **non-blocking**.
- `isGameOver()` — delegates to `chess_->isGameOver()`.
- `isNavigationAllowed()` — virtual, default `true`. BotMode blocks navigation during engine thinking.

### Core Move Flow
1. `tryPlayerMove(playerColor, ...)` — consumes `BoardState` changed-square entries for piece lift, placement, and capture-removal detection, shows legal-move highlights (white = empty, red = capture, purple = en-passant pawn), then waits for completion. Returns `true` with from/to coords when a valid destination is chosen.
2. `applyMove(from, to, promotion, isRemoteMove)` — calls `chess_->makeMove()` (which handles all move/game-end/check logging), delegates castling/remote physical guidance to `board_->assistance()`, then delegates outcome visuals to `board_->feedback()` (capture animation, promotion animation, check blink, game-end fireworks).
3. `applyMove(string)` — coordinate-string overload, parses then delegates with `isRemoteMove = true`.

### Resume Support
`tryResumeGame()` — checks `chess_->hasActiveGame()`, calls `resumeGame()`. Returns `true` if a live game was resumed from flash.

### Board Setup
`waitForBoardSetup()` — delegates to `BoardAssistance`, which runs the blocking LED guidance loop for the required position (piece color = place here, red = remove this). Ends with firework animation.

### Resign System
Three-phase king-based resign gesture managed in `GameMode`: hold king off square (3s) → 2 quick lift-and-returns with escalating orange brightness → board confirm dialog → `chess_->endGame(RESIGN, ...)`.

Virtual hooks let subclasses customize: `isFlipped()`, `onBeforeResignConfirm()`, `onResignCancelled()`, `onResignConfirmed(color)`. BotMode uses these to cancel/restart the engine and notify the provider.

Web resign: `setResignPending(true)` → `processResign()` checks the flag at the start of `update()`.

### Remote Move Guidance
`waitForRemoteMoveCompletion(...)` — virtual, no-op in base. BotMode overrides to delegate to `BoardAssistance`, which blocks with LED guidance until the player physically executes the engine's move on the board.

## PlayerMode

Minimal subclass — `begin()` resumes or starts a `GameModeId::PLAYER` game. `update()` calls `tryPlayerMove(sideToMove)` for alternating colors.

## BotMode

Composes an `EngineProvider*` (strategy pattern, owned — deleted in destructor).

### State Machine (`BotState`)
- `PLAYER_TURN` — `tryPlayerMove()` polls sensors. On valid move: `applyMove()`, notify provider via `onPlayerMoveApplied()`, then if game continues and it's engine's turn → transition to `ENGINE_THINKING`.
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

- **Resign lives in GameMode, not BotMode** — both PlayerMode and BotMode need resign. Putting it in the base class avoids duplication. The virtual hooks (`onBeforeResignConfirm`, `onResignCancelled`, `onResignConfirmed`) let BotMode add engine-specific behavior (cancel request, restart thinking) without duplicating the 3-phase gesture flow.

- **`tryPlayerMove()` is blocking within a non-blocking loop** — once `BoardState::wasLifted()` identifies a piece lift from the facade's changed-square list, `tryPlayerMove()` enters a blocking wait for placement or capture-removal transitions. This is intentional: the physical state during piece-in-hand requires continuous polling for the target square. The outer `update()` loop remains non-blocking because `tryPlayerMove()` returns `false` (no lift detected) on most ticks.

- **`applyMove()` is the chess mutation boundary; board modules own physical interaction** — `GameMode::applyMove()` calls `Game::makeMove()` exactly once, then uses `board_->assistance()` for physical prompts and `board_->feedback()` for move-result visuals. Game-end and check/turn *logging* is handled by `Game` (not duplicated here). Subclasses don't override this. This centralizes the chess flow while keeping board-specific interaction sequences in the board subsystem.

- **Remote moves use board-owned LED guidance** — when BotMode applies an engine move, `waitForRemoteMoveCompletion()` delegates to `BoardAssistance` for LED cues (cyan = pick up, white/red = destination) until the player physically executes the move. This bridges the gap between software state (already applied) and physical board state (player must move the piece).

- **BotMode owns the provider** — `BotMode` deletes the `EngineProvider*` in its destructor. This makes game mode transitions clean: destroying a `BotMode` automatically cancels any running engine task and frees the provider. The provider is never shared between modes.

- **Navigation blocked during engine thinking** — `isNavigationAllowed()` returns `false` in `ENGINE_THINKING`. Without this, a menu navigation during an active FreeRTOS task would corrupt state. The web UI gets `409 Conflict` from `POST /nav` during this window.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game.instructions.md` | `GameMode` holds a `Game*` — sole interface to chess logic |
| `board-driver.instructions.md` | `GameMode` uses the firmware `Board` facade for LED/sensor interaction |
| `engine.instructions.md` | `BotMode` composes `EngineProvider*` |
| `game-headers.instructions.md` | `meta[]` semantic overlay (`GameModeId`, engineId, difficulty) |
| `wifi-manager.instructions.md` | `GameMode` holds a `WiFiManagerESP32*` |
| `api.instructions.md` | `POST /gameselect` params map to mode selection |
