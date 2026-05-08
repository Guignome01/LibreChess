---
description: "Use when modifying anything under src/board/** — public Board API, BoardRuntime, BoardDriver, BoardCanvas, BoardEffects, BoardInput, BoardRenderer, GUI helpers, menus, or workflows. Covers the runtime/canvas/effects model, layer semantics, status-handle contract, sensor cadence, and NVS persistence."
applyTo: "src/board/**, src/shared/utils.*"
---

# Board Firmware & Hardware Patterns

## Public surface

`src/board/board.*` is the public physical-board package root. `Board` exposes a
deliberately narrow API consumed by firmware outside the board folder:

- Lifecycle: `begin()` returns `false` if required runtime resources cannot start.
- LED settings: `getBrightness()`, `setBrightness()`, `getDimMultiplier()`,
  `setDimMultiplier()`, `saveLedSettings()`.
- Sensor cadence: `cadenceMs()` — every poll loop outside the board
  subsystem must `delay(board.cadenceMs())` rather than referencing
  `SENSOR_READ_DELAY_MS`.
- Canvas reset: `clearAllLayers()`.
- Status effects exposed as handles: `BoardEffectHandle startConnectingStatus()`
  and `void stopConnectingStatus(BoardEffectHandle&)`.
- Workflow accessors: `gameplay()`, `menu()`, `diagnostics()`, `calibration()`.

External firmware must not include `BoardDriver`, `BoardRuntime`,
`BoardCanvas`, `BoardEffects`, `BoardInput`, `BoardRenderer`, `BoardFeedback`,
or `BoardAssistance` directly. The only board-internal types it may name are
`BoardEffectHandle` (`board/core/effects.h`) and the selection types from
`board/gui/selection.h`.

## Internal layout

```
src/board/
├── board.{h,cpp}              public package root
├── config.{h,cpp}             menu config helpers
├── core/                      runtime primitives
│   ├── runtime.{h,cpp}        composes driver + canvas + effects + input + renderer
│   ├── driver.{h,cpp}         hall sensors, shift register, WS2812 strip, NVS
│   ├── canvas.{h,cpp}         7-layer pixel buffer + dirty flag
│   ├── effects.{h,cpp}        slot-based looping/transient animations
│   ├── input.{h,cpp}          pure occupancy snapshot + event queue
│   ├── renderer.{h,cpp}       FreeRTOS render task (~30 Hz)
│   └── colors.h               semantic LED palette
├── gui/                       visual helpers (take BoardRuntime&)
│   ├── feedback.{h,cpp}       always-on outcomes, status handles
│   ├── assistance.{h,cpp}     optional move/setup/capture guidance
│   ├── effect_animations.{h,cpp}  pure step functions used by BoardEffects
│   ├── layers.h               BoardLayer enum
│   └── selection.h            game-selection types (public)
├── menus/                     menu primitives
│   └── view.{h,cpp}           MenuView drawable + selection debouncer
└── workflows/                 long-lived workflows (take BoardRuntime&)
    ├── gameplay.{h,cpp}       physical chess interactions, holds feedback+assistance
    ├── diagnostics.{h,cpp}    sensor test
    ├── calibration.{h,cpp}    interactive calibration (friend of BoardRuntime)
    └── menu.{h,cpp}           game-selection state machine + confirm prompts
```

## BoardRuntime — the canvas/effects/IO boundary

`BoardRuntime` owns one `BoardDriver`, one `BoardCanvas`, one `BoardInput`,
one `BoardEffects`, and one `BoardRenderer`. It is constructed by `Board::Impl`
and shared with every workflow and visual helper through a `BoardRuntime&`
reference.

Key contracts:

- **All canvas mutation flows through `runtime.lockCanvas()`**, which returns a
  `CanvasGuard` RAII handle holding `canvas` and `effects` references under
  the runtime mutex. Hold the guard for the minimum scope needed; never store
  references to the underlying canvas/effects.
  ```cpp
  auto g = runtime.lockCanvas();
  g.canvas.setPixel(BoardLayer::FEEDBACK, row, col, LedColors::Cyan);
  g.effects.startBlink(row, col, LedColors::Green, 1, millis(),
                       BoardLayer::FEEDBACK);
  ```
- **Render task runs on Core 1, priority 1, ~30 Hz** (33 ms tick, 4 KiB stack).
  It picks up the dirty flag from the canvas and flushes through `BoardDriver`.
  Workflows never call `show()` directly.
- **Input task runs on Core 1, priority 1**, polling sensors at `cadenceMs()`
  (`SENSOR_READ_DELAY_MS` = 40 ms, 2 KiB stack). It produces
  `LIFTED`/`PLACED`/`BASELINE_SYNCED` events into a 16-slot queue. `BoardInput`
  is pure and not internally synchronized; `BoardRuntime` owns a dedicated
  input mutex and exposes short synchronized transactions:
  ```cpp
  BoardInputEventBatch batch = runtime.drainInputEvents();
  bool occupied = runtime.inputOccupied(row, col);
  bool board[8][8];
  runtime.copyInputOccupancy(board);
  ```
  Do not cache `BoardInput&` in workflows. Process drained batches outside the
  mutex. If `batch.overflowed` is true, discard the partial gesture and resync
  from current occupancy.

## BoardCanvas — 7-layer pixel buffer

```
enum class BoardLayer : uint8_t {
  BACKGROUND = 0, GAME, ASSISTANCE, FEEDBACK, MENU, EFFECT, OVERRIDE
};
```

Each layer stores an 8x8 `LedRGB` array plus one `uint64_t` presence mask
(row-major, one bit per square). `BoardCanvas::resolve(r, c)` returns the
topmost present layer's colour. `drawLine()` and `drawRing()` are available for
common assistance/feedback/effect geometry. Layer ownership:

| Layer       | Owner / use                                           |
|-------------|-------------------------------------------------------|
| BACKGROUND  | Long-lived ambient (currently unused)                 |
| GAME        | Diagnostics paint, future game-state hints            |
| ASSISTANCE  | Legal-move highlights, setup/remote/capture prompts   |
| FEEDBACK    | Resign progress, post-move blinks, illegal flashes    |
| MENU        | `MenuView` drawables, confirm prompts                 |
| EFFECT      | Transient/looping animations from `BoardEffects`      |
| OVERRIDE    | Reserved for hard overrides (calibration prompts)     |

Use `g.canvas.clearLayer(BoardLayer::X)` to wipe one layer; `clearAll()`
resets every layer (used by `Board::clearAllLayers()`).

## BoardEffects — slot-based animations

Six animation slots (`SLOT_COUNT = 6`). Effects are addressed by
`BoardEffectHandle{slot, generation}` — the 16-bit generation counter prevents
stale handles from cancelling a re-used slot. Helpers on `BoardEffects`:

- One-shot: `startBlink`, `startFlash`, `startCapture`, `startPromotion`,
  `startFirework`.
- Looping: `startThinking`, `startWaiting`, `startConnecting`.

All return a `BoardEffectHandle`. To stop a looping effect, hand the handle
back to the helper that owns it (e.g. `feedback_.stopAnimation(handle)`,
`Board::stopConnectingStatus(handle)`). After cancellation the handle is
invalidated. **Status animations no longer use `std::atomic<bool>*` flags.**

The renderer drives effect frames each tick via the pure step functions in
`gui/effect_animations.cpp` (`BoardEffectSteps` namespace). Full-layer effects
reuse a retained scratch canvas and then compose into their target layer, so
one active slot's empty squares do not erase sibling slots on the same layer
without allocating a full canvas on the render-task stack each frame. Effects
write to `BoardLayer::EFFECT` unless a helper specifies otherwise.

## Color Semantics

Colors in `LedColors` (`src/board/core/colors.h`) have **fixed semantic
meanings**. Never use a color for a different purpose.

| Color  | Meaning                | Usage                                    |
|--------|------------------------|------------------------------------------|
| Cyan   | Piece origin           | "Pick up from here"                      |
| White  | Valid move destination | Also: menu back button                   |
| Red    | Capture / error        | Capture square, illegal move, error flash |
| Green  | Confirmation           | Move confirmed, "yes" in dialogs         |
| Yellow | Check / promotion      | Check warning, promotion, random option  |
| Purple | En passant             | Captured pawn square; expert difficulty  |
| Orange | Resign gesture         | Brightness ramp during resign hold       |
| Blue   | Bot thinking           | Thinking animation, Human-vs-Human icon  |
| Lime   | Easy difficulty        | Difficulty menu                          |
| Crimson| Hard difficulty        | Difficulty menu                          |

`scaleColor(color, factor)` (`inline constexpr`) is used for brightness
progression.

## Sensor Grid

64 hall-effect sensors in 8×8 matrix, read via column-scanning with a
74HC595 shift register.

- **Polling**: `SENSOR_READ_DELAY_MS = 40 ms` (use `Board::cadenceMs()` outside
  `src/board/`).
- **Debounce**: `DEBOUNCE_MS = 125 ms` — piece must be stable for the full
  window inside `BoardDriver`. `BoardInput` then converts debounced state into
  `LIFTED`/`PLACED` events.
- **Edge detection** is centralised in `BoardInput`'s event queue. Workflows
  must not maintain their own previous-frame snapshots.

## Calibration

`BoardCalibration` lives in `workflows/calibration.{h,cpp}` and is a friend of
`BoardRuntime`. Two entry points:

- **Public `trigger()`** — invoked by WiFi/web UI for runtime recalibration.
- **Private `load()` / `run()` / `save()`** — invoked by `BoardRuntime::begin()`
  during startup. The friend declaration lets `BoardRuntime` construct a
  `BoardCalibration` instance bound to its own `BoardDriver&`.

NVS namespace `"boardCal"` stores `toLogicalRow[]`, `toLogicalCol[]`,
`ledIndexMap[8][8]`, and `swapAxes`. `BoardDriver` consults the mapping during
normal sensor/LED operations but does not include calibration logic.

## NVS Persistence

Settings stored via Arduino `Preferences`. Always call
`SystemUtils::ensureNvsInitialized()` from `src/shared/utils.*` before first
use. Key namespaces:

- `"boardCal"` — sensor/LED mapping tables.
- `"ledSettings"` — LED brightness and dim multiplier.

## Design Decisions

- **The canvas is the single source of truth** — every persistent visual is a
  layer write. The renderer never reads from workflows; workflows never write
  raw pixels. This makes layer ordering deterministic and lets multiple
  visuals coexist (e.g. legal-move highlight + thinking animation).
- **Effects are slot-based with handles** — looping animations need a stable
  identifier so a slow caller can cancel them safely. The generation counter
  in `BoardEffectHandle` prevents the "ABA" hazard of cancelling a slot that
  has already been recycled. `std::atomic<bool>*` flags are gone.
- **Input is event-driven** — moving edge detection into a single FreeRTOS
  task removes the manual `readSensors()` / `syncOccupancyBaseline()` calls
  that previously had to be sprinkled through every game-mode update loop.
  Runtime-level synchronized drains keep the producer task and workflow
  consumers from racing.
- **Calibration is one workflow with two entry points** — `trigger()` is
  public, `load()`/`run()`/`save()` are private and reachable only via the
  `friend BoardRuntime` declaration. This keeps first-boot calibration UX out
  of the steady-state sensor scan path.
- **Workflows own their visual helpers** — `BoardGameplay` holds a
  `BoardFeedback` and a `BoardAssistance` directly (constructed with the
  shared `BoardRuntime&`). Visual helpers no longer live on a controller
  facade; they are just objects scoped to their consumer.
- **Status effects are exposed as handles** — `BoardGameplay::startThinking`
  and friends return `BoardEffectHandle`. The caller stores the handle and
  passes it back to `stopStatusAnimation`. There is no "and-wait" semantics:
  the renderer is decoupled from the workflow so cancellation is immediate.
- **Colors have fixed semantics** — the table in `core/colors.h` is a
  project-wide contract. Never reuse a color for a different meaning.
- **The board root must not re-export internals** — if firmware outside
  `src/board/` needs new behaviour, expose it on the relevant workflow
  (`BoardGameplay`, `BoardMenu`, …) or add a narrow method to `Board`. Never
  widen `Board` with raw runtime/driver access.

## Related Instruction Files

| File                            | Relationship                              |
|---------------------------------|-------------------------------------------|
| `game-mode.instructions.md`     | Game modes consume `BoardGameplay`, not `Board` directly |
| `wifi-manager.instructions.md`  | WiFiManager uses `Board::startConnectingStatus`/`stopConnectingStatus` and `BoardCalibration::trigger()` |
