---
description: "Use when modifying BoardDriver, BoardDiagnostics, LED animations, sensor reading, calibration, or any hardware interaction code. Covers LED mutex rules, animation queue lifecycle, color semantics, sensor debounce, and NVS persistence."
applyTo: "src/board/**, src/system_utils.*"
---

# Board Firmware & Hardware Patterns

`src/board/board.*` is the firmware-facing facade. It composes `BoardDriver`, `BoardFeedback`, `BoardAssistance`, and a `LibreChess::board::BoardState` snapshot. External firmware modules should depend on `Board*`; `src/board/driver.*` remains the low-level hardware owner. `src/board/animations.*` owns animation job definitions and visual animation execution through driver drawing primitives; `src/board/lifecycle.*` owns the FreeRTOS queue/task, LED mutex, stop flags, and queue barrier.

`src/board/assistance.*` owns physical chess guidance: setup prompts, configurable legal-move assistance (`BoardAssistanceLevel`), castling prompts, remote-move completion prompts, and capture placement prompts. It may read `Game` through public APIs for display decisions, but it must not mutate chess state, talk to engine providers, or call WiFi APIs.

`src/board/feedback.*` owns always-on visual feedback: illegal move blink, resign progress, post-move confirmation, capture/promotion/check/game-end effects, thinking/waiting status animations, remote game-end display, and error flashes.

`src/board/calibration.*` owns the serial-guided calibration workflow and NVS mapping persistence. It is board-internal and uses `BoardDriver` friendship for raw GPIO scanning, strip writes, mapping arrays, and `boardCal` persistence. `BoardDriver::begin()` delegates `load()`/`run()`/`save()` to `BoardCalibration`; `BoardDriver` applies the resulting mapping during normal sensor and LED operations.

`src/board/diagnostics.*` owns board diagnostic workflows such as the user-facing Sensor Test mode. It depends on the `Board` facade, not `BoardDriver`; after an initial occupancy snapshot it consumes `BoardState` changed-square entries (`changedCount()`/`changedSquare()`) to record newly visited squares.

## LED Access

The LED strip is shared between the main loop and a dedicated animation FreeRTOS task, guarded by the mutex owned by `BoardAnimationLifecycle`.

### LedGuard (RAII Mutex)

Multi-step LED writes from the main loop **must** use a scoped `Board::LedGuard` through the facade:

```cpp
{
    Board::LedGuard guard(board);
    board->clearAllLEDs();
    board->setSquareLED(row, col, LedColors::Cyan);
    board->showLEDs();
}
```

Use `BoardDriver::LedGuard` only inside board-internal driver code.

Single queued animations (`blinkSquare`, `captureAnimation`, etc.) acquire the mutex internally — no guard needed by the caller.

### Animation Queue

Animations run on a dedicated FreeRTOS task owned by `BoardAnimationLifecycle` that dequeues `AnimationJob` structs. Each job has a type and type-specific params.

**Short animations** (capture, promotion, blink, firework, flash) — fire-and-forget. Enqueue and return.

**Long-running animations** (thinking, waiting) — return `std::atomic<bool>*` (heap-allocated stop flag). The animation checks the flag each frame. To stop:
1. Call `stopAndWaitForAnimation(flag)` — sets the flag, blocks on the lifecycle completion semaphore, then deletes the flag.
2. **Never** set the flag directly or delete it without waiting — the animation task may still hold the LED mutex mid-frame.

**Barrier**: Call `waitForAnimationQueueDrain()` before writing LEDs directly. It enqueues a `SYNC` no-op and blocks until the worker processes it. Without this, a stale queued animation can overwrite your direct LED writes.

### Animation Types

| Type | Duration | Description |
|------|----------|-------------|
| Capture | ~1s | Concentric wave rings from capture square (red/yellow, quadratic falloff) |
| Promotion | ~1.6s | Yellow waterfall cascading down the promotion column |
| Blink | Configurable | Square blinks N times in a color (check=yellow 3x, confirm=green 1x, illegal=red 2x) |
| Firework | ~2.4s | Ring contracts from edges to center, expands back (winner's color) |
| Flash | Configurable | Entire board flashes N times (critical error = red 3x) |
| Thinking | Continuous | Four corners pulse blue with sinusoidal breathing (8%–100%) |
| Waiting | Continuous | White chase on 28 perimeter squares, two groups of 8 LEDs travel opposite |
| Connecting | One-shot | Two center rows fill blue left-to-right |

## Color Semantics

Colors in `LedColors` (`src/board/colors.h`) have **fixed semantic meanings**. Never use a color for a different purpose.

| Color | Meaning | Usage |
|-------|---------|-------|
| Cyan | Piece origin | "Pick up from here" |
| White | Valid move destination | Also: menu back button |
| DimWhite | "Play as Black" | Bot color selection menu |
| Red | Capture / error | Capture square, illegal move, error indication |
| Green | Confirmation | Move confirmed, "yes" in dialogs |
| Yellow | Check / promotion | King in check warning, pawn promotion, random option |
| Purple | En passant | Captured pawn square; also expert difficulty |
| Orange | Resign gesture | Brightness progression during resign hold |
| Blue | Bot thinking | Thinking animation, Human vs Human indicator |
| Lime | Easy difficulty | Difficulty menu |
| Crimson | Hard difficulty | Difficulty menu |

`scaleColor(color, factor)` — `inline constexpr` helper for brightness progression (0.0–1.0). Used for resign gesture ramp-up.

## Sensor Grid

64 hall-effect sensors in 8×8 matrix, read via column-scanning with a 74HC595 shift register.

- **Polling**: `SENSOR_READ_DELAY_MS` = 40ms interval
- **Debounce**: `DEBOUNCE_MS` = 125ms — piece must be stable for the full window
- **Sensor ownership split**: `BoardDriver` owns raw/debounced current state (`sensorRaw` → `sensorState`); the facade's `BoardState` owns previous/current snapshots and derived transitions.
- **Always call `board->readSensors()` before checking state** — hardware state updates only on explicit read. This also refreshes the facade's `BoardState` transition snapshot.
- Efficient sequential column shifting via `lastEnabledCol` optimization

## Calibration

Interactive serial-guided process implemented by `BoardCalibration`, mapping physical pins to logical `[row][col]` coordinates. Results persisted in NVS namespace `"boardCal"`:
- `toLogicalRow[]`, `toLogicalCol[]` — sensor mapping
- `ledIndexMap[8][8]` — LED position mapping
- `swapAxes` — handles boards with swapped shift register / row pin axes

Runs on first boot or via web UI trigger. Board repeats calibration prompt until completed (with `skip` option).

## NVS Persistence

Settings stored via Arduino `Preferences`. Always call `SystemUtils::ensureNvsInitialized()` before first use. Key namespaces:
- `"boardCal"` — sensor/LED mapping tables
- `"ledSettings"` — LED brightness and dim multiplier

## Design Decisions

- **LED mutex exists because of the animation task** — the LED strip is shared between the main loop (direct LED writes in `tryPlayerMove`, `waitForBoardSetup`) and the dedicated animation FreeRTOS task. Without the mutex, concurrent writes corrupt the strip state. `BoardAnimationLifecycle` owns the mutex and `LedGuard` makes direct writes safe by scoping the lock.

- **Animation queue lifecycle is separate from hardware scanning** — animations run on a separate task to keep `update()` non-blocking. `BoardAnimationLifecycle` owns the queue/task/semaphore details so `BoardDriver` can stay focused on sensors, strip writes, settings, and calibration mapping. The queue provides natural sequencing: multiple animations play in order without explicit coordination.

- **Long-running animations use atomic stop flags** — thinking/waiting animations loop indefinitely. A heap-allocated `std::atomic<bool>*` is returned to the caller who controls when it stops. The flag is heap-allocated because both the caller and the animation task need to access it, and either might outlive the other during shutdown. `stopAndWaitForAnimation()` ensures the animation has fully released the LED mutex before the caller continues.

- **`waitForAnimationQueueDrain()` prevents race conditions** — without the drain barrier, queued animations from a previous move can overwrite LED highlights for the current piece-in-hand. The SYNC job ensures all pending work completes before direct LED writes begin.

- **Driver debounce plus `BoardState` transitions prevent glitch reads** — `sensorRaw` captures the latest hardware read, `sensorState` exposes only debounced stable occupancy, and `BoardState` compares previous/current stable snapshots for `wasLifted()`/`wasPlaced()` queries. The debounce window (125ms) eliminates false triggers from magnet edge effects when pieces slide between squares.

- **Colors have fixed semantics** — the color table in `src/board/colors.h` is a project-wide contract. Cyan always means "piece origin", red always means "capture/error", etc. This consistency lets players learn the visual language once and apply it everywhere. Never reuse a color for a different meaning.

- **Guidance and feedback are separate board concerns** — `BoardAssistance` owns optional/physical guidance such as legal-move hints and sensor-blocking prompts, while `BoardFeedback` owns mandatory outcomes and status visuals. This lets assistance levels evolve without disabling core mode feedback or duplicating guidance logic between local and bot modes.

- **Calibration is separate from low-level scanning** — `BoardCalibration` owns the interactive serial workflow and persistence schema, while `BoardDriver` owns the hardware primitives and applies the saved mapping. This keeps first-boot/calibration UX out of the steady-state sensor scan path.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game-mode.instructions.md` | Primary consumer of the `Board` facade — LED feedback and sensor reading |
