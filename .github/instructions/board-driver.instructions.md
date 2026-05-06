---
description: "Use when modifying BoardDriver, BoardGui, BoardDiagnostics, LED animations, sensor reading, calibration, or any hardware interaction code. Covers LED mutex rules, scheduler queue lifecycle, color semantics, sensor debounce, and NVS persistence."
applyTo: "src/board/**, src/shared/utils.*"
---

# Board Firmware & Hardware Patterns

`src/board/board.*` is the shared physical-board base layer and common capability surface for board workflows. It owns `BoardSystem` and `BoardGui`, exposes board-wide technical operations that are true in every workflow (lifecycle, root/resume menu entry, display/feedback clearing, connecting animation, LED settings, timing), and keeps raw internals private. Board-owned workflows (`BoardGameplay`, `BoardDiagnostics`, `BoardCalibration`) are imported directly for their use case and compose around `Board&`. External firmware must not consume `BoardDriver`, `BoardSystem`, `BoardGui`, `BoardLayering`, colors, or raw LED batches directly. `src/board/core/` holds board runtime primitives: `system.*` is the board-internal service boundary, `driver.*` owns low-level hardware, `colors.h` owns semantic LED colors, and `scheduler.*` owns the FreeRTOS queue/task, LED mutex, stop flags, and queue barrier. Physical board dimensions and `BoardSquare` live in `board.*` because they are base-layer geometry, not a separate state object. `src/board/gui/gui.*` is the board-internal visual coordinator; it owns/wires feedback, assistance, board menus, `BoardStack`, `BoardLayering`, and selection/resume visual policy. `src/board/gui/layering.*` owns persistent base + overlay composition, and `src/board/gui/animations.*` owns value-based animation job factories and visual execution.

`src/board/gui/drawable.h` defines the minimal `BoardDrawable` contract for modal board visuals. `src/board/gui/stack.*` owns modal push/pop/back behavior over non-owning `BoardDrawable*` entries; menus are the first concrete drawable. Overlay composition belongs to `BoardLayering`, not `BoardStack`.

`src/board/gui/assistance.*` owns physical chess guidance: setup prompts, configurable legal-move assistance (`BoardAssistanceLevel`), castling prompts, remote-move completion prompts, and capture placement prompts. It may read `Game` through public APIs for display decisions, but it must not mutate chess state, talk to engine providers, or call WiFi APIs.

`src/board/gui/feedback.*` owns always-on visual feedback: illegal move blink, resign progress, post-move confirmation, capture/promotion/check/game-end effects, thinking/waiting status animations, remote game-end display, and error flashes.

`src/board/calibration.*` exposes the external `BoardCalibration` mode used by application/WiFi flows and the internal `BoardCalibrationWorkflow` wired by `BoardSystem`. The workflow owns serial-guided mapping and NVS persistence, using explicit `BoardDriver` calibration primitives for raw GPIO scanning, raw pixel writes, mapping table access, and `boardCal` persistence. `BoardSystem::begin()` delegates `load()`/`run()`/`save()` to `BoardCalibrationWorkflow` after low-level driver initialization; `BoardDriver` applies the resulting mapping during normal sensor and LED operations but does not include calibration or grant friendship.

`src/board/diagnostics.*` owns board diagnostic workflows such as the user-facing Sensor Test mode. It is a long-lived board mode constructed around `Board&`; internally it uses board-mode access to `BoardSystem` and `BoardLayering`, scans current debounced occupancy each update, and records visited squares in its own workflow state.

`src/board/gameplay.*` owns physical chess gameplay workflows. It is a long-lived board mode constructed around `Board&`, owns current/previous/changed physical occupancy snapshots, detects player lift/placement/capture transitions, runs setup/remote/castling physical guidance through `BoardGui`, and exposes status/error/winner visuals to game modes. It may query `Game` through public APIs but never mutates chess state.

## LED Access

The LED strip is shared between the main loop and a dedicated animation FreeRTOS task, guarded by the mutex owned by `BoardScheduler`.

### Batched Direct LED Updates

External firmware must not include board color/menu headers or perform raw LED batches directly. If an external caller needs board behavior, add a semantic `Board` method or getter.

Board-local multi-step LED writes **must** run through `BoardSystem::batchLEDs()`:

```cpp
system->batchLEDs([&](BoardSystem::LEDWriter& leds) {
    leds.clearAllLEDs(false);
    leds.setSquareLED(row, col, LedColors::Cyan);
    leds.showLEDs();
});
```

`BoardSystem` owns direct dispatch between board-local helpers, the driver, and the scheduler. The scheduler owns the mutex completely. Callers should describe the LED batch they want to draw; they should not acquire or release the strip manually. Board/workflow calls such as `Board::clearAllLEDs()` or `BoardGameplay` move-result feedback route through `BoardGui` and lock internally through the system.

Persistent board visuals should prefer `BoardLayering` over direct full-board batches. The base layer stores menus, setup prompts, legal-move highlights, remote-move guidance, and diagnostics. The overlay layer stores temporary or higher-priority visuals such as resign progress. `BoardLayering` renders both layers through one `BoardSystem::batchLEDs()` call, while short animations remain value-based `AnimationJob`s.

Single queued animations are built with `AnimationJob` factory helpers and submitted through `BoardSystem::runAnimation()`. They acquire the mutex internally — no guard needed by the caller.

### Animation Queue

Animations run on a dedicated FreeRTOS task owned by `BoardScheduler` that dequeues factory-built `AnimationJob` structs. Each job has a type and type-specific params. Board-local semantic modules build jobs with helpers such as `AnimationJob::capture(...)`, `AnimationJob::blink(...)`, and `AnimationJob::firework(...)`, then submit them through `BoardSystem::runAnimation(job)`. Direct one-shot animations, currently WiFi connecting, use `BoardSystem::runAnimationNow(AnimationJob::connecting())` so they still run under the scheduler-owned LED mutex.

**Short animations** (capture, promotion, blink, firework, flash) — fire-and-forget. Build a value job with `AnimationJob` helpers, enqueue through `BoardSystem::runAnimation()`, and return.

**Long-running animations** (thinking, waiting) — start through `BoardSystem::startAnimation(AnimationType::THINKING/WAITING)` and return `std::atomic<bool>*` (heap-allocated stop flag). The animation checks the flag each frame. To stop:
1. Call `stopAndWaitForAnimation(flag)` — sets the flag, blocks on the scheduler completion semaphore, then deletes the flag.
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

Colors in `LedColors` (`src/board/core/colors.h`) have **fixed semantic meanings**. Never use a color for a different purpose.

| Color | Meaning | Usage |
|-------|---------|-------|
| Cyan | Piece origin | "Pick up from here" |
| White | Valid move destination | Also: menu back button |
| `scaleColor(White, 40.0f / 255.0f)` | "Play as Black" | Bot color selection menu |
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
- **Sensor ownership split**: `BoardDriver` owns raw/debounced current state (`sensorRaw` → `sensorState`); `BoardSystem` exposes only current debounced occupancy; `BoardGameplay` owns previous/current gameplay snapshots and derives lifted/placed/changed transitions from stable snapshots.
- **Always call the owning workflow's poll method before checking state** — hardware state updates only on explicit reads. Game modes call `BoardGameplay::readSensors()`; diagnostics calls `BoardSystem::readSensors()` internally; GUI menus poll through `Board`/`BoardGui`. Outside `src/board/`, use `Board::sensorReadDelayMs()` instead of the `SENSOR_READ_DELAY_MS` macro.
- Efficient sequential column shifting via `lastEnabledCol` optimization

## Calibration

Interactive serial-guided process implemented by `BoardCalibration`, mapping physical pins to logical `[row][col]` coordinates. Results persisted in NVS namespace `"boardCal"`:
- `toLogicalRow[]`, `toLogicalCol[]` — sensor mapping
- `ledIndexMap[8][8]` — LED position mapping
- `swapAxes` — handles boards with swapped shift register / row pin axes

Runs on first boot or via web UI trigger. Board repeats calibration prompt until completed (with `skip` option).

## NVS Persistence

Settings stored via Arduino `Preferences`. Always call `SystemUtils::ensureNvsInitialized()` from `src/shared/utils.*` before first use. Key namespaces:
- `"boardCal"` — sensor/LED mapping tables
- `"ledSettings"` — LED brightness and dim multiplier

## Design Decisions

- **LED mutex exists because of the animation task** — the LED strip is shared between the main loop (direct LED writes in assistance, feedback, diagnostics, and menus) and the dedicated animation FreeRTOS task. Without the mutex, concurrent writes corrupt the strip state. `BoardScheduler` owns the mutex, and `BoardSystem::batchLEDs()` keeps that synchronization detail out of both board workflows and board-local helpers.

- **Visual coordination is separate from board services** — `BoardGui` owns visual policy and composes feedback, assistance, menus, and the modal `BoardStack`. `BoardSystem` remains the service boundary for current sensors, settings, calibration triggers, LED batches, and animation submission. Board modes call `BoardGui` through restricted board-mode access; public firmware stays isolated from board-local render types.

- **Layering is separate from modal navigation** — `BoardLayering` owns persistent base + overlay composition for simultaneous LED visuals. `BoardStack` owns modal push/pop flow only. This keeps menu navigation, overlay priority, and low-level LED locking from collapsing into one class.

- **Animation scheduling is separate from hardware scanning** — animations run on a separate task to keep `update()` non-blocking. `BoardScheduler` owns generic queue/task/semaphore details, `BoardSystem` wires it to the driver, and `BoardDriver` stays focused on sensors, strip writes, settings, and calibration mapping. The GUI animation module owns job construction through value-based factory helpers and visual execution, so scheduler/system APIs do not grow one wrapper per animation type. The queue provides natural sequencing: multiple animations play in order without explicit coordination.

- **Long-running animations use atomic stop flags** — thinking/waiting animations loop indefinitely. A heap-allocated `std::atomic<bool>*` is returned to the caller who controls when it stops. The flag is heap-allocated because both the caller and the animation task need to access it, and either might outlive the other during shutdown. `stopAndWaitForAnimation()` ensures the animation has fully released the LED mutex before the caller continues.

- **`waitForAnimationQueueDrain()` prevents race conditions** — without the drain barrier, queued animations from a previous move can overwrite LED highlights for the current piece-in-hand. The SYNC job ensures all pending work completes before direct LED writes begin.

- **Driver debounce plus gameplay transitions prevent glitch reads** — `sensorRaw` captures the latest hardware read, `sensorState` exposes only debounced stable occupancy, and `BoardGameplay` compares previous/current stable snapshots for `wasLifted()`/`wasPlaced()` queries. The debounce window (125ms) eliminates false triggers from magnet edge effects when pieces slide between squares.

- **The board base must not re-export internals** — `Board` is the shared base layer for capabilities that are true in every workflow. If code outside `src/board/` needs a workflow-specific behavior, expose it through that workflow (`BoardGameplay`, `BoardDiagnostics`, `BoardCalibration`) instead of widening `Board` or including board internals.

- **Colors have fixed semantics** — the color table in `src/board/core/colors.h` is a project-wide contract. Cyan always means "piece origin", red always means "capture/error", etc. This consistency lets players learn the visual language once and apply it everywhere. Never reuse a color for a different meaning.

- **Guidance and feedback are separate board concerns** — `BoardAssistance` owns optional/physical guidance such as legal-move hints and sensor-blocking prompts, while `BoardFeedback` owns mandatory outcomes and status visuals. This lets assistance levels evolve without disabling core mode feedback or duplicating guidance logic between local and bot modes.

- **Calibration is split between external mode, system wiring, and driver primitives** — external code owns a `BoardCalibration` mode and calls `trigger()`, `BoardSystem` wires startup/trigger requests to `BoardCalibrationWorkflow`, and `BoardDriver` exposes narrow calibration primitives for raw sensor scans, raw pixel writes, and mapping table updates. This keeps first-boot/calibration UX out of the steady-state sensor scan path, avoids a `BoardDriver` dependency on calibration, and avoids friendship while still keeping `BoardDriver` private outside `src/board/`.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `game-mode.instructions.md` | Game modes consume `BoardGameplay`, not `Board` directly |
