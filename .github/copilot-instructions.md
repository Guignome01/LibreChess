# LibreChess - Project Instructions

## Project Overview
ESP32 Arduino smart chessboard: detects piece movements via hall-effect sensors + shift register, provides LED feedback via WS2812B strip, and communicates with Stockfish API / Lichess API / on-board LibreChess engine over WiFi. Built with PlatformIO (`esp32dev` board, Arduino framework).

## Architecture

### Class Hierarchy
**Core (`lib/core/`)**: Board representation, movegen, evaluation, search, UCI protocol, time management, notation, FEN, EPD parsing — the complete chess engine with zero Arduino dependencies. Game-end detection (check/checkmate/stalemate/draws) implemented as `Position` static methods. Search: `search` (fail-soft negamax + alpha-beta + quiescence (MVV-LVA ordered), iterative deepening, check extensions, recapture extensions, PVS, null move pruning (adaptive R = base + depth/4 + eval surplus), late move reductions (logarithmic table + history-informed + improving-aware + non-PV adjustment), late move pruning (improving-aware thresholds), history pruning (pre-make skip of quiet moves with deeply negative history at shallow depths), SEE capture pruning (prune captures with deeply negative SEE in main search, scaled by depth), reverse futility pruning (static null move pruning, margin/depth, halved when improving), razoring, lazy evaluation, aspiration windows (gradual doubling on fail), root move reordering, internal iterative reductions (IIR), delta pruning, futility pruning, pawn-defended-pawn QS pruning (skip non-pawn×defended-pawn captures in quiescence), lazy SEE capture ordering (all captures scored MVV-LVA + captureHistory, SEE computed lazily when yielded, losing captures deferred), mate distance pruning, singular extensions (exclusion search at TT-hit nodes, depth ≥ 6), transposition table, move ordering, countermove heuristic, improving flag (ply-2/ply-4 eval tracking), history gravity (unified bonus/penalty via gravity formula: h += bonus − h × |bonus| / MAX), triangular PV table). `findBestMove(pos, limits, state, info)` — infrastructure pointers (`timeFunc`, `tt`, `pawnHash`, `evalHash`) set via `SearchState` constructor. Opening book probe before iterative deepening when `SearchState::useBook` is true.

**Game (`lib/game/`)**: `Game` is the central game orchestrator composing `Position` (from core), `History` (in-memory move log + persistent game recording), and optionally `IGameObserver`. All chess-state mutations flow through `Game`. Also contains `IGameStorage` and `IGameObserver` interfaces.

**Dependency model**: `core ← game`. Game imports `search.h` for optional search resource ownership (`initSearch`, `calculateMove`). Firmware accesses search exclusively through Game — never imports core search internals directly.

**Firmware (`src/`)**: `GameMode` (abstract base, `src/game_mode/`) → `PlayerMode` (human v human) | `BotMode` (concrete, composes `EngineProvider*`). `EngineProvider` (base class with FreeRTOS task lifecycle, `src/engine/`) → `StockfishProvider` (`src/engine/stockfish/`) / `LichessProvider` (`src/engine/lichess/`) / `LibreChessProvider` (`src/engine/librechess/`). `BotMode::update()` drives a non-blocking state machine (`BotState::PLAYER_TURN` / `BotState::ENGINE_THINKING`); engine requests are async via FreeRTOS tasks. `BoardDriver` is shared via pointer injection. Each `GameMode` holds a `Game*` — no global chess state.

### Key Components
**Core library** (`lib/core/`): `Position`, `movegen`, `eval`, `eval_params`, `attacks`, `piece`, `utils`, `bitboard`, `zobrist`, `fen`, `notation`, `epd`, `hash_table.h`, `types.h`, `move.h`, `logger.h`, `search`, `search_params`, `MovePicker` (`move_picker.h` — staged move generation + heuristic updates), `TranspositionTable` (in `search.h` — inherits `HashTableBase`), `uci` (UCI protocol handler: `UCIState` + `loop`/`processLine`), `time_management.h` (time control computation), `stats.h` (`#ifdef STATS` counters), `book` (internal opening book: ~43 curated lines, `probe()` called from `findBestMove()` when `useBook` is true). Per-file instruction files auto-load when editing each module (see `core.instructions.md` for the full listing).

**Game library** (`lib/game/`): `Game` (orchestrator, lifecycle owner), `History` (move log + persistent recording), `IGameStorage`/`IGameObserver` (DI interfaces), `types.h` (game-management types: `GameHeader` packed struct with opaque `meta[]` byte array (mode, engineId, difficulty), recording constants, display-coordinate helpers (`rankChar`, `squareName(row,col)`) used by `Game` wrappers and firmware). Per-file instruction files: `game.instructions.md`, `history.instructions.md`, `game-headers.instructions.md`.

**Firmware** (`src/`): `BoardDriver` (LED + sensors + calibration), `WiFiManagerESP32` (web server + API + WiFi + NVS), `LittleFSStorage` (`IGameStorage` impl), `SerialLogger` (`ILogger` impl), `SystemUtils` (Arduino helpers), `SensorTest` (standalone sensor testing), `BoardMenu`/`MenuNavigator` (board-as-GUI).

### Coordinate System
Core internals use **LERF (Little-Endian Rank-File)** natively: a1=0, h8=63. Primary conversions: `rankOf(sq) = sq / 8`, `fileOf(sq) = sq % 8`, `makeSquare(rank, file) = rank * 8 + file`. The game layer and firmware use **row/col** display coordinates (row 0 = rank 8, col 0 = file a); bridge functions (`rowColToSquare`, `squareToRow`, `squareToCol`, `rankChar`, `squareName`) live in `game/types.h`.

## Build & Flash

### Commands
PlatformIO CLI (`pio`) is not on PATH by default. Use the full path:
- **Windows**: `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`
- **Linux**: `~/.platformio/penv/bin/pio`

| Action | CLI |
|--------|-----|
| Build | `pio run` |
| Upload | `pio run -t upload` |
| Serial Monitor | `pio device monitor` (115200 baud) |
| Factory reset | Add `-DFACTORY_RESET` to `build_flags` in `platformio.ini`, then flash |

### Build Pipeline
Three Python scripts run automatically (defined in `platformio.ini`): `minify.py` (HTML/CSS/JS minification), `prepare_littlefs.py` (gzip + place in `data/`), `upload_fs.py` (hash-based conditional filesystem upload). The `data/` directory is committed to git. Edit source files in `src/web/`, never in `data/`. Files named `*.nogz.*` skip gzip compression.

## Testing

Unit tests run natively on the host machine (no ESP32 required) using the PlatformIO Unity test framework. Two environments: `[env:native]` for all tests except statistics, `[env:native_stats]` (adds `-DSTATS`) for search statistics.

### Running Tests

| Action | Command |
|--------|--------|
| Run all tests | `pio test -e native -e native_stats` |
| Run lib tests | `pio test -e native -f test_core -f test_game` |
| Run positions | `pio test -e native -f test_positions_time -f test_positions_depth` |
| Run benchmarks | `pio test -e native -f test_benchmarks` |
| Run statistics | `pio test -e native_stats` |

For test architecture, file mirroring conventions, and per-file test group details, see `.github/instructions/testing.instructions.md` (auto-loaded when editing `test/` files).

### SPRT Testing

Engine-vs-engine strength testing via [fastchess](https://github.com/Disservin/fastchess). Validates that a code change does not regress (or confirms it gains Elo). Separate from unit tests — runs the native UCI engine (`tools/engine/librechess.exe`). See `tools/sprt/README.md` for usage.

## Code Style

C++ formatting: `.clang-format` (Google style base, no column limit). Run clang-format before committing.

## Engineering Principles

These principles are **non-negotiable** and apply to every code change — including refactors, new features, and internal restructuring. No change may violate them regardless of scope or complexity.

- **Separation of Concerns** — each class owns a single responsibility. Hardware in `BoardDriver`, chess rules in `movegen`, network in `WiFiManagerESP32`. Never mix concerns.
- **Loose Coupling** — pointer injection, no global state. Expose minimal public APIs, keep internals private.
- **Orchestrator via Game** — firmware accesses chess logic exclusively through `Game`. Never include `Position`, `History`, `movegen`, `piece`, or `utils` directly from firmware (`src/`). `Game` provides dual overloads (Square-native and row/col) for `makeMove`, `getSquare`, `getPossibleMoves`, `checkEnPassant`, `checkCastling`; Square-native is the primary implementation, row/col thin-wraps it for firmware convenience. Also re-exports `isDraw`, `forEachSquare`, and static utility wrappers (`isEmptySquare`, `pieceColor`, `pieceType`, `pieceToChar`, `colorName`, `squareName`, `fileChar`, `rankChar`). `checkEnPassant` and `checkCastling` are `Position` member methods (return `EnPassantInfo` / `CastlingInfo` from `types.h`) that delegate to `utils::checkEnPassant` / `utils::checkCastling` Square-based free functions; `Game` thin-wraps them. Native tests may include internal `lib/core/` headers.
- **Dependency Minimization** — prefer bundled ESP-IDF/Arduino functionality (`mbedtls`, FreeRTOS) over external libraries.
- **DRY** — extract shared logic into helpers, base classes, or utilities. No duplication.
- **Reuse Before Creating** — check existing functions/patterns first. Build on existing infrastructure.
- **Extend Before Inventing** — before adding standalone data (new parameters, arrays, fields), check whether existing infrastructure already carries related information. Extend existing structures and flows rather than introducing parallel mechanisms that duplicate context.
- **Cohesive Data Types** — when a fixed-size array and its count are always passed together, bundle them into a struct (e.g. `MoveList` for move generation output, `HashHistory` for Zobrist position history). Data and its length should travel as one unit, not as coupled out-params with raw index conventions.
- **Lookup Tables over Branching** — when mapping a small set of discrete inputs to outputs, prefer a `constexpr` array or lookup function over if/switch cascades. Simpler, faster, and less error-prone.
- **Chess Programming Wiki as Authority** — every search and evaluation technique must cite its CPW reference link in doc comments. Consult the [Chess Programming Wiki](https://www.chessprogramming.org/) before designing solutions — prefer established techniques over ad-hoc inventions.
- **ESP32 Awareness** — `constexpr` and compile-time computation, minimize heap (fixed arrays, file-scoped statics), `enum class` over raw integers. Be mindful of stack sizes and watchdog timers.
- **Performance & Readability** — optimize for performance, but prioritize readability over cleverness.
- **Security by Default** — validate inputs at boundaries, never expose secrets in APIs, hash credentials, use TLS.
- **Code Quality** — validate at boundaries, trust internally. Prefer compile-time checks.
- **Well-Documented Code** — every function, struct, namespace, and non-trivial block must have clear documentation. Use section banners (`// ---------------------------------------------------------------------------`) to visually separate logical groups within a file. Document *why* a design choice was made, not just *what* the code does. Public APIs require doc comments explaining purpose, parameters, return values, and caller context. File-local helpers and anonymous-namespace types need comments explaining their role in the larger algorithm. When code flow is non-obvious (e.g. bitwise tricks, pin/check mask filtering), add inline comments explaining the reasoning.
- **Documentation & Tests** — every code change must update affected docs and tests in the same operation, never deferred. When changing `lib/core/` or `lib/game/`: update the relevant per-file instruction file (e.g., `position.instructions.md`, `search.instructions.md`) and library-level instruction file (`core.instructions.md` or `game-library.instructions.md`), plus `architecture.md`, `project-structure.md`, `testing.instructions.md`, and unit tests as needed. See the Completion Checklist in each library instruction file and `docs/development/additional-topics.md` for full sync triggers.
- **Continuous Instruction Enrichment** — during any analysis, development, or debugging, proactively enrich scoped instruction files (`*.instructions.md`) with undocumented details discovered in the code: architectural decisions and their *why*, implied rules or invariants not yet written down, data flow patterns and caller/callee relationships, key implementation details that would save future investigation time, and project intent or design rationale found in comments or patterns. Do not wait for an explicit request — if something is learned that belongs in an instruction file, add it.