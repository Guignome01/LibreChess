# LibreChess - Project Instructions

## Project Overview
ESP32 Arduino smart chessboard: detects piece movements via hall-effect sensors + shift register, provides LED feedback via WS2812B strip, and communicates with Stockfish API / Lichess API / on-board LibreChess engine over WiFi. Built with PlatformIO (`esp32dev` board, Arduino framework).

## Architecture

### Class Hierarchy
**Core (`lib/core/`)**: Board representation, rules, movegen, evaluation, notation, FEN — the foundation layer with zero Arduino dependencies. Purely consumed by game and engine.

**Game (`lib/game/`)**: `Game` is the central game orchestrator composing `Position` (from core), `History` (in-memory move log + persistent game recording), and optionally `IGameObserver`. All chess-state mutations flow through `Game`. Also contains `IGameStorage` and `IGameObserver` interfaces.

**Engine (`lib/engine/`)**: `search` (fail-soft negamax + alpha-beta + quiescence (MVV-LVA ordered), iterative deepening, check extensions, recapture extensions, PVS, null move pruning (adaptive R = base + depth/4 + eval surplus), late move reductions (logarithmic table + history-informed + improving-aware), late move pruning (improving-aware thresholds), reverse futility pruning (static null move pruning, margin/depth, halved when improving), razoring, lazy evaluation, aspiration windows (gradual doubling on fail), root move reordering, internal iterative deepening (IID), delta pruning, futility pruning, SEE-based move ordering (cached for recapture extension reuse), mate distance pruning, singular extensions (exclusion search at TT-hit nodes, depth ≥ 6), transposition table, move ordering, countermove heuristic, improving flag (ply-2/ply-4 eval tracking), history gravity (unified bonus/penalty via gravity formula: h += bonus − h × |bonus| / MAX), triangular PV table) and `Engine` (direct-call facade: `calculateMove(fen, limits) → SearchResult`). Depends on core only.

**Dependency model**: `core ← game`, `core ← engine`. Game never imports engine and vice versa.

**Firmware (`src/`)**: `GameMode` (abstract base, `src/game_mode/`) → `PlayerMode` (human v human) | `BotMode` (concrete, composes `EngineProvider*`). `EngineProvider` (base class with FreeRTOS task lifecycle, `src/engine/`) → `StockfishProvider` (`src/engine/stockfish/`) / `LichessProvider` (`src/engine/lichess/`) / `LibreChessProvider` (`src/engine/librechess/`). `BotMode::update()` drives a non-blocking state machine (`BotState::PLAYER_TURN` / `BotState::ENGINE_THINKING`); engine requests are async via FreeRTOS tasks. `BoardDriver` is shared via pointer injection. Each `GameMode` holds a `Game*` — no global chess state.

### Key Components
**Core library** (`lib/core/`): `Position` (position container with `BitboardSet` + `Piece mailbox[64]`, move execution, game-end detection), `LibreChess` (bitboard) (bitboard types, LERF square mapping, `BitboardSet` struct), `attacks` (precomputed leaper tables + classical ray functions + attacked-by maps), `piece` (type-safe piece representation, all constexpr), `movegen`/`rules` (stateless static logic, bitboard-based attack detection), `utils` (board-level utility namespace), `eval` (tapered evaluation: material + phase-specific PSTs (all six piece types have separate MG/EG tables) + pawn structure + positional terms — bishop pair, bad bishop (penalty per own pawn on same color complex), rook on open/semi-open file, rook on 7th rank, rook behind passer (Tarrasch Rule, EG only), mobility (MG/EG split weights per piece type), king safety/pawn shield, knight outposts, center control, king danger (unified zone attack counting + proximity via nonlinear danger table, MG only), passed pawn rank-based exponential scaling + protected passer + candidate passer + king distance to passers (EG only, not pawn-hashed), space, trapped pieces, connectivity, threats (pawn→minor/rook/queen, minor→rook/queen, rook→queen), opposite-color bishop scaling (EG endgame, ×0.75) — interpolated by game phase; tempo bonus applied in search layer; pawn hash table caches pawn structure scores, eval hash table caches full evaluation results), `iterator` (bitboard-based iteration: `forEachSquare`, `forEachPiece`, `somePiece`, `findPiece`), `zobrist` (constexpr Zobrist key generation + incremental hashing + pawn-only hash for pawn hash table), `fen` (FEN parse/serialize), `notation` (coordinate/SAN/LAN conversion), `types.h` (core chess enums/structs: Color, Piece, GameResult, PositionState, HashHistory, MoveFormat), `move.h` (Move, MoveResult, MoveEntry, MoveList), `logger.h` (ILogger interface). Full API details in `.github/instructions/core-library.instructions.md`.

**Game library** (`lib/game/`): `Game` (orchestrator, lifecycle owner), `History` (move log + persistent recording), `IGameStorage`/`IGameObserver` (DI interfaces), `types.h` (game-management types: `GameHeader` packed struct with opaque `meta[]` byte array, recording constants).

**Engine library** (`lib/engine/`): `search` (fail-soft negamax + alpha-beta + quiescence (MVV-LVA ordered), iterative deepening, check extensions, recapture extensions, PVS, null move pruning (adaptive R = base + depth/4 + eval surplus), late move reductions (logarithmic table + history-informed + improving-aware), late move pruning (improving-aware), reverse futility pruning (margin/depth, halved when improving), razoring, lazy evaluation, aspiration windows (gradual doubling on fail), root move reordering, internal iterative deepening (IID), delta pruning, futility pruning, SEE-based move ordering (cached for recapture reuse), mate distance pruning, singular extensions (exclusion search at TT-hit nodes, depth ≥ 6), transposition table, move ordering, countermove heuristic, improving flag, history gravity (penalizes non-cutoff quiet moves on beta cutoff), triangular PV table), `Engine` (direct-call facade over search: `calculateMove(fen, limits) → SearchResult`).

**Firmware** (`src/`): `BoardDriver` (LED + sensors + calibration), `WiFiManagerESP32` (web server + API + WiFi + NVS), `LittleFSStorage` (`IGameStorage` impl), `SerialLogger` (`ILogger` impl), `SystemUtils` (Arduino helpers), `SensorTest` (standalone sensor testing), `BoardMenu`/`MenuNavigator` (board-as-GUI).

### Coordinate System
Board arrays use `[row][col]` where **row 0 = rank 8** (black's back rank), **col 0 = file a**. Internally, bitboards use LERF (Little-Endian Rank-File): a1=0, h8=63. Conversion: `squareOf(row, col) = (7 - row) * 8 + col`.

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

Unit tests run natively on the host machine (no ESP32 required) using the PlatformIO Unity test framework and the `[env:native]` build environment.

### Running Tests

| Action | Command |
|--------|--------|
| Run all tests | `pio test -e native` |
| Run a suite | `pio test -e native -f test_core` |

For test architecture, file mirroring conventions, and per-file test group details, see `.github/instructions/testing.instructions.md` (auto-loaded when editing `test/` files).

## Code Style

C++ formatting: `.clang-format` (Google style base, no column limit). Run clang-format before committing.

## Engineering Principles

These principles are **non-negotiable** and apply to every code change — including refactors, new features, and internal restructuring. No change may violate them regardless of scope or complexity.

- **Separation of Concerns** — each class owns a single responsibility. Hardware in `BoardDriver`, chess rules in `movegen`/`rules`, network in `WiFiManagerESP32`. Never mix concerns.
- **Loose Coupling** — pointer injection, no global state. Expose minimal public APIs, keep internals private.
- **Orchestrator via Game** — firmware accesses chess logic exclusively through `Game`. Never include `Position`, `History`, `movegen`/`rules`, or `iterator` directly from firmware (`src/`). `Game` re-exports iterator helpers (`forEachSquare`, `forEachPiece`, `somePiece`). Native tests may include internal `lib/core/` headers.
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
- **Documentation & Tests** — every code change must update affected docs and tests in the same operation, never deferred. When changing `lib/core/`: update `core-library.instructions.md`, `architecture.md`, `project-structure.md`, `testing.instructions.md`, and unit tests as needed. See the Completion Checklist in `core-library.instructions.md` and `docs/development/additional-topics.md` for full sync triggers.