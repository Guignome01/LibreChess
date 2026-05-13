# Project Standards

Conventions and coding guidelines for contributing to LibreChess. For detailed technical architecture (component internals, data flows, concurrency, storage), see [architecture.md](architecture.md).

## Code Style

Formatting is enforced via `.clang-format` at the project root (Google style base, no column limit). Run clang-format before committing to keep the codebase consistent.

## Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Classes | PascalCase | `BoardDriver`, `Position`, `WiFiManagerESP32` |
| Methods & variables | camelCase | `readSensors()`, `sideToMove`, `isGameOver` |
| Constants & macros | UPPER_SNAKE_CASE | `LED_COUNT`, `SENSOR_READ_DELAY_MS`, `DEBOUNCE_MS` |
| File names | snake_case | `driver.cpp`, `game.h`, `storage/littrefs.cpp` |
| Enum values | UPPER_SNAKE_CASE | `GameResult::CHECKMATE`, `Axis::ROWS` |

## Architecture Principles

### Separation of Concerns

Each class owns a single responsibility and never crosses into another's domain:

- `Board` is the public physical-board package root for lifecycle (`begin()` returns `bool`), LED settings, timing, status helpers, `clearAllSurfaces()`, and owned workflow access. It privately owns one `BoardRuntime`, which composes `BoardDriver`, `BoardCanvas`, `BoardInput`, `BoardScheduler`, `BoardAnimations`, and `BoardRenderer`. `BoardGameplay`, `BoardDiagnostics`, `BoardCalibration`, and `BoardMenu` are long-lived workflows under `src/board/workflows/` that take a `BoardRuntime&`; outside firmware reaches them through `Board` accessors instead of constructing them separately. `BoardCanvas` owns a fixed-size ordered surface stack; `BoardScheduler` owns generic timed painter slots addressed by `BoardScheduledHandle` and gives each scheduled painter its own canvas surface; `BoardAnimations` owns board-specific animation helpers addressed by `BoardAnimationHandle`; `BoardRenderer` runs the FreeRTOS render task that flushes the resolved frame through `BoardDriver` and stops through a bounded wait; `BoardRuntime` runs the cooperative sensor poll task and synchronizes access to the pure `BoardInput` event ring/overflow diagnostics through input helper methods. `BoardDriver` handles low-level hardware interaction (LEDs, sensors, settings). No board module mutates chess state.
- `movegen`/`rules` namespaces implement chess rules and move generation. No hardware access, no network calls.
- `WiFiManagerESP32` manages WiFi, the web server, and API endpoints. Doesn't touch the board hardware directly.
- `History` + concrete storage backends such as `LittleFSStorage` own game persistence. Don't know about sensors or LEDs.

When adding new functionality, determine which component owns that concern and extend it there. If the functionality crosses boundaries, coordinate through the main loop or injection — don't let one component reach into another's internals.

### Dependency Injection

Components are connected through pointer injection at construction time, not through global state or singletons. This keeps dependencies explicit and testable. A game mode receives exactly the services it needs — nothing more.

### DRY and Reuse

Before writing new code, check whether an existing function or pattern covers the need. Shared logic is extracted into helpers (`utils`), base classes (`Game`), or utility functions. If the same pattern appears in more than one place, refactor it into a single reusable implementation.

New features should build on existing infrastructure. For example, `LichessProvider` implements the `EngineProvider` interface and is composed into `BotMode` (which extends `GameMode`) rather than reimplementing bot-move guidance from scratch.

## LED Access Rules

- All canvas mutation inside `src/board/` must go through `BoardRuntime::lockCanvas()`, which returns a `CanvasGuard` RAII handle (canvas + animations refs under the runtime mutex). Code outside `src/board/` calls semantic `Board` methods (e.g. `clearAllSurfaces()`, `startConnectingStatus()`); raw LED writes are not exposed.
- One-shot animations (blink, flash, capture, promotion, firework) are launched via `BoardAnimations::start*` helpers; they auto-clear when their duration expires.
- Looping animations (thinking, waiting, connecting) return a `BoardAnimationHandle{slot, generation}`; cancel by handing the handle back to the helper that owns it (e.g. `BoardGameplay::stopStatusAnimation(handle)`, `Board::stopConnectingStatus(handle)`). Generation counters guard against ABA on slot reuse — stale handles silently no-op.
- Colors in `LedColors` have fixed semantic meanings (see [architecture.md](architecture.md#color-semantics)). Use consistently.

- Outside `src/board/`, use `Board::cadenceMs()` instead of board-local macros such as `SENSOR_READ_DELAY_MS`.

## ESP32 Patterns

- **Prefer `constexpr` and PROGMEM** for data that doesn't change at runtime.
- **Minimize heap allocations** — use fixed-size arrays, stack allocation, and file-scoped statics over dynamic containers.
- **Strong types** — prefer `enum class` over raw integers to catch errors at compile time.
- **Be mindful** of stack sizes, watchdog timers, and FreeRTOS task priorities.

## Frontend Conventions

### API Layer

The frontend uses a two-file pattern for server communication:

- `api.js` — low-level utilities: `getApi()`, `postApi()`, `deleteApi()` fetch wrappers, and `pollHealth()` for OTA reboot detection.
- `provider.js` — domain-specific `Api` object with a named method for every backend endpoint (e.g., `Api.getBoardUpdate()`, `Api.resign()`, `Api.selectGame()`).

All HTML pages call `Api.*` methods. No page contains raw `fetch()` calls to API endpoints. When adding a new endpoint, add a corresponding method to `provider.js` and use it from the page.

### Web Asset Pipeline

- Edit source files in `src/web/` — HTML, CSS, JS, SVG, MP3
- Never edit files in `data/` directly — they are generated by the build scripts
- Files named `*.nogz.*` skip gzip compression (used for binary assets like MP3s that don't benefit from gzip)
- The build pipeline minifies → gzips → places in `data/`. See the [installation guide](installation.md) for details.

## Security Conventions

- **Input validation** at system boundaries — API handlers validate all parameters before processing. Internal functions trust data that has already been validated.
- **No secrets in responses** — WiFi passwords, Lichess tokens, and OTA passwords are never returned in API responses.
- **Credential hashing** — OTA passwords are stored as salted SHA-256 hashes (using mbedtls, bundled with ESP-IDF). The raw password is never persisted.
- **TLS encryption** — external API connections (Lichess, Stockfish) use `WiFiClientSecure` with TLS encryption.
- **Minimal dependencies** — prefer `mbedtls` (bundled) over external crypto libraries. Prefer built-in FreeRTOS primitives over third-party abstractions.

## Testing Conventions

- **Tests guard correctness** — never modify a test to make it pass. If a test fails, fix the production code.
- **Tests must stay in sync** — when changing chess logic in `lib/core/`, update or add tests in the same change.
- **Always test changes** — every logic change must be validated by running the test suite (`pio test -e native`) before committing.
- **Core library boundary** — only code in `lib/core/` (no Arduino dependencies) is testable natively. Arduino/ESP32 utilities live in `src/shared/utils.*`.
- **Board setup** — tests that call `getPossibleMoves()` must always have both kings on the board (the engine filters moves that leave the king in check, and returns no moves if no king is found).

For details on test architecture and suites, see [project-structure.md — Unit Tests](project-structure.md#unit-tests-test).
