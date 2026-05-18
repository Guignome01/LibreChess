# Project Structure

A comprehensive map of the codebase, covering firmware, web frontend, build tools, and runtime storage.

## Top-Level Layout

```
├── src/                    Firmware source code and web frontend sources
├── lib/core/               Chess engine — board representation, rules, movegen, evaluation, search, UCI protocol, time management
├── lib/game/               Game orchestrator — Game, History, recording, DI interfaces, pure engine provider contract
├── test/                   Native unit tests (PlatformIO Unity framework)
├── data/                   Pre-built web assets (gzip-compressed) for LittleFS
├── docs/                   Project documentation
├── BuildGuide/             Build photos and schematics (to be updated)
├── platformio.ini          PlatformIO build configuration
├── LibreChess.code-workspace VS Code workspace file
├── grid_scan_test.cpp      Standalone sensor grid debugging utility
├── .clang-format           C++ code formatting rules (Google style base)
├── .github/                Copilot instructions, scoped instruction files, and workflow skills
├── LICENSE                 Project license
└── README.md               Project overview and quick start
```

## Firmware (`src/`)

### Core

| File | Purpose |
|------|---------|
| `main.cpp` | Entry point: `setup()` and `loop()`. Game mode startup, WiFi/resign/board-edit relay, and game lifecycle management. Owns the shared public `Board`, a typed `MainMenu`, and a small `MainMenuHost` adapter; physical menu open/reopen handling lives in `MainMenu`, while every program (game, diagnostics, calibration) is launched through `Board::startProgram(id)` with ids from `board/programs/ids.h`. Game modes receive the active program downcast to `IBoardGame*` after `Board::startProgram(BoardProgramIds::GAME)`. |
| `board/board.h/.cpp` | Public physical-board package root. Owns one internal `BoardRuntime`, board services (`BoardAnimations`, `BoardMenuRunner`, `BoardProgramRunner`), a fixed program factory, and the active `BoardAssistanceProvider`; tracks the currently active `IBoardGame*` as an observer pointer (cleared on `stopProgram`). Exposes a narrow public API (`begin()` lifecycle result, LED settings, sensor cadence, `update()`, `clearAllSurfaces()`, animation tokens, assistance provider installation, typed/named menu facade, and a single `startProgram(id)`/`stopProgram()` facade returning `BoardProgram*`). Game-mode integration is a `static_cast<IBoardGame*>` of the returned pointer (only the `GAME` factory entry produces an `IBoardGame`). Keeps raw board internals and concrete programs private. |
| `board/runtime/runtime.h/.cpp` | Board-internal hardware/canvas/scheduler/IO boundary. Owns `BoardDriver`, `BoardCanvas`, `BoardInput`, `BoardScheduler`, and `BoardRenderer`; exposes `lockCanvas()` for synchronized canvas mutation, board-owned GUI adapter access to the scheduler/canvas, and synchronized input helpers (`drainInputEvents()`, `copyInputOccupancy()`, `inputOccupied()`); runs startup calibration through `BoardCalibrationRunner`; and shuts down renderer/input tasks with bounded cooperative waits. |
| `board/runtime/calibration.h/.cpp` | `BoardCalibrationRunner`: startup `load()`/`run()`/`save()` flow over raw `BoardDriver` access before renderer startup. Owns serial-guided calibration and NVS mapping persistence (`boardCal` namespace). |
| `board/runtime/driver.h/.cpp` | Hardware abstraction: LED strip (NeoPixelBus, I2S DMA), sensor grid (shift register scan + GPIO reads), saved calibration mapping application, LED settings (brightness, dimming), and GPIO pin definitions. |
| `board/runtime/canvas.h/.cpp` | Fixed-size ordered surface stack. Each surface stores `LedRGB[8][8]` plus a `uint64_t` presence mask; `resolve(r, c)` returns the newest present surface colour; includes rect/fill/line/ring drawing helpers; `dirty()` flag picked up by the render task. |
| `board/runtime/scheduler.h/.cpp` | Generic fixed-slot timed painter runner (6 slots, `BoardScheduledHandle{slot, generation}`) and `BoardPainter` callback contract. Owns allocation, immediate cancellation, expiration, looping, fixed copied context storage, and one canvas surface per scheduled painter; generation counter guards against ABA on slot reuse. |
| `board/runtime/input.h/.cpp` | Pure occupancy snapshot + 16-slot event ring (`LIFTED`/`PLACED`/`BASELINE_SYNCED`) with dropped-event count and max-depth diagnostics. Fed by the `BoardRuntime` poll task; direct access is not thread-safe, so programs use `BoardRuntime` input helpers. |
| `board/runtime/renderer.h/.cpp` | FreeRTOS render task on Core 1 (~30 Hz, 4 KiB stack). Runs active scheduled painters, picks up the canvas dirty flag, flushes the resolved frame through `BoardDriver`, and stops through a bounded graceful wait. |
| `board/runtime/helpers.h/.cpp` | Shared logical board dimensions and coordinate helpers (`BoardHelpers::ROWS`, `COLS`, `SQUARES`, `LAST_ROW`, `LAST_COL`, `inBounds()`) plus `BoardSurface`, the retained-surface helper for visual owners. Used by board DTOs, canvas, input, menus, diagnostics, visual helpers, and tests. |
| `board/services/menu/types.h` | Generic menu data: `MenuOption`, no-selection/back sentinels, and fixed option-count constants. |
| `board/services/menu/selection.h/.cpp` | `MenuSelection` shared physical-board menu primitive: owns one canvas surface, paints options through `runtime.lockCanvas()`, snapshots occupancy once per poll, debounces empty-then-occupied piece placement, handles orientation flipping, clears the selected tile on stable press, stores one selectable page plus optional white back button, and returns option ids on stable release without interpreting them. |
| `board/services/menu/menu.h/.cpp` | Declarative menu contract and runner: `MenuTile`, `MenuPageConfig`, `MenuAdvance{STAY,NEXT,CLOSE}`, the `MenuFlow` virtual interface (`next`/`back`/`close`/`currentPage`/`blink`/`wait`), the typed `BoardMenu` contract (`tiles`/`tileCount`/`initialPage`/`pageConfig` + `onOpen`/`onNext`/`onBack`/`onSelect`/`onClose` hooks), and `BoardMenuRunner`. The runner owns the fixed page stack (`MENU_PAGE_STACK_DEPTH = 8`), polling/drawing/debounce/blocking cadence loops, and implements `MenuFlow` privately so hooks can queue transitions applied after each hook returns (explicit hook transitions take priority over a tile's `autoAdvance`). Typed menu instances are passed directly by callers (no factory layer). |
| `board/services/program/program.h/.cpp` | Polled board program contract and runner: `BoardProgram` plus `BoardProgramRunner`. The runner owns one active program slot, starts/cancels replacements, polls the active program, reports completion, and owns a factory-created `unique_ptr`. Menus remain independent overlays. Every program — game, diagnostics, and calibration — flows through this runner uniformly. |
| `board/services/program/factory.h/.cpp` | Fixed-size program factory used by `Board` to create built-in programs (game, diagnostics, calibration) by stable string id. |
| `board/runtime/colors.h` | Self-contained `LedRGB` struct, named color constants (Cyan, White, Red, Green, Yellow, Purple, Orange, Blue, etc.), and `scaleColor()` brightness helper. Does not include chess/game headers. |
| `board/services/visual/visual.h/.cpp` | `BoardVisual`: lightweight retained-surface base/helper for visuals that own one `BoardCanvasHandle`. It centralizes lazy surface acquisition, clearing, square clearing, and release while leaving drawing policy in concrete visuals. |
| `board/services/visual/animations.h/.cpp` | Board-owned visual animation vocabulary, helper API, timing constants, status handle alias, and frame painters. Converts animation requests into `BoardPainter` callbacks scheduled by `BoardScheduler`; native-testable without Arduino/FreeRTOS. |
| `board/services/visual/animation_token.cpp` | Implementation of `BoardAnimationToken` — move-only RAII handle that cancels its animation on destruction by acquiring the runtime canvas lock. Separate TU so native tests can include animations without pulling `BoardRuntime`/Arduino headers. |
| `board/menus/selection_types.h` | Shared `BoardGameSelection` and `BoardGameSelectionMode` types used by physical menus, firmware, and resume confirmation. |
| `board/menus/main.h/.cpp` | `MainMenu`: predefined root mode picker, stable root tile ids, mode-colour helper for resume prompts, prompt text, physical menu lifecycle helpers (`open`, `canOpen`, `update`) via `MainMenuHost`, and internal routing into the embedded bot setup menu. |
| `board/menus/game_selection.h/.cpp` | `GameSelectionMenu`: predefined bot setup flow (difficulty/color), option ids, and bot-specific `BoardGameSelection` capture. Used internally by `MainMenu` and independently tested. |
| `board/menus/confirm.h/.cpp` | `ConfirmMenu` and `ResumeConfirmMenu`: predefined green/red prompts used by resume and resign confirmation. Passed to `Board::runMenu()` when callers need typed result access, or created by menu id for simple overlays. |
| `board/types.h` | Engine-agnostic board gameplay DTOs (legal target lists, target ranking, setup snapshots, move feedback/completion data). These are the data shapes that keep `src/board/` independent from `Game`, `MoveList`, and concrete engines. |
| `board/programs/game/game_provider.h` | Board-owned game rules contract (`BoardGameProvider`) consumed by `BoardGame` for setup, lifted-piece lookup, and physical move validation. Concrete rules implementations live outside `src/board/`. |
| `board/assistance_provider.h` | Board-owned assistance provider contract plus fixed NONE and LEGAL_MOVES providers. BEST_MOVE providers live outside `src/board/` and return `BoardMoveTargetRanking` data only. |
| `board/programs/ids.h` | Stable string ids for primary board programs (`BoardProgramIds::GAME`, `DIAGNOSTICS`, `CALIBRATION`). |
| `board/programs/factory.h/.cpp` | Built-in program registration for `BoardProgramFactory`; this is the only board-level source that imports concrete game/diagnostics/calibration headers. |
| `board/programs/game/program_provider.h` | `IBoardGame` interface consumed by game modes. Keeps firmware mode code independent from the concrete `BoardGame` class. |
| `board/programs/game/program.h/.cpp` | Board game physical-interaction program component. Implements `IBoardGame`, takes `BoardRuntime&`, `BoardAnimations&`, and `BoardMenuRunner&`; holds game-specific visuals (`BoardFeedback` + `BoardAssistance`); drains synchronized `BoardInputEventBatch` values for player intent, detects lifts/placements/captures/resign gestures, validates against mapped legal targets, requests BEST_MOVE target rankings on piece lift, runs resign confirmation through a typed `ConfirmMenu`, drives setup/remote/castling guidance, and clears both game visual surfaces on reset/cancel. Never mutates chess state. |
| `board/programs/game/visuals/feedback.h/.cpp` | Game-program visual feedback/status helper. Owns a retained `BoardVisual` surface for resign progress and starts illegal move blink, mapped move-result, check/game-end, thinking/waiting/error animations. Status animations return `BoardAnimationHandle` values whose cancellation releases the scheduled surface immediately before later feedback such as fireworks. |
| `board/programs/game/visuals/assistance.h/.cpp` | Game-program physical guidance visual helper. Owns a retained `BoardVisual` surface for board setup prompts, configurable legal-move/best/worst-move assistance, castling/remote move completion, and capture placement prompts that preserve the rendered target color. Setup prompts verify occupancy before painting and require mismatches to persist briefly before LEDs are shown, keeping already-correct setup transitions clean. Consumes board-owned setup/target/ranking DTOs only; never imports `Game` or engine providers. |
| `board/programs/diagnostics/program.h/.cpp` | Sensor Test program. Implements `BoardProgram`, copies current debounced occupancy through `BoardRuntime`, records visited squares, lights them white on an owned `BoardVisual` surface, and completes with an injected `BoardAnimations&` firework when all 64 are visited. |
| `board/programs/calibration/program.h/.cpp` | Runtime recalibration program. Implements `BoardProgram`; its `update()` clears the `"boardCal"` NVS namespace and calls `ESP.restart()` so startup serial-guided calibration runs again on the next boot. |

### Game Modes

| File | Purpose |
|------|---------|
| `game_mode/board_adapter.h/.cpp` | Maps `LibreChess::Game`, `MoveList`, `MoveResult`, `CastlingInfo`, and core piece/color types into board-owned DTOs and the `BoardGameProvider` contract. This is the only GameMode-side mapping boundary for the board game program. |
| `game_mode/game_mode.h/.cpp` | Abstract base class for all game modes. Holds a `Game*` that orchestrates chess state, recording, and observer notification. Implements shared logic: `applyMove()` (the only `Game::makeMove()` call path for modes), coordinate-string parsing, `waitForBoardSetup()`, assistance service/cancel through board gameplay, `tryResumeGame()`, web resign relay, and mode-specific resign hooks. Physical interaction is delegated to `BoardGame`; chess mutations stay in `GameMode`/`Game`. |
| `game_mode/bot_mode.h/.cpp` | Concrete class for human-vs-engine play (composition pattern). Composes an opponent `EngineProvider*` via strategy injection. Non-blocking `update()` drives an async state machine (`BotState::PLAYER_TURN` / `ENGINE_THINKING`): player turn → service independent assistance → `tryPlayerMove()` → `applyMove()` → `provider_->onPlayerMoveApplied()`; engine turn → `provider_->requestMove()` (spawns FreeRTOS task) → polls `provider_->checkResult()` each tick. BotMode owns opponent engine lifecycle, thinking/waiting indicators, remote game-end handling, errors, and provider resign hooks; shared physical guidance lives in `BoardGame`. |
| `game_mode/player_mode.h/.cpp` | Human vs Human mode. Minimal subclass of `GameMode` — implements `begin()` (board setup, game recording) and `update()` (sensor polling, move processing). |

### Engine Providers

| File | Purpose |
|------|---------|
| `engines/async_provider.h` | `AsyncEngineProvider`: firmware-side FreeRTOS task helper for concrete engines. Owns `BaseTaskContext`, `spawnTask()`, `pollResult()`, `peekResult()`, `finishTask()`, and cancellation mechanics while deriving from the pure `EngineProvider` contract in `lib/game/src/provider.h`. |
| `engines/factory.h/.cpp` | Central construction/wiring helpers for opponent engines and board assistance providers. Keeps `main.cpp` from including concrete engine implementations directly. |
| `engines/types.h` | Firmware engine IDs stored in `GameMeta` for recording/resume decisions. |
| `engines/stockfish/engine.h/.cpp` | `StockfishEngine`: extends `AsyncEngineProvider`, spawns a one-shot FreeRTOS task per move that calls `StockfishAPI`. `TaskContext` extends `BaseTaskContext` with FEN, depth, and evaluation fields. Retry logic with cancel checking between attempts. |
| `engines/stockfish/api.h/.cpp` | Stockfish API client. Builds request URLs, parses JSON responses (evaluation, best move, continuation). Connects to `stockfish.online` over HTTPS. |
| `engines/stockfish/settings.h` | HTTP request configuration for the Stockfish API: `depth`, `timeoutMs`, `maxRetries`. Settings are computed internally by `StockfishEngine` from its `LEVELS[8]` table (depths 6–16). |
| `engines/lichess/engine.h/.cpp` | `LichessEngine`: extends `AsyncEngineProvider`, blocking `initialize()` discovers active games (token verification + event polling). `requestMove()` spawns a FreeRTOS task that opens a persistent NDJSON stream and reads events; reconnects with exponential backoff on connection loss. `onPlayerMoveApplied()` sends moves to Lichess with retries. `onResignConfirmed()` resigns on the server. |
| `engines/lichess/api.h/.cpp` | Lichess API client. Token management, game event polling, persistent game stream (`connectGameStream()` / `readStreamEvent()`), move submission, and resignation. Connects to `lichess.org` over HTTPS. |
| `engines/lichess/config.h` | `LichessConfig` struct: holds the Lichess API token. |
| `engines/librechess/engine.h/.cpp` | `LibreChessEngine`: extends `AsyncEngineProvider`, runs the on-board search engine in-process via `Game::calculateMove()`. Takes a `Game*` at construction. `initialize()` calls `game->initSearch()` with heap-sized TT + `game->setTimeFunc(millis)`, then logs Game search/hash diagnostics when heap pressure degrades resources. Each `requestMove()` allocates its task context with `new(std::nothrow)`, spawns a FreeRTOS task (64 KiB stack), wires cancellation, calls `game->calculateMove(limits)`, and converts the `SearchResult` to `EngineResult`. No network, no string serialization. |
| `engines/librechess/assistance.h/.cpp` | `LibreChessAssistanceProvider`: implements `BoardAssistanceProvider` for BEST_MOVE assistance. Runs one difficulty-scaled `Game::rankCandidateTargets()` search over the lifted piece's legal targets inside a transient 64 KiB `lcAssist` task, falls back to `Game::scoreCandidateMove()` if needed, and returns `BoardMoveTargetRanking` best/worst destination data. |

### Infrastructure

| File | Purpose |
|------|---------|
| `wifi_manager_esp32.h/.cpp` | WiFi connection management (state machine with AP/STA modes), async web server (ESPAsyncWebServer), all HTTP API endpoints, mDNS, known-networks registry (NVS), OTA password management, and board state relay to the web UI. |
| `storage/littrefs.h/.cpp` | Concrete `IGameStorage` backed by LittleFS. The dedicated `storage/` folder leaves room for additional persistence backends without crowding the firmware root. Manages `/games/` directory, binary game files (header + moves + FEN table), storage limits enforcement, and JSON game list API for the web UI. |
| `shared/serial_logger.h/.cpp` | Concrete `ILogger` using Arduino `Serial`. |
| `shared/utils.h/.cpp` | Arduino/ESP32 utility functions such as `SystemUtils::ensureNvsInitialized()` (Preferences guard). Not available in native tests. |

## Web Frontend (`src/web/`)

The ESP32 serves a web interface directly from flash storage. The frontend is built with vanilla HTML, CSS, and JavaScript — no build framework or SPA router. Each page is a self-contained HTML file that includes shared scripts.

### Pages

| File | Purpose |
|------|---------|
| `index.html` | Home and settings page. WiFi network management, Lichess token, LED brightness/dimming, board recalibration, OTA firmware upload, and OTA password security. |
| `board.html` | Board view and interaction page. Live board display with evaluation bar, move history with navigation, board editor (drag-and-drop, FEN, castling/en-passant controls), game history browser, game review mode, settings popup (themes, colors, sounds), focus mode, and resign button. |
| `game.html` | Game mode selection page. Four mode cards, independent assistance controls, and bot configuration panel (opponent engine, color, difficulty). Redirects to board page after selection. |

### Scripts (`src/web/scripts/`)

| File | Purpose |
|------|---------|
| `api.js` | Low-level HTTP utilities: `getApi()`, `postApi()`, `deleteApi()` fetch wrappers and `pollHealth()` for OTA reboot polling. |
| `provider.js` | Domain-specific API client. `Api` object with named methods for every backend endpoint (e.g., `Api.getNetworks()`, `Api.resign()`, `Api.selectGame()`). All pages use `Api.*` methods — no page contains raw fetch calls. |
| `Position-1.0.0.min.js` | Third-party Position rendering library. |
| `jquery-4.0.0.min.js` | jQuery (dependency of Position.js). |

### Styles (`src/web/css/`)

| File | Purpose |
|------|---------|
| `styles.css` | Application styles. Dark theme, responsive layout, game mode cards, board controls, evaluation bar, FEN editor, OTA dropzone, settings popup, game history cards, review panel. |
| `Position-1.0.0.min.css` | Third-party Position styles. |

### Assets

- `src/web/pieces/` — SVG chess piece images (12 files: `wK.svg`, `bQ.svg`, etc.)
- `src/web/sounds/` — Move sounds (`move.nogz.mp3`, `capture.nogz.mp3`). The `.nogz.` naming convention prevents gzip compression in the build pipeline — these are served as raw binary files.

## Libraries (`lib/`)

Two PlatformIO libraries keep reusable chess logic outside the firmware layer. `lib/core/` and `lib/game/` use the dependency boundary `core ← game`. Game never imports engine internals directly. All use `std::string` (not Arduino `String`) where strings are needed; firmware bridges with `.c_str()` / `std::string()`. PlatformIO's Library Dependency Finder auto-discovers these libraries for the ESP32 and native test environments.

### Foundation (`lib/core/`)

Board representation, rules, movegen, evaluation, search, UCI protocol, time management, notation, FEN, and utilities. Zero Arduino dependencies — natively compilable for host-based unit testing.

| File | Purpose |
|------|---------|
| `library.json` | PlatformIO library descriptor. |
| `src/piece.h` | `piece` namespace (header-only): type-safe piece representation (Piece/Color/PieceType enums, bit extraction, construction, predicates, color helpers, FEN char conversion via switches, unified `pieceIndex` overloads (`(Color, PieceType)`, `(Piece)`, `(char)`) returning 0–11 or `PIECE_IDX_NONE` with `isValidPieceIndex()` predicate, material values). |
| `src/bitboard.h` | `LibreChess` (bitboard) namespace (header-only): `Square` type and LERF coordinate conversion (`rankOf`, `fileOf`, `makeSquare`; anchor constants `SQ_A1`/`SQ_H1`/`SQ_A8`/`SQ_H8`/`SQ_NONE`, compass-rose directional shifts), bitboard types (`Bitboard = uint64_t`), bit manipulation (`popcount`, `lsb`, `popLsb`), file/rank masks, square-color masks (`DARK_SQUARES`, `LIGHT_SQUARES`), `BitboardSet` struct (12 piece + 2 color + occupancy bitboards with `setPiece`/`removePiece`/`movePiece`). |
| `src/attacks.h/.cpp` | `attacks` namespace: const leaper tables (`KNIGHT[64]`, `KING[64]`, `PAWN[2][64]`, ~2.5 KiB, computed at compile time via constexpr builders, placed in .rodata; wrapper structs `Table64`/`PawnTable` with `operator[]`), O(1) slider functions (`rook` via first-rank table + Hyperbola Quintessence, `bishop` via HQ on diagonal masks, `queen` = rook+bishop), x-ray functions (`xrayRook`, `xrayBishop`), ray geometry (`between`, `line`), `AttackInfo` struct + `computeAll(bb)` (per-piece-type/color attack maps for evaluation/search), `isSquareUnderAttack(bb, sq, color)` (check detection via attack table lookups), `see(bb, mailbox, move)` (Static Exchange Evaluation — swap algorithm with least-valuable-attacker iteration; delegates to `eval::materialValue()` for piece values with king sentinel of 20000; used by search for quiescence pruning and move ordering), `init()` inline no-op (retained for backward compatibility). |
| `src/movegen.h/.cpp` | `movegen` namespace: stateless chess logic. Move generation (fills `MoveList&`), legal move filtering via copy-make on `BitboardSet`. All functions take `const BitboardSet& bb` + `const Piece mailbox[]`; check detection delegates to `attacks::isSquareUnderAttack()`. Position-dependent state (castling rights, en passant target) is passed in via `const PositionState&`. Game-end detection (check, checkmate, stalemate, draws) was formerly in a separate `rules` namespace and is now implemented as `Position` static methods. |
| `src/position.h/.cpp` | `Position` class: board representation and position-level chess logic — a pure position container with no lifecycle state. Owns `BitboardSet bb_` (12 piece bitboards + 2 color + occupancy), `Piece mailbox_[64]` (flat array for O(1) piece identity), current turn, and all position state via `PositionState` (castling rights, en passant, halfmove/fullmove clocks; quiet moves saturate halfmove at 100). Incremental Zobrist hash (`hash_`). King cache (`Square kingSquare_[2]`). Incremental accumulators: `int16_t material_` (white-relative material balance), `int16_t mgPST_` / `int16_t egPST_` (material+PST scores), all updated incrementally on move/capture/promotion. Cached EP legality (`bool epIsLegal_` — avoids redundant movegen in make/Zobrist). Incremental game phase (`int8_t phase_` — N=1,B=1,R=2,Q=4; public `phase()` getter clamped to max 24). Game-end detection (check, checkmate, stalemate, insufficient material, threefold repetition, 50-move rule, draws) implemented as static methods — formerly in the `rules` namespace, now merged into `Position`. `recordPosition()` compacts `HashHistory` while reserving an append slot. Insufficient material detection (K vs K, K+B/N vs K, K+B vs K+B same-color). Public API: `newGame()`, `loadFEN()` → `bool` (validates FEN before applying; returns false on invalid input), `makeMove()` → `MoveResult`, `makeNullMove()` / `unmakeNullMove()` (null move support for NMP in search), `getFen()`, `getCastlingRights()`, `positionState()`, `bitboards()`, `mailbox()`. Convenience wrappers (implementations in `position.cpp`, header dependency-free): `getPossibleMoves()`, `inCheck()`, `isCheckmate()`, `isDraw()`, `isFiftyMoves()`. Member methods: `checkEnPassant()` → `EnPassantInfo`, `checkCastling()` → `CastlingInfo` (structs in `types.h`, delegate to `utils::` Square-based free functions), `boardToText()`. Note: lifecycle state (`gameOver_`, `gameResult_`, `winnerColor_`) and lifecycle methods (`endGame()`, `isGameOver()`) live in `Game`. Move history, observer notification, undo/redo, and batching also live in `Game`. |
| `src/utils.h` | `utils` namespace (header-only): stateless board-level helpers — LERF-native coordinate helpers (`squareName(Square)`, `fileChar`, `rankCharFromRank`, `fileIndex`, `rankIndexFromChar`), validation (`isValidPromotionChar`), castling rights (`hasCastlingRight()` via `BIT[2][2]` lookup indexed by `[raw(color)][kingSide]`, `castlingCharToBit()` switch-based, formatting/parsing), `updateCastlingRights()` (lookup-table approach: 64-entry `CASTLING_MASK[sq]` indexed by LERF square), `resolveKingSquare(bb, color, kingSq)` (inline king-square finder used by movegen + search), `roundDownPow2(n)` (shared power-of-two rounding for TT/hash table sizing). Display-coordinate helpers (`rankChar(row)`, `squareName(row,col)`) live in `game/types.h`. `gameResultName()` lives in `types.h` next to `GameResult`. Board iteration helpers: `forEachSquare(mailbox, fn)` loops 0–63, `forEachPiece(bb, mailbox, fn)` iterates occupied squares via `popLsb`. `checkEnPassant(mailbox, from, to)` and `checkCastling(mailbox, from, to)` are Square-based free functions shared by `Position` member methods and `movegen`. `boardToText` is a `Position` member method. `EnPassantInfo`/`CastlingInfo` structs live in `types.h`. |
| `src/hash_table.h` | `HashTableBase<Entry>` template (header-only): generic hash table base providing `resize(numEntries)`, `free()`, `clear()`, `isAllocated()`, and `allocationFailed()` — shared by `PawnHashTable`, `EvalHashTable` (core), and `TranspositionTable` (engine). Rounds size down to power-of-two via `utils::roundDownPow2`. |
| `src/evaluation.h/.cpp` | `eval` namespace: tapered position evaluation — two overloads: `evaluatePosition(bb)` (full), `evaluatePosition(bb, mg, eg, phase, pawnHash)` (pre-computed material+PST+phase — used by search). `computeGamePhase(bb)` is public (used by `Position` for incremental tracking). `PHASE_WEIGHT[]` lookup table maps PieceType → phase contribution (N=1, B=1, R=2, Q=4; max 24). Endgame PSTs for king (centralization) and pawn (advancement) differ from midgame; other pieces share PSTs. Returns score in centipawns. Production: flat `PSQT_MG[12][64]` / `PSQT_EG[12][64]` `static constexpr` tables (rodata, macro-initialized) pre-combine material + PST + color sign per piece-square for O(1) lookup. TUNING: `pieceSquareMGEG()` computes directly from mutable params (no cached tables, no invalidation). `pieceSquareMGEG()` returns `PSQTPair{mg, eg}` for combined access; `computeMaterialPST()` delegates to `pieceSquareMGEG` in a loop. `chebyshevDist(a, b)` — Chebyshev distance (always non-static). evaluation.h has zero `#ifdef TUNING`. Pawn-structure analysis functions (`isPassed`, `isIsolated`, `isDoubled`, `isBackward`); pawn masks are `static constexpr` (`PawnMasks` struct in anonymous namespace, placed in .rodata),  `PawnHashTable` caches pawn structure MG/EG scores + passed pawn bitboards (1024 entries × 24B = 24 KiB; `PawnEntry` includes `Bitboard passedPawns[2]` to avoid re-scanning on cache hit). `EvalHashTable` caches full evaluation results (1024 entries × 8B = 8 KiB). Both inherit `HashTableBase<Entry>` from `hash_table.h` and are optional (nullptr to skip). |

| `src/fen.h/.cpp` | `fen` namespace: FEN string handling — `boardToFEN()` (mailbox → FEN string), `fenToBoard()` (FEN string → `BitboardSet` + `mailbox` + state), `validateFEN()` (format validation: rank structure, piece chars, turn, castling, en passant, clocks, storage-compatible clock ranges). |
| `src/notation.h/.cpp` | `notation` namespace: Square-native move notation conversion — coordinate (`"e2e4"`), SAN (`"Nf3"`), LAN (`"Ng1-f3"`) output and parsing. Parse functions output `Square` (LERF); format functions take `Square`. All functions are pure (`const BitboardSet&` + `const Piece mailbox[]` passed in). `Game` provides row/col wrappers for firmware. |
| `src/types.h` | Core chess types: `Piece`/`Color`/`PieceType` enums, `Square` (`uint8_t`, `SQ_NONE = 255`), `PositionState` struct (packed: `uint8_t castlingRights`, `Square epSquare`, `uint8_t halfmoveClock` saturated at 100, `uint16_t fullmoveClock`) with `initial()` static factory, `GameResult` enum class, `gameResultName()`, `HashHistory` struct (`keys[256]` + count, used by `Position`), `MoveFormat` enum class (`COORDINATE`, `SAN`, `LAN`). Game-management types (`GameHeader`, recording constants) live in `lib/game/src/types.h`. |
| `src/move.h` | Move representation: `Move` struct (3 bytes: from/to/flags with capture, EP, castling, promotion bits), `ScoredMove` struct (Move + int16_t score), `MoveList` struct (fixed-size `Move[218]` array + count, used by both per-piece and bulk move generation, with `targetRow`/`targetCol` adapter accessors for UI), `MoveResult` struct (packed `uint8_t flags` with MR_VALID/MR_CAPTURE/MR_EP/MR_CASTLING/MR_PROMOTION/MR_CHECK and constexpr accessor methods, returned by `Position::makeMove()`), `MoveEntry` struct (uses MR_* flag constants directly — MR_CAPTURE/MR_EP/MR_CASTLING/MR_PROMOTION/MR_CHECK with constexpr accessors, move log record with `build()` factory), `invalidMoveResult()` factory. |
| `src/zobrist.h` | `zobrist` namespace (header-only constants + `zobrist.cpp` for `computeHash`): constexpr-generated Zobrist keys, piece-index mapping, full-board hash computation (`computeHash(bb, mailbox, turn, state, epLegal)` — EP legality pre-computed by caller), pawn-only hash (`computePawnHash(bb)`) for pawn hash table. `Position` uses incremental hashing via XOR deltas in `make()`; `computeHash()` is retained for debug verification. |
| `src/epd.h/.cpp` | `epd` namespace (wrapped in `namespace LibreChess`): generic EPD parser. Structs: `EPDOperation` (opcode + fixed operand array), `EPDRecord` (4-field FEN + fixed operation list + `findOperation()` / `id()` accessors). Functions: `parseEPDLine()`, `validateEPDLine()`. Validates the first four FEN fields through `fen::validateFEN()` and rejects over-cap operation/operand records rather than truncating. Supports standard opcodes (`bm`, `am`, `id`, `c0`, `c9`). Used by tactical test suites and the offline tuner. |
| `src/eval/params.h` | `eval` namespace: extracted evaluation constants — `EVAL_CONST`/`EVAL_FIXED`/`PST_ELEM`/`MAT_ELEM` macros, material values, PST tables (12 arrays × 64), pawn structure bonuses, bishop pair/bad bishop, rook bonuses, mobility weights, king safety/danger tables, outpost/space/trapped/threat parameters. Separated from `evaluation.cpp` for clarity and tuning workflow. |
| `src/trace.h/.cpp` | `eval` namespace (`#ifdef TUNING` only): all tuning infrastructure consolidated into two files. **trace.h**: `TraceEntry`/`Trace`/`TrainingPosition` types, `extractTrace`/`buildParamMap`/`findParam` decls, descriptor structs (`ScalarParam`, `PstDef`), `tuning::` accessor API decls, and all eval param `extern` declarations. **trace.cpp**: `ptrMap`/`nameMap`/`pIdx()` index maps, `extractTrace()` (mirrors `evaluatePosition()`), descriptor getter implementations (`scalarParams`, `pstDefs`), `buildRegistry()` (loop-generates mobility + PST entries from descriptors), and `tuning::` accessor wrappers. Compiles to nothing in production builds. |
| `src/search.h/.cpp` | `search` namespace: on-board chess engine — negamax with alpha-beta pruning + quiescence search (MVV-LVA ordered) + iterative deepening + check extensions + recapture extensions (cached SEE) + PVS + null move pruning (NMP, adaptive R) + late move reductions (logarithmic table + history + improving) + late move pruning (improving-aware) + reverse futility pruning (margin/depth, improving-aware) + aspiration windows (gradual doubling) + root move reordering + delta pruning (quiescence) + futility pruning (shallow negamax) + SEE-based capture ordering (losing captures demoted, SEE computed lazily when yielded) + improving flag (ply-2/ply-4 eval tracking). `findBestMove(pos, limits, state, info)` is the single public entry point and clamps external depth to `[1, MAX_PLY]`. `SearchLimits` (depth/time/stop), `SearchResult` (bestMove/score/depth/nodes), `SearchState` (per-search heuristics + staticEvals). Constants: `MATE_SCORE`, `MAX_PLY`, `DEFAULT_TT_SIZE`. Also contains `TranspositionTable` (inherits `HashTableBase<TTEntry>`), `TTFlag` enum, `PackedMove` typedef + pack/unpack, `TTEntry` struct (12 bytes), generation tracking, inline `probe`/`store`. |
| `src/search_params.h` | `search` namespace: extracted search constants — pruning margins (futility, razor, RFP, LMP, history), reduction thresholds (NMP, LMR depth/moves), LMR reduction table (`LMR_TABLE` + `initLMR()`), aspiration window delta, singular extension parameters, lazy eval margin, tempo bonus, and quiescence depth limit. Separated from `search.cpp` for clarity. |
| `src/engine.h/.cpp` | `Engine` class: search resource ownership facade. Owns `TranspositionTable`, `PawnHashTable`, `EvalHashTable`, and `SearchState`. Provides `calculateMove(pos, limits, info)`, `clearState()`, `resizeTT()`, `setTimeFunc()`, `setExternalStop()`, and hash allocation diagnostics (`hashTablesReady()`, `hashTableAllocationFailed()`). Composed by both `UCIState` (value member) and `Game` (optional heap pointer). Does not own `Position`. |
| `src/uci.h/.cpp` | `uci` namespace: UCI protocol handler. `UCIState` resource bundle (owns Position and `Engine` which manages TT, hash tables, SearchState; plus external stop flag). `loop(state, in, out)` — blocking stdin/stdout loop for the native CLI. `processLine(state, line, output)` — pure string-in/string-out for unit tests. Supports `uci`, `isready`, `setoption`, `ucinewgame`, `position`, `go`, `quit`; `go` clamps depth and parses clocks as bounded non-negative milliseconds. Reference: CPW UCI. |
| `src/time_management.h` | `time_management` namespace (header-only): `computeTimeLimits(wtime, btime, winc, binc, movestogo, sideToMove) → SearchLimits`. Inputs are `uint32_t` milliseconds; internal math is bounded and returns at least 1ms soft/hard time even for zero clocks. Formula: `softTime = safeRemaining/30 + increment/2`, `hardTime = min(max(1, safeRemaining/4), softTime*4)`. Safety margin: `safeRemaining = max(1, remaining - 50ms)`. With movestogo: `softTime = safeRemaining/movestogo + increment`. Reference: CPW Time Management. |
| `src/move_picker.h` | `MovePicker` (header-only): staged move generation for search — 9 stages (TT → captures → killers → countermove → quiets → bad captures → done). MVV-LVA scoring (`scoreMVVLVA`), lazy SEE evaluation, `isMoveValid` (flag reconstruction), `pickBest` template, heuristic update helpers (`updateKillers`, `updateHistory` with gravity formula, `updateCaptureCutoffHistory`, `updateQuietCutoffHeuristics`), safe accessor helpers (`safeCaptureHistScore`, `safeCountermove`). |
| `src/stats.h` | Search statistics (`#ifdef STATS` only): `SearchStats` counters for node types, pruning, extensions, reductions. Compiled only in `native_stats` env. |
| `src/book.h/.cpp` | `book` namespace: internal opening book. ~43 curated opening lines replayed at static initialization via `ReplayBoard` (64-byte mailbox); line parsing validates each 4-character coordinate token before replay. Compiled book storage uses parallel hash/from/to arrays to avoid per-entry struct padding. `probe(hash, from, to, rng) → bool` — linear scan with dedup, random selection via xorshift64. `entryCount()` for diagnostics. Zero heap usage. Probed from `findBestMove()` when `SearchState::useBook` is true. |

Game lifecycle, history, recording, DI interfaces, and the pure engine provider
contract. Depends on `lib/core/`.

| File | Purpose |
|------|---------|
| `src/game.h/.cpp` | `Game` class: central game orchestrator. Composes `Position`, `History`, and optionally `IGameObserver`. Constructor: `(IGameStorage*, IGameObserver*, ILogger*)`. All chess-state mutations flow through this class. Handles threefold repetition detection (via Zobrist hashing), move history recording, persistent game recording (delegated to `History`), observer notification, and batching. Optionally composes an `Engine*` (heap-allocated via `initSearch()` with `new(std::nothrow)`) for bot mode search. Provides `calculateMove(limits) → SearchResult` that delegates to the engine, plus search/hash allocation diagnostics for firmware heap-pressure handling. Provides dual overloads (Square-native and row/col) for `makeMove`, `getSquare`, `getPossibleMoves`, `checkEnPassant`, `checkCastling`; Square-native is the primary implementation, row/col thin-wraps it for firmware convenience. Exposes `bitboards()` and `mailbox()` accessors, board iteration helpers (`forEachSquare` via `utils::`), and notation convenience methods (`makeMove(string)`, `toCoordinate()`, `parseCoordinate()`, `getHistory(format)`) so firmware never needs to include core headers directly. |
| `src/types.h` | Game-management types: `GameHeader` packed struct (16 bytes, `#pragma pack(push, 1)`, on-disk recording format with opaque `meta[GAME_META_SIZE]` byte array for firmware-specific data), recording constants (`FEN_MARKER`, `MAX_GAMES`, `MAX_USAGE_PERCENT`, `GAME_META_SIZE`). Display-coordinate bridge: `rowColToSquare`, `squareToRow`, `squareToCol`, `rankChar(row)`, `squareName(row,col)`. Includes `piece.h` to re-export core types — shared name with `lib/core/src/types.h` (see note in file header). |
| `src/provider.h` | Pure `EngineProvider` contract and data (`DifficultyLevel`, `EngineInitResult`, `EngineResult`). Shared by firmware game modes and concrete engines without pulling Arduino, FreeRTOS, board programs, or firmware metadata into `lib/game`. `EngineInitResult::mode` is an opaque byte that firmware maps to `GameModeId`/`GameMeta`. |
| `src/history.h/.cpp` | `History` class: in-memory game history and persistent game recording. Ordered move log (`MoveEntry` structs with full move metadata including previous position state), Zobrist hash tracking for threefold repetition detection, and game recording lifecycle (compact 2-byte move encoding via static `encodeMove()`/`decodeMove()`, manages `GameHeader`, delegates persistence to `IGameStorage`, flushes the live header after every persisted half-move, decodes replay bytes without assuming alignment, validates moves during replay, replays games directly into a `Position`). Fixed-size arrays (ESP32-friendly). Composed by `Game`. |
| `src/observer.h` | `IGameObserver` abstract interface: `onBoardStateChanged(fen, evaluation)`. |
| `src/storage.h` | `IGameStorage` abstract interface: game file lifecycle (begin, append, finalize, discard), read-back for replay, and storage management. |

## Unit Tests (`test/`)

Native unit tests using the PlatformIO Unity framework. Library suites mirror `lib/core/` and `lib/game/`, plus perft, position, benchmark, and statistics suites.

```
test/
├── test_helpers.h                       Shared utilities (setupInitialBoard, clearBoard, placePiece, etc.)
├── test_shared.cpp                      Shared globals (bb, mailbox, needsDefaultKings)
├── test_core/
│   ├── test_all.cpp                    Main entry: setUp/tearDown, register calls for all core tests
│   ├── test_attacks.cpp                 attacks: leaper tables, slider attacks (+ bulk reference cross-check), x-ray attacks, geometry rays, AttackInfo, SEE
│   ├── test_bitboard.cpp               LibreChess: square mapping roundtrip, bit ops, square-color masks, BitboardSet mutations
│   ├── test_epd.cpp                    EPD parser: parseEPDLine (bm/am/id/c0/c9, quoted/comma-separated), validateEPDLine, strict FEN fields, cap rejection, accessors
│   ├── test_evaluation.cpp             eval: material scoring, pawn structure, tapered evaluation, pawn analysis functions, positional terms, pawn/eval hash tables, allocation status
│   ├── test_eval_regression.cpp        eval regression: 12 fixed-position score assertions (symmetry, material, pawn structure, threats, phase tapering)
│   ├── test_fen.cpp                    FEN round-trip, boardToFEN/fenToBoard, validateFEN (valid/invalid positions, fields, clock bounds)
│   ├── test_movegen.cpp                Move generation per piece type, captures, bulk generation, legal move queries
│   ├── test_notation.cpp               Coordinate/SAN/LAN output and parsing, auto-format detection, roundtrip
│   ├── test_piece.cpp                  piece: type extraction, predicates, FEN chars, material values, Zobrist index, color helpers
│   ├── test_position.cpp               Position: moves, special moves, draws, FEN, API queries, check/checkmate/stalemate, pin-aware generation, castling, en passant, promotion
│   ├── test_utils.cpp                  utils: 50-move rule, castling rights, coordinate helpers, board transforms, resolveKingSquare, forEachSquare/forEachPiece
│   ├── test_zobrist.cpp                Zobrist hashing: key determinism, computeHash, computePawnHash, position sensitivity
│   ├── test_search.cpp                 search: mate-in-1, captures, quiescence, stalemate avoidance, iterative deepening, depth clamp, time/stop control, TT store/probe/clear/pack/mate-score, move ordering, delta pruning, futility pruning, SEE ordering
│   └── test_uci.cpp                     UCI protocol: uci command, isready, go depth, bounded clocks, position/fen, newgame, info output, quit, mate score, hash diagnostics, setoption Hash, go movetime
├── test_game/
│   ├── test_all.cpp                    Main entry: setUp/tearDown, register calls for game tests
│   ├── test_game.cpp                   Game: threefold repetition, draw detection, observer notification/batching, history
│   ├── test_history.cpp                History: move log with undo/redo, branch-on-undo, compact encode/decode
│   ├── test_history_persistence.cpp    History recording: persistence lifecycle, header flush, replay, interrupted trailing bytes, branch-truncation, compact encode/decode
│   └── test_provider.cpp               Provider contract: result defaults, no-op hooks, virtual dispatch
├── suites/                              Shared EPD test files (no .cpp — not compiled)
│   ├── wac.epd                          Win At Chess — 300 positions (Reinfeld/Wilson, CPW verbatim)
│   ├── bk.epd                           Bratko-Kopec — 24 positions (Bratko/Kopec, CPW verbatim)
│   └── eret.epd                         Eigenmann Rapid Engine Test — 111 positions (Eigenmann, CPW verbatim)
├── test_positions_time/                 Time-based position test suites (standalone, heavyweight)
│   └── test_positions_time.cpp          Suite runner: loads .epd files from ../suites/, 500ms/position, informational pass rates
├── test_positions_depth/                Depth-based position test (standalone, deterministic)
│   └── test_positions_depth.cpp         WAC 300 at fixed depth 10, hard assert on solve count vs calibrated baseline
├── test_benchmarks/                     Performance benchmarks + regression tests
│   ├── test_all.cpp                    Main entry: register calls for benchmark + regression tests
│   ├── test_timing.cpp                  Micro-benchmarks: make/unmake, evaluate, bishop attacks, perft(5), search depth 8
│   └── test_regression.cpp              Node count regression (10 pos × depth 10, 15% threshold) + eval regression (15 pos, exact match)
├── test_statistics/                     Search statistics diagnostic (standalone)
│   └── test_statistics.cpp              Runs 5 positions at depth 10, prints TT/cutoff/pruning/extension stats. Requires -DSTATS.
└── test_perft/
    └── test_perft.cpp                  Perft validation: initial position, kiwipete, and standard positions 3–6
```

Run all: `pio test -e native`. Run one suite: `pio test -e native -f test_core`. See [PlatformIO Unit Testing docs](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html).

## Engine CLI (`tools/engine/`)

Native UCI engine executable for SPRT testing with fastchess. Compiles the core library into a standalone command-line binary.

| File | Purpose |
|------|---------|  
| `main.cpp` | Entry point: sets up `nativeMillis()` via `std::chrono`, creates `UCIState`, calls `uci::loop(state, stdin, stdout)`. ~30 lines. |
| `Makefile` | Compiles `main.cpp` + all `lib/core/src/*.cpp`. Output: `librechess.exe` (Windows) / `librechess` (Linux). Flags: `-std=gnu++17 -O2 -DNDEBUG`. |

Build: `cd tools/engine && mingw32-make` (Windows) or `make` (Linux).

## SPRT Testing (`tools/sprt/`)

Sequential Probability Ratio Test infrastructure for validating engine changes. Uses [fastchess](https://github.com/Disservin/fastchess) to run many games between a baseline engine and a patched engine, accepting or rejecting the change with statistical confidence.

| File | Purpose |
|------|---------|  
| `Makefile` | SPRT runner: builds baseline (from git ref via worktree) and dev (current tree) engines, invokes fastchess with SPRT bounds and adjudication rules. |
| `8moves_v3.pgn` | Opening book (~34,700 openings, 8 moves deep, Stockfish self-play). Sourced from [CPW](https://www.chessprogramming.org/). Suited for weaker engines. |

Usage: `cd tools/sprt && make` (defaults: BASELINE=HEAD, TC=8+0.08, ELO0=0, ELO1=10). See `tools/sprt/README.md` for full parameter reference.

## Tuning Tools (`tools/tune/`)

Offline Texel tuning infrastructure. Compiles the core library with `-DTUNING` (which makes `EVAL_CONST` parameters mutable `int` instead of `constexpr`) and runs gradient descent on a labeled EPD corpus.

| File | Purpose |
|------|---------|
| `tune.cpp` | Adam gradient-descent optimizer with float accumulators. Loads corpus via `epd::parseEPDLine()` with `c9` result annotations, uses `eval::extractTrace()` to compute per-parameter analytical gradients in double precision, and rounds to integer only at the end. Outputs C++ code for copy-paste into `evaluation.cpp`. |
| `Makefile` | Builds the tuner using g++ with `-DTUNING -std=gnu++17`. Compiles all core sources (including `trace.cpp`) + local `tune.cpp` into object files, links with `-pthread`. Targets: `tune` (build), `pipeline` (build + run), `clean`. |

## Build Scripts (`src/web/build/`)

| File | Purpose |
|------|---------|
| `minify.py` | Pre-build: minifies HTML/CSS/JS from `src/web/` → `src/web/build/`. Skips gracefully if npm tools aren't installed. |
| `prepare_littlefs.py` | Pre-build: gzip-compresses web assets into `data/` for LittleFS. Respects the `.nogz.` convention. Cleans intermediate files after. |
| `upload_fs.py` | Build hook: hashes `data/` contents and only uploads the LittleFS image when assets change. |

## Filesystem (`data/`)

The `data/` directory contains pre-built, gzip-compressed web assets ready for LittleFS upload. This directory is committed to the repository so the project can be built and flashed without npm minification tools.

```
data/
├── index.html.gz
├── board.html.gz
├── game.html.gz
├── favicon.svg.gz
├── css/          *.css.gz
├── scripts/      *.js.gz
├── pieces/       *.svg.gz
└── sounds/       *.mp3 (raw, not gzipped)
```

`ESPAsyncWebServer` detects `.gz` files and serves them with `Content-Encoding: gzip` automatically. Sound files are served raw with `setTryGzipFirst(false)`.

## LittleFS Runtime Layout

At runtime, the firmware creates additional files on the LittleFS partition:

```
/games/
├── live.bin        Active game data (header + moves) — crash recovery
├── live_fen.bin    Active game FEN snapshots — crash recovery
├── 0001.bin        Completed game #1
├── 0001_fen.bin    FEN table for game #1
├── ...
```

Storage limits: maximum 50 saved games, capped at 80% of LittleFS capacity.

## Configuration

LibreChess has no editable configuration file. All settings are persisted in ESP32 NVS (non-volatile storage) and managed through the web UI or code constants:

| Setting | Storage | How to Change |
|---------|---------|---------------|
| WiFi networks (up to 3) | NVS `"wifiNets"` | Web UI WiFi Settings |
| Lichess API token | NVS `"wifiCreds"` | Web UI Lichess Settings |
| OTA password | NVS `"ota"` (salted SHA-256) | Web UI Security Settings |
| LED brightness | NVS `"ledSettings"` | Web UI Board Settings |
| Dark square dimming | NVS `"ledSettings"` | Web UI Board Settings |
| Calibration data | NVS `"boardCal"` | Auto (first boot) or Web UI recalibrate button |
| GPIO pin assignments | `src/board/runtime/driver.h` `#define`s | Edit source code |
| Board, framework, libraries | `platformio.ini` | Edit file |
| Factory reset | `platformio.ini` build flag | Add `-DFACTORY_RESET` to `build_flags` |
