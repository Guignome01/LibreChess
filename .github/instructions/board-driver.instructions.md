---
description: "Use when modifying anything under src/board/** — public Board API, BoardRuntime, BoardDriver, BoardCanvas, BoardScheduler, BoardAnimations, BoardInput, BoardRenderer, visual helpers, menus, or programs. Covers the runtime/canvas/scheduler model, ordered surface semantics, status-handle contract, sensor cadence, and NVS persistence."
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
- Service tick: `update()` polls board-owned overlays/programs once and returns
  `Board::UpdateResult` completion flags.
- Canvas reset: `clearAllSurfaces()`.
- Animations: `startAnimation(const char* id)` returns a move-only
  `Board::Animation` token that stops automatically on destruction.
- Typed/named menu facade: `showMenu(BoardMenu&)`, `runMenu(BoardMenu&)`, and `stopMenu()`.
- Game program facade: `startGame()` returns the persistent `BoardGameProgram*`
  for game-mode integration (the instance is permanent and only resets transient
  state); `stopGame()` resets it.
- Polled program facade: `startProgram(const char* id)` returns `BoardProgram*`
  for built-in polled programs (e.g. diagnostics); `stopProgram()` cancels it.
- Facade helpers for external firmware: `resetCalibration()`.

External firmware must not include `BoardDriver`, `BoardRuntime`,
`BoardCanvas`, `BoardScheduler`, `BoardAnimations`, `BoardInput`,
`BoardRenderer`, `BoardFeedback`, or `BoardAssistance` directly. The only
board-facing types it may name are `Board::UpdateResult`,
`Board::Animation`, `BoardProgram`, `BoardGameProgram`, stable string ids, plus
typed menu objects from `board/menus/*` (`GameSelectionMenu`, `ConfirmMenu`,
`ResumeConfirmMenu`) when the caller needs typed menu result access.

## Internal layout

```
src/board/
├── board.{h,cpp}              public package root
├── assistance_provider.h      board-owned assistance provider contract
├── types.h                    engine-agnostic board DTOs
├── runtime/                   hardware/canvas/scheduler/input boundary
│   ├── runtime.{h,cpp}        composes driver + canvas + scheduler + input + renderer
│   ├── calibration.{h,cpp}    raw startup calibration runner
│   ├── driver.{h,cpp}         hall sensors, shift register, WS2812 strip, NVS
│   ├── canvas.{h,cpp}         ordered fixed-size surface stack + dirty flag
│   ├── scheduler.{h,cpp}      generic fixed-slot timed painter runner + callback contract
│   ├── input.{h,cpp}          pure occupancy snapshot + event queue
│   ├── renderer.{h,cpp}       FreeRTOS render task (~30 Hz)
│   ├── helpers.{h,cpp}        shared 8x8 dimensions + retained-surface helpers
│   └── colors.h               semantic LED palette
├── services/                  reusable board services over runtime
│   ├── visual/
│   │   ├── visual.{h,cpp}     BoardVisual retained-surface helper
│   │   └── animations.{h,cpp} animation API + frame painters
│   ├── program/
│   │   ├── program.{h,cpp}    BoardProgram + BoardProgramRunner
│   │   └── factory.{h,cpp}    fixed registry for named program creation
│   └── menu/                  board-owned typed menu runner primitives
│   │   ├── factory.{h,cpp}    fixed registry for named menu creation
│   │   ├── types.h            MenuOption + generic menu result constants
│   │   ├── panel.{h,cpp}      shared draw/poll/debounce mechanics
│   │   ├── selection.{h,cpp}  selectable page + optional back button
│   │   └── menu.{h,cpp}       BoardMenu contract + BoardMenuRunner
├── menus/                     predefined typed menus
│   ├── ids.h                  stable menu string ids
│   ├── factory.{h,cpp}        built-in menu registrations
│   ├── game_selection.{h,cpp} game selection tree + result types
│   └── confirm.{h,cpp}        green/red and resume confirmation menus
└── programs/                  primary programs and program-specific visuals
  ├── ids.h                  stable program string ids
  ├── factory.{h,cpp}        built-in program registrations
    ├── game/
  │   ├── game_program.h     BoardGameProgram interface consumed by game modes
    │   ├── game_rules.h       BoardGameRules contract consumed by gameplay
    │   ├── gameplay.{h,cpp}   physical chess interactions
    │   └── visuals/           game-only feedback and assistance visuals
    └── diagnostics/
        └── diagnostics_program.{h,cpp} sensor test BoardProgram
```

## BoardRuntime — the canvas/scheduler/IO boundary

`BoardRuntime` owns one `BoardDriver`, one `BoardCanvas`, one `BoardInput`,
one `BoardScheduler`, and one `BoardRenderer`. It is constructed by
`Board::Impl` and shared with board services, programs, and visuals through a
`BoardRuntime&` reference. `BoardAnimations` is owned by `Board::Impl` and
is injected into programs/helpers that need animation vocabulary. It receives
the runtime-owned scheduler/canvas references from `BoardRuntime` and all
scheduling/cancellation still happens while callers hold `lockCanvas()`.

Key contracts:

- **All canvas mutation flows through `runtime.lockCanvas()`**, which returns a
  `CanvasGuard` RAII handle holding the `canvas` reference under the runtime
  mutex. Hold the guard for the minimum scope needed; never store references to
  the underlying canvas. Animation scheduling/cancellation must also happen
  while this guard is held, using the injected `BoardAnimations&`.
  ```cpp
  auto g = runtime.lockCanvas();
  BoardCanvasHandle surface = BoardSurface::writable(g.canvas, surface_);
  g.canvas.setPixel(surface, row, col, LedColors::Cyan);
  animations.startBlink(row, col, LedColors::Green, 1, millis());
  ```
- **Render task runs on Core 1, priority 1, ~30 Hz** (33 ms cadence, 4 KiB stack).
  It asks `BoardScheduler` to run scheduled painters, picks up the dirty flag
  from the canvas, and flushes through `BoardDriver`.
  `BoardRenderer::stop()` uses a bounded graceful wait and reports whether the
  task acknowledged shutdown before any last-resort deletion. Programs never
  call `show()` directly.
- **Input task runs on Core 1, priority 1**, polling sensors at `cadenceMs()`
  (`SENSOR_READ_DELAY_MS` = 40 ms, 2 KiB stack). It produces
  `LIFTED`/`PLACED`/`BASELINE_SYNCED` events into a 16-slot queue. `BoardInput`
  is pure and not internally synchronized; it also tracks overflow diagnostics
  (`droppedEventCount` and max queue depth) until the runtime drains/clears the
  overflow state. `BoardRuntime` owns a
  dedicated input mutex and exposes short synchronized transactions:
  ```cpp
  BoardInputEventBatch batch = runtime.drainInputEvents();
  bool occupied = runtime.inputOccupied(row, col);
  bool board[8][8];
  runtime.copyInputOccupancy(board);
  ```
  Do not cache `BoardInput&` in programs. Process drained batches outside the
  mutex. If `batch.overflowed` is true, log/use the diagnostic fields, discard
  the partial gesture, and resync from current occupancy.
- **Shutdown is idempotent and bounded**: the renderer is stopped with a bounded
  wait, the input poll task is asked to exit cooperatively through task
  notification, and only then are runtime-owned mutexes/semaphores released.
  Timeout fallbacks log the failure before last-resort task deletion.

## BoardCanvas — ordered surface stack

`BoardHelpers` (`core/helpers.h`) is the shared logical-board dimension source
for canvas, input, menus, diagnostics, and board DTOs. Prefer
`BoardHelpers::ROWS`, `COLS`, `SQUARES`, and `inBounds()` over local 8/64
constants in board code. The same helper module also exposes `BoardSurface`,
the retained-surface lifecycle helper used by visual owners.

`BoardCanvas` owns a fixed pool of ordered 8x8 surfaces. Each active surface
stores one `LedRGB[8][8]`, a `uint64_t` presence mask, a generation counter,
and an insertion/activation order. `BoardCanvas::resolve(r, c)` returns the
newest active surface that has presence at that square. No heap allocation is
used.

All visual owners use explicit `BoardCanvasHandle` values. Persistent helpers
store their handle as a member and use `BoardSurface::writable()`
(`core/helpers.h`) under the canvas guard to lazily acquire/re-front the retained
surface before painting. Use `BoardSurface::clear()` / `clearSquare()` /
`release()` for active-handle-safe cleanup. `drawLine()` and
`drawRing()` are available for common geometry on a chosen surface. `clearAll()`
blanks every active surface and is used by `Board::clearAllSurfaces()` before
scheduled animations are also cancelled.

## BoardScheduler and BoardAnimations

`BoardScheduler` (`runtime/scheduler.*`) owns the generic logical-frame paint
contract (`BoardPainter`: callback + fixed-size copied context + paint mode)
and six timed painter slots (`SLOT_COUNT = 6`). Scheduled painters are addressed by
`BoardScheduledHandle{slot, generation}`; the 16-bit generation counter prevents
stale handles from cancelling a re-used slot. The scheduler has no animation
vocabulary. It owns slot allocation, start time, duration/looping,
cancellation, and one canvas surface per scheduled painter. Full-surface painters
clear only their own surface before painting each frame, so sibling animations
do not erase each other.

`BoardAnimations` (`services/visual/animations.*`) is the board-owned visual animation API and frame
painting implementation. `Board::Impl` owns one instance next to `BoardRuntime`
and constructs it with the runtime-owned `BoardScheduler&` and `BoardCanvas&`.
It converts animation requests into scheduled painters and exposes
`BoardAnimationHandle` (an alias of `BoardScheduledHandle` in
`board/services/visual/animations.h`) for callers.
Helpers:

- One-shot: `startBlink`, `startFlash`, `startCapture`, `startPromotion`,
  `startFirework`.
- Looping: `startThinking`, `startWaiting`, `startConnecting`.

Internal animation helpers return a `BoardAnimationHandle`. Inside board-owned
visuals, stop a looping animation by handing the handle back to the helper that
owns it (e.g. `feedback_.stopAnimation(handle)`). External firmware uses
`Board::startAnimation(id)` and holds the returned `Board::Animation` token;
the token stops automatically on destruction and can be stopped early with
`token.stop()`. **Status animations do not use `std::atomic<bool>*` flags.**

## BoardMenuRunner and predefined menus

Generic menu mechanics live under `src/board/services/menu/`. `MenuPanel` owns the
canvas surface, orientation transform, occupancy polling, two-phase debounce,
and selection blink. `MenuSelection` stores one fixed-size page and optional
white back button. `BoardMenuRunner` is the only class that polls input for
menus; it implements `BoardMenuController` and drives typed `BoardMenu` objects
through `begin()`, `onSelect()`, `onBack()`, and `cancel()` hooks.

Predefined menu flows live under `src/board/menus/` and are passed to `Board` at
runtime:

- `GameSelectionMenu` owns the root/difficulty/color state machine and exposes
  `BoardGameSelection` / `BoardGameSelectionMode` after completion.
- `ConfirmMenu` owns the green/red yes/no result.
- `ResumeConfirmMenu` extends confirmation with the mode-coloured resume blink.

Menus define option layouts and semantic transitions only. They must not call
`BoardRuntime`, drain `BoardInput`, or run their own polling loops. Use
`Board::showMenu(menu)`, `Board::runMenu(menu)`, `Board::stopMenu()`, and
`Board::update()` outside `src/board/`.

## Color Semantics

Colors in `LedColors` (`src/board/runtime/colors.h`) have **fixed semantic
meanings**. Never use a color for a different purpose. `colors.h` is
self-contained and must not include chess/game headers; tiny Game-facing color
choices should stay local to the GUI implementation that renders them.

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
- **Edge detection** is centralised in `BoardInput`'s event queue. Programs
  must not maintain their own previous-frame snapshots.

## Calibration

`BoardCalibrationRunner` lives in `runtime/calibration.{h,cpp}` and is invoked by
`BoardRuntime::begin()` during startup for `load()` / `run()` / `save()`.
Runtime recalibration is exposed directly on `Board::resetCalibration()`,
which clears the calibration NVS namespace and reboots so startup calibration
runs again.

NVS namespace `"boardCal"` stores `toLogicalRow[]`, `toLogicalCol[]`,
`ledIndexMap[8][8]`, and `swapAxes`. `BoardDriver` consults the mapping during
normal sensor/LED operations but does not include program-level calibration logic.

## NVS Persistence

Settings stored via Arduino `Preferences`. Always call
`SystemUtils::ensureNvsInitialized()` from `src/shared/utils.*` before first
use. Key namespaces:

- `"boardCal"` — sensor/LED mapping tables.
- `"ledSettings"` — LED brightness and dim multiplier.

## Design Decisions

- **The canvas is the single source of truth** — every persistent visual writes
  to an explicit `BoardCanvasHandle` surface. The renderer never reads from
  programs; programs never write raw pixels. Insertion-order composition keeps
  multiple visuals deterministic without enum-based priority or roles.
- **Scheduled visuals are slot-based with handles** — looping animations need a
  stable identifier so a slow caller can cancel them safely. The generation
  counter in `BoardScheduledHandle`/`BoardAnimationHandle` prevents the "ABA"
  hazard of cancelling a slot that has already been recycled.
- **Input is event-driven** — moving edge detection into a single FreeRTOS
  task removes the manual `readSensors()` / `syncOccupancyBaseline()` calls
  that previously had to be sprinkled through every game-mode update loop.
  Runtime-level synchronized drains keep the producer task and program
  consumers from racing.
- **Startup calibration is runtime-owned, runtime recalibration is Board-owned** —
  `BoardCalibrationRunner` owns serial-guided raw startup calibration over
  `BoardDriver`; `Board::resetCalibration()` only clears persisted mapping
  and reboots. `BoardRuntime` no longer depends on program calibration or
  exposes friend raw-driver access.
- **Programs own their visual helpers** — `BoardGameplay` holds a
  `BoardFeedback` and a `BoardAssistance` directly (constructed with the
  shared `BoardRuntime&` plus the board-owned `BoardAnimations&`). Visual
  helpers no longer live on a controller facade; they are just objects scoped
  to their consumer.
- **Menus are typed objects, not programs** — `BoardMenuRunner` owns all menu
  polling/debounce/rendering. Predefined menus under `board/menus/` are small
  state machines that define which page to show and what result to record when
  a square is selected. This keeps game-selection/resume/resign prompts reusable
  without exposing runtime internals or creating another primary program.
- **Board gameplay is engine-agnostic** — `types.h` defines the board-owned
  DTOs, `programs/game/game_rules.h` defines the board-owned `BoardGameRules`
  contract, and `assistance_provider.h` defines the board-owned
  `BoardAssistanceProvider` contract. `BoardGameplay`, `BoardAssistance`, and
  `BoardFeedback` consume only these mapped structures; they must not include
  `Game`, `MoveList`, `MoveResult`, concrete engine providers, or search APIs.
  `Board` owns the active assistance provider. `NONE` and `LEGAL_MOVES` are
  fixed board providers that never call an engine. BEST_MOVE is the only
  assistance level allowed to use an engine-backed provider.
- **Status animations are exposed as move-only tokens** —
  `BoardGameProgram::startThinkingStatus()` / `startWaitingStatus()` return
  `BoardAnimationToken` values. The caller stores the token in a member or
  local; destruction or `stop()` cancels the animation by acquiring the
  canvas lock. The renderer is decoupled from the program so cancellation is
  immediate. External firmware uses `Board::Animation` (alias of
  `BoardAnimationToken`).
- **Colors have fixed semantics** — the table in `runtime/colors.h` is a
  project-wide contract. Never reuse a color for a different meaning.
- **The board root must not re-export internals** — if firmware outside
  `src/board/` needs new behaviour, expose it on the relevant program,
  implement a typed `BoardMenu`, or add a narrow method to `Board`. Never widen
  `Board` with raw runtime/driver access.

## Related Instruction Files

| File                            | Relationship                              |
|---------------------------------|-------------------------------------------|
| `game-mode.instructions.md`     | Game modes consume `BoardGameProgram`, not `Board` directly |
| `wifi-manager.instructions.md`  | WiFiManager uses `Board::startAnimation("connecting")` and `Board::resetCalibration()` |
