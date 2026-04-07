---
description: "Use when modifying BoardDriver, LED animations, sensor reading, calibration, or any hardware interaction code. Covers LED mutex rules, animation queue lifecycle, color semantics, sensor debounce, and NVS persistence."
applyTo: "src/board_driver.*, src/led_colors.h, src/sensor_test.*, src/system_utils.*"
---

# BoardDriver & Hardware Patterns

## LED Access

The LED strip is shared between the main loop and a dedicated animation FreeRTOS task, guarded by `ledMutex`.

### LedGuard (RAII Mutex)

Multi-step LED writes from the main loop **must** use a scoped `BoardDriver::LedGuard`:

```cpp
{
    BoardDriver::LedGuard guard(boardDriver);
    boardDriver.clearAllLEDs();
    boardDriver.setSquareLED(row, col, LedColors::Cyan);
    boardDriver.showLEDs();
}
```

Single queued animations (`blinkSquare`, `captureAnimation`, etc.) acquire the mutex internally — no guard needed by the caller.

### Animation Queue

Animations run on a dedicated FreeRTOS task (`animationWorkerTask`) that dequeues `AnimationJob` structs from `animationQueue`. Each job has a type and type-specific params.

**Short animations** (capture, promotion, blink, firework, flash) — fire-and-forget. Enqueue and return.

**Long-running animations** (thinking, waiting) — return `std::atomic<bool>*` (heap-allocated stop flag). The animation checks the flag each frame. To stop:
1. Call `stopAndWaitForAnimation(flag)` — sets the flag, blocks on `animationDoneSemaphore`, then deletes the flag.
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

Colors in `LedColors` (`led_colors.h`) have **fixed semantic meanings**. Never use a color for a different purpose.

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
- **Triple-buffered state**: `sensorRaw` (latest read) → `sensorState` (debounced) → `sensorPrev` (snapshot for change detection)
- **Always call `boardDriver.readSensors()` before checking state** — arrays update only on explicit read
- Efficient sequential column shifting via `lastEnabledCol` optimization

## Calibration

Interactive serial-guided process mapping physical pins to logical `[row][col]` coordinates. Results persisted in NVS namespace `"calibration"`:
- `toLogicalRow[]`, `toLogicalCol[]` — sensor mapping
- `ledIndexMap[8][8]` — LED position mapping
- `swapAxes` — handles boards with swapped shift register / row pin axes

Runs on first boot or via web UI trigger. Board repeats calibration prompt until completed (with `skip` option).

## NVS Persistence

Settings stored via Arduino `Preferences`. Always call `SystemUtils::ensureNvsInitialized()` before first use. Key namespaces:
- `"calibration"` — sensor/LED mapping tables
- `"settings"` — LED brightness, dim multiplier, other user preferences

## Design Decisions

- **LED mutex exists because of the animation task** — the LED strip is shared between the main loop (direct LED writes in `tryPlayerMove`, `waitForBoardSetup`) and the dedicated animation FreeRTOS task. Without the mutex, concurrent writes corrupt the strip state. `LedGuard` makes this safe by scoping the lock.

- **Animation queue, not direct calls** — animations run on a separate task to keep `update()` non-blocking. If `captureAnimation()` ran inline, the main loop would freeze for ~1s. The queue also provides natural sequencing: multiple animations play in order without explicit coordination.

- **Long-running animations use atomic stop flags** — thinking/waiting animations loop indefinitely. A heap-allocated `std::atomic<bool>*` is returned to the caller who controls when it stops. The flag is heap-allocated because both the caller and the animation task need to access it, and either might outlive the other during shutdown. `stopAndWaitForAnimation()` ensures the animation has fully released the LED mutex before the caller continues.

- **`waitForAnimationQueueDrain()` prevents race conditions** — without the drain barrier, queued animations from a previous move can overwrite LED highlights for the current piece-in-hand. The SYNC job ensures all pending work completes before direct LED writes begin.

- **Triple-buffered sensors prevent glitch reads** — `sensorRaw` (latest hardware read) → `sensorState` (debounced, stable) → `sensorPrev` (previous frame snapshot). Change detection compares `sensorState` vs `sensorPrev`. The debounce window (125ms) eliminates false triggers from magnet edge effects when pieces slide between squares.

- **Colors have fixed semantics** — the color table in `led_colors.h` is a project-wide contract. Cyan always means "piece origin", red always means "capture/error", etc. This consistency lets players learn the visual language once and apply it everywhere. Never reuse a color for a different meaning.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game-mode.instructions.md` | Primary consumer of `BoardDriver` — LED feedback and sensor reading |
