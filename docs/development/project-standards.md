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

- `Board` is the shared physical-board base layer for capabilities that are true in every workflow: lifecycle, root/resume menus, display clearing/status primitives, LED settings, timing, and restricted access to `BoardSystem`/`BoardGui` for board-owned workflows. `BoardGameplay`, `BoardDiagnostics`, and `BoardCalibration` are purpose-specific workflows composed around that base and imported directly for their use case. `BoardGui` is the internal visual coordinator for feedback, assistance, menus, modal stack flow, and `BoardLayering`. `BoardLayering` owns persistent base + overlay visual composition. `BoardSystem` is the internal hardware/settings/scheduler service boundary consumed by board-local modules. `BoardDriver` handles low-level hardware interaction (LEDs, sensors, settings); `BoardScheduler` owns animation queue concurrency; `BoardGameplay` owns physical chess interaction transitions. No board module mutates chess state.
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

- Multi-step LED updates inside `src/board/` must use `BoardLayering` for persistent base/overlay visuals or `BoardSystem::batchLEDs()` for low-level LED batches rather than manual mutex handling. Code outside `src/board/` should call semantic `Board` methods instead of raw LED APIs.
- Single animations are factory-built `AnimationJob` values submitted through `BoardSystem::runAnimation()` and acquire the mutex automatically — no guard needed.
- Long-running animations start through `BoardSystem::startAnimation(AnimationType::THINKING/WAITING)` and return `std::atomic<bool>*` — cancel via `stopAndWaitForAnimation(flag)`.
- Use `waitForAnimationQueueDrain()` inside board-local code as a barrier before writing LEDs directly.
- Colors in `LedColors` have fixed semantic meanings (see [architecture.md](architecture.md#color-semantics)). Use consistently.

- Outside `src/board/`, use `Board::sensorReadDelayMs()` instead of board-local macros such as `SENSOR_READ_DELAY_MS`.

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
