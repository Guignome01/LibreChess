# Architecture

Deep technical documentation of LibreChess internals. This document covers how the system works under the hood — component design, data flows, concurrency, storage, networking, and security. It is the authoritative reference for anyone modifying or extending the firmware.

## Class Hierarchy

```
Core (lib/core/):
  movegen (stateless move generation)
  Position (board representation + position logic + game-end detection via static methods)
  attacks (precomputed attack tables + slider functions)
  eval (tapered evaluation)
  notation/fen/utils/zobrist/piece (support namespaces)
  Engine (direct-call facade)
   ├─ owns Position (from core)
   └─ owns TranspositionTable + stop control
  search (fail-soft alpha-beta + quiescence + iterative deepening + check ext + PVS + NMP + LMR + LMP + history pruning + razoring + lazy eval + aspiration windows + IIR + delta pruning + futility pruning + pawn-defended-pawn QS pruning + SEE-based ordering + countermove heuristic + mate distance pruning + triangular PV table)
  TranspositionTable (search.h — inherits HashTableBase<TTEntry>)
  MovePicker (move_picker.h — staged move generation + heuristic updates)
  SearchState (~10 KiB, direct member of Engine)

Game (lib/game/):
  Game (central game orchestrator)
   ├─ composes Position (from core)
   ├─ composes History (move log + persistent game recording)
   └─ uses IGameObserver (notification)

Firmware (src/):
  GameMode (abstract base, src/game_mode/)
   ├─ PlayerMode (human vs human)
   └─ BotMode (concrete, composes EngineProvider*)

  EngineProvider (base class, src/engine/)
   ├─ StockfishProvider (src/engine/stockfish/)
   ├─ LichessProvider (src/engine/lichess/)
   └─ LibreChessProvider (src/engine/librechess/)

  SensorTest (standalone, does not inherit GameMode)
```

`GameMode` defines the shared game infrastructure and common logic: `tryPlayerMove()`, `applyMove()` (delegates to `Game::makeMove()`; the string overload parses coordinate notation via `Game::parseCoordinate()`), `waitForBoardSetup()`, `tryResumeGame()`, resign gesture handling, and LED feedback helpers. Each `GameMode` instance holds a `Game*` (`chess_`) which orchestrates board state, recording, and observer notification. All chess mutations flow through `Game`; the firmware never modifies the board or turn directly. Each subclass overrides `begin()` and `update()` to implement mode-specific behavior.

`BotMode` is a concrete class that composes an `EngineProvider*` (strategy pattern). Its `update()` implements a non-blocking state machine (`BotState::PLAYER_TURN` / `BotState::ENGINE_THINKING`): on the player's turn it calls `tryPlayerMove()` → `applyMove()` → `provider_->onPlayerMoveApplied()`; when the turn flips to the engine it calls `provider_->requestMove()` (spawns a FreeRTOS task) and transitions to `ENGINE_THINKING`. In the thinking state, it polls `provider_->checkResult()` each tick — sensors, resign gestures, and web UI remain responsive while the engine computes. `BotMode` also provides shared infrastructure: thinking animation management (`startThinking()`/`stopThinking()`), remote move guidance (`waitForRemoteMoveCompletion()` — LED cues + sensor blocking), engine move application (`applyEngineMove()`), remote game-end handling (`handleRemoteGameEnd()`), error abort (`abortWithError()`), and resign hooks (`onBeforeResignConfirm()` cancels the engine request, `onResignCancelled()` re-requests, `onResignConfirmed()` delegates to the provider).

`EngineProvider` is the base class in `src/engine/engine_provider.h`. It defines the contract for all chess engines: `initialize()` (blocking setup, returns `EngineInitResult` with player color, FEN, mode ID), `requestMove()` / `checkResult()` (async move computation via FreeRTOS tasks), plus optional hooks `onPlayerMoveApplied()` (Lichess sends the move to the server), `onResignConfirmed()` (Lichess resigns on the server), and `getEvaluation()` (Stockfish returns engine eval). It also owns the shared FreeRTOS task lifecycle: `activeTask_` pointer (a `BaseTaskContext*` with `std::atomic<bool>` cancel/ready flags and an `EngineResult`), `spawnTask()` (creates + launches a FreeRTOS task), `pollResult()` / `peekResult()` (non-destructive ready check), `finishTask()` (deletes the context), and `cancelRequest()`. Providers never touch `Game`, `BoardDriver`, or any hardware — they only do HTTP and return data.

`StockfishProvider` extends `EngineProvider` by spawning a one-shot FreeRTOS task per move that calls the Stockfish API. The task performs the HTTP GET with retry logic, parses the JSON response, and stores the result in its `TaskContext` (extends `BaseTaskContext` with FEN, depth, and evaluation fields).

`LichessProvider` extends `EngineProvider`. Its `initialize()` blocks during game discovery (token verification + polling for active games). Its `requestMove()` spawns a FreeRTOS task that opens a persistent NDJSON stream via `LichessAPI::connectGameStream()` and reads events via `readStreamEvent()`. On connection loss, it reconnects with exponential backoff (1s→2s→4s→8s, up to 5 attempts) — the game stays paused during reconnection; if all attempts are exhausted the game is aborted.

`LibreChessProvider` extends `EngineProvider`. It runs the on-board chess engine entirely in-process via the `Engine` facade — no network, no string serialization required. `initialize()` creates a persistent `Engine` with a heap-sized TT (capped at 128 KiB), which persists across moves along with hash tables and SearchState — no per-move heap fragmentation. Each `requestMove()` spawns a FreeRTOS task (64 KiB stack) that wires `ctx->cancel` to `engine.setExternalStop()` for cooperative cancellation, builds `SearchLimits` with depth/moveTime, and calls `engine.calculateMove(fen, limits)` on the persistent Engine. The `SearchResult` (bestMove, score, depth, nodes) is converted to `EngineResult` using `notation::toCoordinate()`. `initialize()` always succeeds, reports `mode = GameModeId::BOT`, `canResume = true`.

`SensorTest` follows the same `begin()`/`update()`/`isComplete()` lifecycle but is not a `GameMode` subclass — it doesn't need chess logic, FEN state, or move history.

### Dependency Injection

Components are wired through pointer injection at construction time. No global state or singletons:

```cpp
BoardDriver boardDriver;
SerialLogger logger;
LittleFSStorage storage(&logger);
WiFiManagerESP32 wifiManager(&boardDriver, &storage);
Game chess(&storage, &wifiManager, &logger);
auto* provider = new StockfishProvider(stockfishSettings, playerColor);
BotMode botGame(&boardDriver, &wifiManager, &chess, provider);
```

`Game` owns the `Position` internally — there is no shared chess state. All game mode classes interact with chess state through the `Game` orchestrator. `BotMode` takes ownership of the `EngineProvider*` (deletes it in its destructor). `History` handles both in-memory tracking and persistent recording — when constructed without an `IGameStorage*`, recording is silently skipped (used by Lichess games since they are recorded on the server).

## Coordinate System

Core internals use **LERF (Little-Endian Rank-File)** square indexing natively: a1=0, b1=1, ..., h8=63. Primary conversions: `rankOf(sq) = sq / 8`, `fileOf(sq) = sq % 8`, `makeSquare(rank, file) = rank * 8 + file`. The game layer and firmware use **row/col** display coordinates (row 0 = rank 8, col 0 = file a). Bridge functions (`rowColToSquare`, `squareToRow`, `squareToCol`, `rankChar`, `squareName`) live in `game/types.h`. Board mailbox is a flat `[64]` LERF-indexed array; `INITIAL_BOARD[64]` holds the starting position.

The LED strip is wired in a serpentine (zigzag) pattern across the physical board, but the `BoardDriver` calibration system maps physical LED indices to logical `[row][col]` coordinates. The calibration mapping is stored in NVS and applied transparently — all code above `BoardDriver` works exclusively in logical coordinates.

## Component Details

### BoardDriver

Hardware abstraction layer. Owns three subsystems:

**LED strip** — a 64-LED WS2812B strip driven by `NeoPixelBrightnessBus<NeoGrbFeature, NeoEsp32I2s0800KbpsMethod>`. The I2S peripheral with DMA offloads timing-critical signal generation to hardware, avoiding conflicts with WiFi interrupts and keeping the main loop responsive. The strip is connected to GPIO 32 (`LED_PIN`). Global brightness is adjustable (0–255, default 255), and dark squares are automatically dimmed by a configurable multiplier (default 70%, stored in NVS as `dimMultiplier`). The `currentColors[8][8]` array tracks the current color of every square so dim multiplier changes can be applied retroactively.

**Sensor grid** — 64 A3144 hall-effect sensors arranged in an 8×8 matrix, read through column-scanning multiplexing. A 74HC595 shift register activates one column at a time (via transistor switches), and 8 row GPIOs are read simultaneously. This uses only 11 GPIO pins (3 shift register control + 8 row inputs) to scan all 64 sensors. Sensor state is triple-buffered: `sensorRaw[8][8]` (latest physical read), `sensorState[8][8]` (debounced current state), and `sensorPrev[8][8]` (snapshot for change detection). The `lastEnabledCol` field enables efficient sequential column shifting — instead of clocking through all 8 bits each time, the driver detects sequential column advances and shifts by one bit.

GPIO pin definitions are `#define`d at the top of `board_driver.h`:
- Shift register: `SR_CLK_PIN` (14), `SR_LATCH_PIN` (26), `SR_SER_DATA_PIN` (33)
- Row inputs: `ROW_PIN_0` through `ROW_PIN_7` (GPIOs 4, 16, 17, 18, 19, 21, 22, 23)
- LED data: `LED_PIN` (32)
- `SR_INVERT_OUTPUTS`: set to 1 if using PNP transistors instead of NPN

The physical order of pin connections **does not matter** — the calibration process maps physical pins to logical board coordinates.

**Calibration** — an interactive serial-guided process that runs on first boot (or when triggered via the web UI). It maps physical sensor/LED positions to logical `[row][col]` coordinates by asking the user to place pieces in specific patterns. The resulting mapping tables (`toLogicalRow[]`, `toLogicalCol[]`, `ledIndexMap[8][8]`, `swapAxes`) are persisted in NVS namespace `"calibration"`. The `swapAxes` flag handles boards where the shift register and row pins are wired to the opposite physical axis. Until calibration completes, the board repeats the calibration prompt on every boot (with a `skip` option that defers but doesn't persist).

**Sensor polling parameters**: `SENSOR_READ_DELAY_MS` = 40ms (polling interval), `DEBOUNCE_MS` = 125ms (state change debounce window). A piece must be present (or absent) for the full debounce duration before the change is registered, preventing false triggers from sliding pieces or magnetic interference. Always call `boardDriver.readSensors()` before reading state — the state arrays are only updated on explicit read calls.

### movegen

Lives in `lib/core/` — a standalone PlatformIO library with zero Arduino dependencies. This makes it natively compilable for host-based unit testing while still being auto-discovered by the ESP32 build via PlatformIO's Library Dependency Finder.

Pure, **stateless** chess logic. All methods are `static` — `movegen` has no member variables, no constructor, and no instance state. Position-dependent context (castling rights, en passant target) is passed in via `const PositionState&` (defined in `types.h`). Board state is passed as `const BitboardSet& bb` + `const Piece mailbox[]` (or `bb` alone for pure bitboard operations). Game-end detection (check, checkmate, stalemate, draws) was formerly in a separate `rules` namespace and is now implemented as `Position` static methods with the same stateless signature pattern. Implements:

- **Per-piece move generation** — `getPossibleMoves(bb, mailbox, row, col, state, moves)` fills a `MoveList&` with all legal target squares for a single piece. Used by the board UI. Delegates to the file-local `filterPieceMoves()` template which handles king-move validation (full `leavesInCheck`), EP special casing, and pin/check mask filtering for non-king pieces. `FilterMode` enum (`ALL`, `CAPTURES_PROMOS`, `QUIETS`) controls which move types pass through — `getPossibleMoves` uses `FilterMode::ALL`.
- **Bulk move generation** — `generateAllMoves(bb, mailbox, color, state, moves)` fills a `MoveList&` with all legal moves for the entire position as `Move` structs (from/to/flags). `generateCaptures(...)` is the capture-only variant for quiescence search. Both build their own `LegalityContext` internally and use the same pin+check mask infrastructure, amortizing the cost across all friendly pieces via bitboard serialization. File-local anonymous-namespace helpers in `movegen.cpp`: `pinRayFor`, `computePinData`.
- **Staged move generation** — `buildLegalityContext(bb, color, kingSq)` builds pin and check data once, then `generateCaptures(bb, mailbox, color, state, ctx, moves)` and `generateQuiets(bb, mailbox, color, state, ctx, moves)` reuse the same context. Avoids computing pins and check masks twice when the search uses separate capture and quiet stages. `PinData` and `LegalityContext` are public structs in `movegen.h`.
- **Castling** — the `state.castlingRights` bitmask (bit 0 = White kingside, bit 1 = White queenside, bit 2 = Black kingside, bit 3 = Black queenside) is passed in by the caller. `addCastlingMoves()` checks rights, empty intermediate squares, and that the king doesn't pass through or land on an attacked square.
- **En passant** — the target square (`state.epRow`, `state.epCol`) is passed in by the caller. `hasLegalEnPassantCapture()` checks whether an adjacent pawn can legally execute the capture (used by `Position` for Zobrist hashing).
- **Game state checks** — `attacks::isSquareUnderAttack(bb, sq, defendingColor)` uses precomputed attack table lookups + bitwise AND per piece type (knight, king, pawn) and O(1) slider attack functions (`attacks::rook`, `attacks::bishop`, `attacks::queen` via first-rank table + Hyperbola Quintessence) — takes only the `BitboardSet`, no mailbox needed. `hasAnyLegalMove(bb, mailbox, color, state)`. Check/checkmate/stalemate detection and draw queries (`isCheckmate`, `isStalemate`, `isInsufficientMaterial`, `isDraw`, `isGameOver`, `isThreefoldRepetition`, `isFiftyMoveRule`) are `Position` static methods. Threefold repetition detection takes `const HashHistory&`.

`Position` owns all position state (castling rights, en passant target, halfmove/fullmove clocks) via a `PositionState` struct and passes it to `movegen` methods as needed.

### zobrist

Also in `lib/core/` (`zobrist.h`, header-only). `zobrist` namespace provides Zobrist hashing for position comparison and threefold repetition detection. All 793 random keys (12×64 piece-square + 16 castling + 8 en passant + 1 side-to-move) are generated at compile time via a `constexpr` xorshift64 PRNG seeded with `0x12345678ABCDEF01`. The generated `Keys` struct is stored with `PROGMEM` (~6.2KB flash, zero RAM). Public API:

- `computeHash(bb, mailbox, turn, state, epLegal)` — computes the full Zobrist hash for a position. Iterates pieces via bitboard serialization, uses `piece::pieceIndex()` for piece-to-table-index mapping, XORs castling rights, conditionally XORs en passant only when a legal capture exists (passed in as the `epLegal` parameter — callers pre-compute this via `movegen::hasLegalEnPassantCapture()`), and XORs side-to-move for black. Used for debug verification; the hot path uses incremental hashing.

- `computePawnHash(bb)` — computes a pawn-only Zobrist hash. XORs piece keys for all white pawns (index 0) and black pawns (index 6). Used as the lookup key for the pawn hash table (`eval::PawnHashTable`).

- **Incremental hashing** — `Position::make()` updates `hash_` inline via XOR deltas: `hash_ ^= pieceKey[piece][from] ^ pieceKey[piece][to]`, plus castling key changes, en passant key changes, and side-to-move toggle. No full-board recompute on every move.

Consumed by `Position::recordPosition()` for position history tracking.

### piece

In `lib/core/` (`piece.h`, header-only). `piece` namespace provides type-safe piece representation via the `Piece`, `Color`, and `PieceType` enum classes. All functions are `constexpr` (except `colorName()`). Core API: `pieceType(piece)` / `pieceColor(piece)` (extraction), `makePiece(color, type)` (construction), `isEmpty` (predicate), `~color` / `~piece` (opponent flip via operator overload), `pawnForward()` / `homeRank()` / `promotionRank()` / `pawnStartRank()` (LERF-native color-derived constants), `charToPiece` / `pieceToChar` / `charToPieceType` / `pieceTypeToChar` (FEN char conversion via switches — MinGW g++ 5.1 constexpr limitation prevents lookup-table approach), `pieceIndex` overloads — unified 0–11 piece indexing for `BitboardSet::byPiece[]`, Zobrist keys, PST lookup, and move ordering arrays; three overloads: `pieceIndex(Color, PieceType)` (arithmetic: `raw(c)*6 + raw(pt)-1`), `pieceIndex(Piece)` (decomposes via `pieceColor`+`pieceType`), `pieceIndex(char)` (FEN char shorthand, e.g. `pieceIndex('K')` = 5, `pieceIndex('k')` = 11); `PIECE_IDX_NONE` sentinel (-1) for `Piece::NONE`; `isValidPieceIndex()` bounds predicate, `colorName(color)` → `"White"` / `"Black"`. No material values — centipawn values live in `eval::materialValue()`.

### Position

Also in `lib/core/` (`position.h/cpp`). Board representation and position-level chess logic — a pure position container with no lifecycle state. Owns a `BitboardSet bb_` (12 piece bitboards + 2 color aggregates + occupancy), a parallel `Piece mailbox_[64]` (flat array for O(1) piece identity by square), current turn, all position state via a `PositionState` struct (castling rights, en passant target, halfmove clock, fullmove clock), a king position cache (`Square kingSquare_[2]` — single LERF square index per color, public accessor `kingSq(c)` returns `Square`, maintained incrementally for O(1) check detection), a running Zobrist hash (`uint64_t hash_`, updated incrementally via XOR deltas in `make()`), incremental accumulators (`int material_`, `int mgPST_`, `int egPST_` — white-relative material/PST scores, updated via shared private helper `updateAccumulators()` called by `make()`), an EP legality cache (`bool epIsLegal_` — cached `hasLegalEnPassantCapture()` result, avoids redundant movegen in make/Zobrist hot path), an incremental game phase (`int phase_` — N=1,B=1,R=2,Q=4 max 24, tracked via `PHASE_WEIGHT[]` in `updateAccumulators()`, exposed via `phase()` getter), a 1-deep `UndoCache` for O(1) state restore in `reverseMove()` (stores `Move` + `UndoInfo` + post-move hash; cache-hit delegates to `unmake()`, cache-miss falls back to manual board reversal + `recomputeDerived()`), and a `HashHistory` for position tracking. `bitboards()` and `mailbox()` expose the internal representation for core-internal callers (`notation`, etc.). `getSquare(Square sq)` reads from `mailbox_[sq]`. Public API: `newGame()`, `loadFEN()` → `bool` (validates FEN before applying — checks rank count, valid characters, rank piece sum, and turn field; returns false and leaves board unchanged on invalid input), `makeMove(Square from, Square to, char promo)` → `MoveResult`, `makeNullMove()` / `unmakeNullMove()` (null move support for NMP in search), `getFen()`, `getCastlingRights()`, `positionState()`. Undo/redo support: `reverseMove(const MoveEntry&)` restores the board to the state before the given move (bitboard + mailbox rollback, captured piece, castling rook via `unmakeCastlingRook()`, position state, Zobrist history); uses a 1-deep `UndoCache` for O(1) accumulator restore (hash-validated, recomputes on miss — see Design Decisions in core.instructions.md). `applyMoveEntry(const MoveEntry&)` delegates to `makeMove()` with the entry's `from`/`to` Square values and promotion. Convenience wrappers delegate to `movegen` (implementations live in `position.cpp`, which includes `movegen.h` — `position.h` itself does not include them, keeping the header dependency graph cycle-free): `getPossibleMoves(Square sq, MoveList&)`, `inCheck()`, `isCheckmate()`, `isDraw()`, `isFiftyMoves()`. Game-end detection (`isCheckmate`, `isStalemate`, `isInsufficientMaterial`, `isDraw`, `isGameOver`, `isThreefoldRepetition`, `isFiftyMoveRule`) is implemented as `Position` static methods — formerly in the `rules` namespace, now merged into `Position`. Position also owns `checkEnPassant(Square from, Square to)` → `EnPassantInfo` and `checkCastling(Square from, Square to)` → `CastlingInfo` as member methods (structs defined in `types.h`), delegating to `utils::checkEnPassant` / `utils::checkCastling` Square-based free functions. `boardToText()` returns a human-readable board string for debugging. Game-end detection is invoked from `makeMove()` via `Position::isGameOver()`, covering checkmate, stalemate, 50-move draw, insufficient material (K vs K, K+B vs K, K+N vs K, K+B vs K+B same-color bishops), and threefold repetition (via Zobrist hashing — constexpr-generated keys in `zobrist.h`). En passant is only hashed when a legal capture exists (verified via `movegen::hasLegalEnPassantCapture()`). Lifecycle state (`gameOver_`, `gameResult_`, `winnerColor_`) and lifecycle methods (`endGame()`, `isGameOver()`) live in `Game` — Board has no concept of game-over. Move history, observer notification, undo/redo, and batching also live in `Game`. FEN/eval caching also lives in `Game` (dirty-flag pattern), keeping Position a pure state container with no caching overhead on the search hot path.

#### Why Bitboards + Mailbox?

The board uses a **dual representation**: a `BitboardSet` (12 piece bitboards + 2 color aggregates + occupancy) alongside a flat `Piece mailbox_[64]`. Both are updated in lockstep on every mutation. This is a standard pattern in chess programming — see [Chess Programming Wiki — Mailbox](https://www.chessprogramming.org/Mailbox) and [Bitboards](https://www.chessprogramming.org/Bitboards).

**Why not just bitboards?** Bitboards excel at set operations — "which squares have white knights?" is a single `uint64_t`. But the reverse question — "what piece is on e4?" — requires scanning all 12 piece bitboards (`bb.byPiece[i] & squareBB(e4)` for each `i`). The mailbox answers this with a single array read. Concrete callers that need O(1) piece identity: FEN serialization (iterates every square), SAN disambiguation ("which piece is on the source square?"), move application (piece and capture identity), en passant/castling analysis (piece identity at specific squares). 64 bytes of mailbox eliminates an O(12) scan on the hot path.

**Why not just a mailbox?** Attack detection with a mailbox requires walking rays direction by direction, checking each square. With bitboards, `attacks::isSquareUnderAttack` is a single `AND` per piece type against precomputed attack tables — e.g., `attacks::KNIGHT[sq] & bb.byPiece[knightIdx]` replaces a loop over 8 knight offsets with bounds checking. Slider attacks use O(1) Hyperbola Quintessence + first-rank table lookup rather than ray-walking loops.

**Performance in practice**: for LibreChess's current use case (board UI, single-move validation, sensor-driven play), the speed difference is negligible — the ESP32 spends orders of magnitude more time on WiFi, LED updates, and sensor reads. Where bitboards would genuinely matter is in **search** (alpha-beta, millions of `isSquareUnderAttack` calls per second) — if a local engine or puzzle solver is added, the infrastructure is ready. The practical benefits today are: cleaner attack detection code (lookup + AND vs. handwritten sliding loops), industry-standard representation (compatible with any chess programming resource), and correctness tooling (perft with detailed counters validates against known-good reference counts).

### History

Also in `lib/game/` (`history.h/cpp`). In-memory game history and persistent game recording with two concerns:

1. **Move log with undo/redo** — ordered list of `MoveEntry` structs with full move metadata (piece, captured, promotion, flags, previous `PositionState` for undo). Fixed-size array (`MAX_MOVES` = 300, ESP32-friendly). Cursor-based navigation: `addMove()`, `undoMove()` → `const MoveEntry*`, `redoMove()` → `const MoveEntry*`, `canUndo()`, `canRedo()`, `currentMoveIndex()`, `getMove()`, `lastMove()`, `moveCount()`, `clear()`. `addMove()` at a branch point (cursor not at end) wipes future moves and truncates the recording.
2. **Persistent recording** — automatic and optional. When an `IGameStorage*` is provided and a header has been set via `setHeader()`, `addMove()` persists encoded moves transparently (no explicit record calls). Recording API: `setHeader(GameHeader)` (creates live file, starts recording — replaces old `startRecording()`), `snapshotPosition(fen)` (replaces old `recordFen()`), `save(result, winner)` (replaces old `finishRecording()`), `discard()` (replaces old `discardRecording()`), `isRecording()`, `hasActiveGame()`, `getActiveGameInfo()`, `replayInto(Position&)`. Constructor: `History(IGameStorage* storage = nullptr, ILogger* logger = nullptr)` — when storage is null, recording is silently skipped.

Composed by `Game`.

### Game

Also in `lib/game/` (`game.h/cpp`). Central game orchestrator — the primary API for all game-level operations. Composes `Position`, `History`, and optionally `IGameObserver`. Constructor: `(IGameStorage*, IGameObserver*, ILogger*)`.

All chess-state mutations (`makeMove`, `loadFEN`, `endGame`, `newGame`) flow through this class. `Game` is the sole owner of lifecycle state: `gameOver_`, `gameResult_`, and `winnerColor_`. The board has no concept of game-over — `endGame()`, `isGameOver()`, `gameResult()`, and `winnerColor()` exist only on `Game`. `makeMove()` atomically: validates via board, applies (`Position::isGameOver()` handles all end conditions including threefold repetition, returning results via `MoveResult`), reads `MoveResult` to update lifecycle state, records in history with full `MoveEntry` metadata (addMove handles both in-memory log and persistence automatically), auto-saves recording on game-end and notifies observer. `undoMove()` and `redoMove()` step the history cursor and call `Position::reverseMove()`/`applyMoveEntry()` respectively; `undoMove()` clears lifecycle state. `canUndo()` and `canRedo()` delegate to `History`.

Draw detection: `isDraw()` delegates to `Position`, which handles all draw conditions internally (50-move rule, threefold repetition, insufficient material).

Batching: `beginBatch()`/`endBatch()` suppress observer notifications during multi-step operations (e.g., replay), firing a single notification when the outermost batch completes.

Notation convenience methods: `makeMove(const std::string& move)` parses a coordinate notation string and delegates to the Square-native overload. `static toCoordinate()` and `static parseCoordinate()` wrap `notation` — firmware game modes use these instead of including `notation` directly. `getHistory(out[], maxMoves, MoveFormat)` returns move history in coordinate, SAN, or LAN format. All query/mutation methods (`makeMove`, `getSquare`, `getPossibleMoves`, `checkEnPassant`, `checkCastling`) provide dual overloads: Square-native (primary implementation) and row/col (thin wrapper for firmware convenience).

### eval, utils & SystemUtils

`eval` (in `lib/core/`, `evaluation.h/cpp`) is a namespace providing tapered position evaluation via `evaluatePosition(const BitboardSet& bb)`. Three overloads: `evaluatePosition(bb)` (full computation), `evaluatePosition(bb, mg, eg, pawnHash)` (pre-computed material+PST), `evaluatePosition(bb, mg, eg, phase, pawnHash)` (pre-computed material+PST + phase — used by search when `Position::phase()` is available, avoids recomputing phase each call). `computeGamePhase(bb)` is public (used by Position for incremental tracking). `PHASE_WEIGHT[]` lookup table maps PieceType → phase contribution. Returns a score in centipawns (positive = White advantage). Computes separate midgame (MG) and endgame (EG) scores, each combining material counting (MG via `MATERIAL[]` with pawn fixed at 100cp, EG via `MATERIAL_EG[]` with pawn EG tunable), phase-specific piece-square tables (all six piece types have separate MG and EG tables), pawn structure bonuses/penalties (rank-based exponential passed pawn scaling via `PASSED_RANK_BONUS_MG/EG[8]`, isolated MG/EG, doubled MG/EG, backward MG/EG, connected passers MG/EG, protected passer (MG only) — via file-scoped constexpr pawn mask arrays (`PawnMasks` struct in anonymous namespace, placed in .rodata); passed and forward masks stored white-only with black derived via vertical mirror), passed pawn king distance (EG only, not pawn-hashed — `evalPassedPawnKingDist` receives pre-collected `passedPawns[2]` bitboards and applies Chebyshev-distance bonuses: own king proximity + enemy king proximity), and positional terms: bishop pair (+30 MG / +50 EG), bad bishop (penalty per own pawn on same color complex as bishop, −3 MG / −5 EG each, via `evalBadBishop`), rook on open file (MG/EG split) / semi-open file (MG/EG split), rook on 7th rank (bonus when enemy king on back rank or enemy pawns on starting rank), rook behind passer (Tarrasch Rule, EG only: +15 own rook behind own passer / −10 enemy rook behind our passer, via `evalRookBehindPasser`; also receives `passedPawns[2]`), piece mobility (MG/EG split weights per piece type: N 4/4, B 3/5, R 1/3, Q 1/2, via `attacks::computeAll()` → `AttackInfo`), king safety / pawn shield (missing pawn −15, rank-indexed advanced pawn penalty via `SHIELD_ADV_RANK3` / `SHIELD_ADV_RANK4PLUS`, open file −20, MG only), king danger (unified zone attack counting + proximity: nonlinear `KING_DANGER_TABLE[13]` indexed by weighted attacker sum using `KING_DANGER_WEIGHT[]` {N=2, B=2, R=3, Q=5} + proximity bonus for pieces within Chebyshev distance ≤ 3 of king when zone attackers exist, MG only), knight outposts (MG/EG split, doubled for central squares), space (safe squares behind pawn chain in files c–f, MG only via `SPACE_BONUS_MG`), trapped pieces (bishop trapped on a7/h7/a2/h2 by enemy pawn −50 MG, rook trapped by own uncastled king −40 MG), and threats (MG/EG bonus for pawn→minor, pawn→rook, pawn→queen, minor→rook, minor→queen MG only, rook→queen MG only via `evalThreats`, uses `AttackInfo::byPiece` intersection). Opposite-color bishop scaling (×0.75) is applied to the final interpolated score when each side has exactly one bishop on different color complexes in the endgame (phase ≤ 6). Tempo bonus (+10 cp for side to move) is applied in the search layer's `evaluate()` wrapper. All six piece types have distinct MG/EG PSTs (knight EG: support squares; bishop EG: long diagonals; rook EG: 7th rank emphasis; queen EG: central activity; king: safety → centralization; pawn: center control → advancement). Game phase is derived from non-pawn material (N=1, B=1, R=2, Q=4; max 24); final score interpolates: `(mg * phase + eg * (24 - phase)) / 24`. Based on the simplified evaluation function (CPW / Tomasz Michniewski), extended with positional heuristics. Black pieces mirror via `sq ^ 56`. Iterates piece bitboards via `popLsb` — no mailbox needed.

`utils` (in `lib/core/`, `utils.h`, header-only) is a namespace providing stateless board-level helper functions: LERF-native coordinate helpers (`squareName(Square)`, `fileChar`, `rankCharFromRank`, `fileIndex`, `rankIndexFromChar`), validation (`isValidPromotionChar`), castling rights string formatting/parsing (`castlingRightsToString`/`castlingRightsFromString`), `hasCastlingRight()` via `BIT[2][2]` lookup, `castlingCharToBit()`, `updateCastlingRights()` (pure lookup-table function returning updated castling rights bitmask), `resolveKingSquare()` (inline king-square finder used by movegen + search). Display-coordinate helpers (`rankChar(row)`, `squareName(row,col)`) live in `game/types.h`. `gameResultName()` lives in `types.h` next to `GameResult`. `checkEnPassant(mailbox, from, to)` and `checkCastling(mailbox, from, to)` are Square-based free functions for EP/castling detection from a mailbox + square pair — shared by both `Position` member methods (row/col API) and `movegen` (Square API). `boardToText` is a `Position` member method. `EnPassantInfo` and `CastlingInfo` structs live in `types.h`. `fen` (in `lib/core/`, `fen.h/cpp`) centralizes all FEN string handling: `boardToFEN()` (mailbox → FEN string), `fenToBoard()` (FEN string → `BitboardSet` + `mailbox` + state), and `validateFEN()` (format validation). `notation` (in `lib/core/`, `notation.h/cpp`) provides Square-native move notation conversion: coordinate notation (`"e2e4"`), SAN (`"Nf3"`), and LAN (`"Ng1-f3"`) output and parsing. Parse functions output `Square` (LERF) directly; format functions take `Square`. All functions are pure — `const BitboardSet&` and `const Piece mailbox[]` are passed in as parameters. Output functions omit check/checkmate suffixes; the caller appends them. `Game` provides row/col wrappers (`toCoordinate()`, `parseCoordinate()`) for firmware callers that work in display coordinates. All functions use `std::string` (not Arduino `String`). Internal APIs (`updateBoardState`, `addFen`, `setBoardStateFromFEN`) accept `std::string` directly; Arduino `String` conversion happens only at the hardware/network boundary (e.g., HTTP responses, LittleFS reads).

`SystemUtils` (in `src/`) contains the Arduino/ESP32-dependent functions that were separated from the core library: `colorLed()` (piece char → LED color) and `ensureNvsInitialized()` (Arduino Preferences guard). Board debug output uses `Position::boardToText()`. These are not available in native tests.

### WiFiManagerESP32

Manages WiFi connectivity, the web server, and all HTTP API endpoints. Key subsystems:

**WiFi state machine** — event-driven `WiFiState` enum (`AP_ONLY`, `CONNECTING`, `CONNECTED`, `RECONNECTING`) managed via `WiFi.onEvent()` callbacks. Uses a singleton `instance` pointer for the static event callback to access instance state.

- **AP lifecycle**: The access point (`LibreChess`, password `chess123`, IP `192.168.4.1`) starts immediately on boot. After a stable STA connection is maintained for `AP_STABILIZATION_MS` (10 seconds), a FreeRTOS timer callback (`apStabilizationCallback`) disables the AP. If the STA connection drops, the AP re-enables immediately. This stabilization window prevents flapping when WiFi is intermittent.
- **Reconnection**: On STA disconnect, the state transitions to `RECONNECTING`. The firmware cycles through all saved networks with exponential backoff (starting at `RECONNECT_INITIAL_MS` = 5 seconds, capped at `RECONNECT_MAX_MS` = 60 seconds). `reconnectNetworkIndex` tracks which network to try next.
- **mDNS**: hostname `librechess` (defined as `MDNS_HOSTNAME`), started in `begin()` and restarted in `handleWiFiConnected()` to rebind to the STA interface. Enables `http://librechess.local` access.

**Known-networks registry** — up to `MAX_SAVED_NETWORKS` (3) WiFi networks stored in NVS namespace `"wifiNets"` (keys: `"count"`, `"ssid0"`/`"pass0"` through `"ssid2"`/`"pass2"`). On boot, networks are loaded and tried in order.

**Web server** — `AsyncWebServer` on port 80. Serves gzipped static files from LittleFS via `serveStatic`. API endpoints handle JSON requests for board state, game selection, settings, WiFi management, Lichess token, OTA updates, game history, board editing, and resign. All configuration getters and setters are exposed as `public` methods for the main loop to relay state between the web layer and game logic (e.g., `getSelectedGameMode()`, `getPendingBoardEdit()`, `getPendingResign()`).

**Board state relay** — `WiFiManagerESP32` implements `IGameObserver`. `Game` calls `onBoardStateChanged(fen, evaluation)` automatically after every board mutation. The observer caches the FEN, evaluation, and navigation state (move index, move count, undo/redo availability, move list). The web UI polls `GET /board-update` which returns all cached state as JSON.

**Move navigation** — `WiFiManagerESP32` holds a `const Game*` reference (set via `setGameRef()`) for read-only navigation queries. `POST /nav` sets a pending action flag; the main loop applies it to `Game`. Navigation is blocked during the engine's turn in bot mode via `GameMode::isNavigationAllowed()`.

**Board editing** — `handleBoardEditSuccess()` stores a pending FEN string from the web UI's board editor. The main loop checks `getPendingBoardEdit()` each cycle and applies it to the active game via `setBoardStateFromFEN()`, then calls `clearPendingEdit()`.

**Web resign** — the web UI's resign button sends `POST /resign`. The handler sets `hasPendingResign = true`. The main `loop()` relays this to the active game via `setResignPending(true)`, then clears the web flag. The game's `processResign()` picks it up on the next `update()` call.

### History / Game / LittleFSStorage

Game recording and crash recovery follow a layered architecture with clean separation:

- **`History`** (`lib/game/`) — handles in-memory move logging with cursor-based undo/redo and persistent recording orchestration. Recording is automatic: when an `IGameStorage*` is present and a header has been set, `addMove()` persists transparently. Compact 2-byte move encoding via public static `encodeMove()`/`decodeMove()` methods. Manages the `GameHeader`, delegates persistence to `IGameStorage`. Flushes the header to storage every full turn (2 half-moves) to reduce flash wear; FEN snapshots always trigger an immediate flush. Branch-on-undo: when `addMove()` is called with the cursor not at the end, future moves are wiped and storage is truncated via `IGameStorage::truncateMoveData()`. Validates moves during replay — rejects corrupted recordings with invalid moves. Replays games directly into a `Position` and populates the in-memory move log.
- **`Game`** (`lib/game/`) — central game orchestrator composing `Position` + `History` + `IGameObserver`. Constructor: `(IGameStorage*, IGameObserver*, ILogger*)`. Each mutation (move, load FEN, end game) atomically updates the board, records in history (persistence is automatic via `addMove()`), and notifies the observer. Provides `startNewGame(playerColor, meta)` for atomic board-reset + recording-start. `endGame()` is guarded against double-calls. `undoMove()`/`redoMove()` step the history cursor and update the board. Exposes convenience pass-throughs to `Position` query methods: `getPossibleMoves()`, `checkEnPassant()` (→ `EnPassantInfo`), `checkCastling()` (→ `CastlingInfo`), `isDraw()` — so game mode classes never need to access `Position` or `movegen`/`rules` directly. Also provides notation convenience methods (`makeMove(string)`, `toCoordinate()`, `parseCoordinate()`, `getHistory(format)`) so firmware never needs to include `notation` directly. Static utility wrappers (`isEmptySquare()`, `pieceColor()`, `pieceType()`, `pieceToChar()`, `colorName()`, `squareName()`, `fileChar()`, `rankChar()`) re-export `piece` and `utils` helpers so firmware never needs to include `piece.h` or `utils.h` directly.
- **`LittleFSStorage`** (`src/`) — concrete `IGameStorage` backed by LittleFS.

**Binary format** — each game consists of two files:
- `<id>.bin` (or `live.bin`) — 16-byte packed `GameHeader` followed by 2-byte compact-encoded move entries
- `<id>_fen.bin` (or `live_fen.bin`) — FEN snapshot table for efficient position reconstruction

The `GameHeader` struct (exactly 16 bytes, `#pragma pack(push, 1)`) contains:
| Field | Type | Description |
|-------|------|-------------|
| `result` | `GameResult` | Game outcome enum class (0 = IN_PROGRESS, 1 = CHECKMATE, 2 = STALEMATE, 3 = DRAW_50, 4 = DRAW_3FOLD, 5 = RESIGNATION, 6 = DRAW_INSUFFICIENT, 7 = DRAW_AGREEMENT, 8 = TIMEOUT, 9 = ABORTED) |
| `winnerColor` | `uint8_t` | `'w'`, `'b'`, `'d'` (draw), or `'?'` (in-progress) |
| `playerColor` | `uint8_t` | Human's color (`'w'`/`'b'`), `'?'` if unset |
| `moveCount` | `uint16_t` | Number of 2-byte entries (including FEN markers) |
| `fenEntryCnt` | `uint16_t` | Number of FEN table entries |
| `lastFenOffset` | `uint16_t` | Byte offset of last FEN entry within the FEN table |
| `timestamp` | `uint32_t` | Unix epoch from NTP (0 if unavailable) |
| `meta` | `uint8_t[3]` | Opaque firmware metadata (mode + engineId + difficulty, interpreted by game_mode.h) |

**Move encoding** — each move is packed into 2 bytes: `[from_square(6 bits)][to_square(6 bits)][promotion(4 bits)]`. Square index = `row * 8 + col`. Promotion codes: 0 = none, 1 = queen, 2 = rook, 3 = bishop, 4 = knight. The special marker `0xFFFF` (`FEN_MARKER`) indicates that a FEN snapshot was recorded at this point in the move sequence.

**FEN snapshots** — periodic FEN strings are appended to the FEN table file. During replay, the system finds the last FEN snapshot, restores the board to that position, and replays only the moves that follow. This bounds replay time regardless of game length.

**Crash recovery** — during gameplay, moves are appended to `live.bin` and FEN snapshots to `live_fen.bin` in real time. The header is flushed every full turn (2 half-moves) rather than every move, reducing flash write cycles by half. On boot, `hasActiveGame()` checks if these files exist. If so, `getActiveGameInfo()` reads the header to determine the mode and configuration, and `History::replayInto()` restores the full game state directly into the `Position`. `Game::resumeGame()` wraps this and fires a single observer notification after replay completes. Move data size is derived from the file size (not the header's `moveCount`) for robustness against mid-turn crashes. Each move is validated during replay — corrupted or invalid moves cause the replay to abort.

**Storage limits** — `MAX_GAMES` = 50 games, `MAX_USAGE_PERCENT` = 80% of LittleFS capacity. `enforceStorageLimits()` is called after each game finishes and deletes the oldest games (lowest ID) until both limits are satisfied.

**Game list API** — `getGameListJSON()` returns a JSON array of all completed games with metadata (id, mode, result, winner, move count, timestamp, bot config). Used by the web UI's game history panel.

### Game Mode Interaction with Core

The `GameMode` base class coordinates between `BoardDriver` (hardware) and `Game` (chess state + recording + notification):

1. `tryPlayerMove()` — polls sensors for a piece lift, shows valid moves via `chess_->getPossibleMoves(row, col, ...)` (which delegates to `movegen::getPossibleMoves()`), waits for placement, validates the move, then calls `applyMove()`.
2. `applyMove()` — calls `chess_->makeMove()` which atomically updates the board, records the move in history, persists via History's recording, and notifies the observer. Returns a `MoveResult` struct with all move metadata. The firmware then uses the `MoveResult` to drive LED feedback, sounds, remote-move guidance, and game-end animations.
3. Turn advancement, castling rights, and game-end detection are all handled internally by `Position::makeMove()` — the firmware never modifies the board or turn directly.

## Game Mode Lifecycle

### Boot Sequence

1. `Serial.begin(115200)`, NVS initialization
2. (Optional) Factory reset if `-DFACTORY_RESET` build flag is set
3. `LittleFS.begin()` — mount filesystem
4. `storage.initialize()` — create `/games/` directory if needed
5. `boardDriver.begin()` — initialize LED strip, GPIO pins, calibration (may block for interactive serial calibration on first boot), and start the animation FreeRTOS task
6. `wifiManager.begin()` — start AP, load saved networks, begin STA connection attempts, start web server, configure mDNS
7. `initMenus(&boardDriver)` — two-phase menu initialization (set `BoardDriver*` on all menus, configure items and back buttons)
8. `configTime(0, 0, "pool.ntp.org", "time.nist.gov")` — non-blocking NTP sync
9. `checkForResumableGame()` — if a live game exists on flash, show a confirm dialog and optionally resume
10. If not resuming, `enterGameSelection()` — push the root menu onto the navigator stack

### Main Loop

Every iteration of `loop()`:

1. `wifiManager.update()` — handle WiFi reconnection state machine
2. Check for pending board edits from the web UI (`getPendingBoardEdit()`)
3. Check for web-based game mode selection (`getSelectedGameMode()`)
4. If in `AppMode::SELECTION`: poll the menu navigator, handle results via `handleMenuResult()`
5. If in a game mode and not yet initialized: call `initializeSelectedMode()` (creates the game object, calls `begin()`)
6. If in a game mode: relay web resign flag, check `isGameOver()`, call `update()`
7. `delay(SENSOR_READ_DELAY_MS)` — 40ms pause for sensor polling cadence

### Mode Initialization

`initializeSelectedMode()` performs cleanup and setup:

1. If not resuming, discard any leftover live game file
2. `delete` the previous `activeGame` and `sensorTest` objects
3. Create the new game object via `new` (the only heap allocation for game modes)
4. Call `begin()` — which typically calls `waitForBoardSetup()` to wait for correct piece placement, then `chess_->startNewGame(playerColor, metaBytes(meta))` to begin recording

For game resume: `begin()` detects the `resumingGame` flag, skips piece setup, calls `chess_->resumeGame()` which delegates to `History::replayInto()` to restore the full game state directly into the `Position`, then continues with normal `update()` calls.

### Menu Navigation

The `MenuNavigator` manages a stack of `BoardMenu` instances (max depth 4):

- **Game selection** (root) → 4 center squares: Blue (ChessMoves), Green (Bot), Yellow (Lichess), Red (SensorTest)
- **Bot difficulty** (pushed on Bot selection) → 8 squares across row 3, colors green→blue, levels 1–8 (engine resolves level → depth)
- **Bot color** (pushed on difficulty selection) → 3 squares: White, DimWhite (play as Black), Yellow (random)

Menu IDs use distinct ranges per level (0–9 root, 10–19 difficulty, 20–29 color) so `handleMenuResult()` can route by ID value alone — no callbacks or virtual dispatch.

The web UI can also trigger game selection via `POST /game/select`, setting `gameMode` and bot configuration on `WiFiManagerESP32`. The main loop detects this, bypasses the physical menu, and proceeds directly to mode initialization.

## Resign System

### Physical Resign Gesture

The gesture runs inline inside `tryPlayerMove()` — no separate state machine. The flow:

1. Player lifts their king on their own turn. `tryPlayerMove()` detects the lift and starts a timer.
2. If the king stays off the board for `RESIGN_HOLD_MS` (3000ms), the resign sequence begins. The origin square shows orange at 25% brightness via `showResignProgress(row, col, 0)`.
3. Player returns the king. Orange increases to 50% (`showResignProgress(row, col, 1, clearFirst=true)`). All other LEDs are cleared.
4. `continueResignGesture()` takes over — a blocking loop that waits for 2 more quick lift-and-return cycles, each within `RESIGN_LIFT_WINDOW_MS` (1000ms). Orange progresses to 75% then 100%.
5. If all lifts complete in time, `boardConfirm()` shows a yes/no dialog (green/red squares).
6. On confirm, `handleResign(resignColor)` is called. The base implementation ends the game with `GameResult::RESIGNATION` and plays a firework animation in the opponent's color. In bot mode, `BotMode::onResignConfirmed()` delegates to `EngineProvider::onResignConfirmed()` — `LichessProvider` sends a resign request to the Lichess server.

If any step times out (king not returned within the window), the gesture is silently canceled — no error feedback, just a return to normal play. The progressive orange brightness (25% → 50% → 75% → 100%) uses `LedColors::scaleColor(LedColors::Orange, factor)` with factors from `RESIGN_BRIGHTNESS_LEVELS`.

LED helper functions encapsulate the mutex pattern:
- `showResignProgress(row, col, level, clearFirst)` — acquires `LedGuard`, optionally clears all LEDs, sets the square color, shows
- `clearResignFeedback(row, col)` — acquires `LedGuard`, turns off the square
- `showIllegalMoveFeedback(row, col)` — queues a red blink for illegal moves

### Web Resign

1. Web UI: ⚑ button → JS `confirm()` → `POST /resign` via `Api.resign()`
2. `WiFiManagerESP32`: sets `hasPendingResign = true`
3. `main.cpp loop()`: relays `wifiManager.getPendingResign()` → `activeGame->setResignPending(true)`, clears web flag
4. Game `update()` → `processResign()` checks `resignPending` flag → calls `handleResign(sideToMove)`

### Turn Restriction

Resign is only processed on the current player's turn in all modes, matching real chess conventions.

### Web Navigation

Move history navigation is server-driven: the web UI sends navigation commands via `POST /nav`, the server applies them to `Game`'s undo/redo system, and the frontend reads the updated state from `GET /board-update`.

1. Web UI: nav button → `Api.nav(action)` → `POST /nav` with `action=undo|redo|first|last`
2. `WiFiManagerESP32`: validates action, sets `pendingNavAction_`. If `navigationBlocked_`, returns `409`.
3. `main.cpp loop()`: reads `wifiManager.getPendingNavAction()`, checks `activeGame->isNavigationAllowed()`, applies via `chess.undoMove()` / `chess.redoMove()` (loops for first/last), clears flag.
4. `Game::undoMove()`/`redoMove()` steps the history cursor and updates the board, then notifies the observer.
5. `WiFiManagerESP32::onBoardStateChanged()` caches `moveIndex`, `moveCount`, `canUndo`, `canRedo`, and the move list as JSON.
6. Next `GET /board-update` poll returns the navigated position FEN and all cached navigation state.

**Navigation blocking** — `GameMode::isNavigationAllowed()` is a virtual method (default: `true`). `BotMode` overrides it to return `true` only when `botState_ == BotState::PLAYER_TURN` or when the game is over. The main loop updates `wifiManager.setNavigationBlocked()` each tick so the async web handler can reject requests immediately.

## LED System

### Animation Queue

Animations run on a dedicated FreeRTOS task (`animationWorkerTask`) with its own queue (`animationQueue`, type `QueueHandle_t`). The task runs in an infinite loop: dequeue an `AnimationJob`, acquire the LED mutex, execute the animation, release the mutex, and signal the done semaphore if applicable.

`AnimationJob` is a struct with a `type` field (`AnimationType` enum: `CAPTURE`, `PROMOTION`, `BLINK`, `WAITING`, `THINKING`, `FIREWORK`, `FLASH`, `SYNC`) and a `params` union containing type-specific data. The `SYNC` type is a no-op barrier — `waitForAnimationQueueDrain()` enqueues a SYNC job and blocks on the `animationDoneSemaphore` until the worker reaches it.

**Short animations** (capture, promotion, blink, firework, flash) — fire-and-forget. The caller enqueues the job and returns immediately. The animation task dequeues and executes it.

**Long-running animations** (thinking, waiting) — return an `std::atomic<bool>*` stop flag (heap-allocated). The animation task checks the flag on each frame. The caller owns the flag and must use `stopAndWaitForAnimation(flag)` to:
1. Set the flag to `true`
2. Block on `animationDoneSemaphore` until the worker finishes the current frame and releases the LED mutex
3. `delete` the flag and null the pointer

Never set the flag directly or `delete` it without waiting — the animation task may still be mid-frame with the LED mutex held.

### LedGuard (RAII Mutex)

The LED strip is a shared resource between the main loop and the animation task, guarded by `ledMutex` (FreeRTOS semaphore). `BoardDriver::LedGuard` is an RAII wrapper:

```cpp
{
    BoardDriver::LedGuard guard(boardDriver);
    boardDriver.clearAllLEDs();
    boardDriver.setSquareLED(row, col, LedColors::Cyan);
    boardDriver.showLEDs();
}
```

Single animation calls (`blinkSquare`, `captureAnimation`, etc.) are queued and acquire the mutex inside the animation task — no guard needed by the caller.

**Critical rule**: before writing LEDs directly from the main loop, call `waitForAnimationQueueDrain()` to ensure all queued animations have completed. Otherwise a stale queued animation can execute after your writes and overwrite them.

### Color Semantics

Colors in the `LedColors` namespace (`led_colors.h`) have fixed meanings:

| Color | RGB | Meaning |
|-------|-----|---------|
| Cyan | (0, 255, 255) | Piece origin — "pick up from here" |
| White | (255, 255, 255) | Valid move destination, menu back button |
| DimWhite | (40, 40, 40) | "Play as Black" option in bot color menu |
| Red | (255, 0, 0) | Capture square, illegal move, error |
| Green | (0, 255, 0) | Move confirmed, "yes" in confirm dialogs |
| Yellow | (255, 200, 0) | King in check, pawn promotion, random option |
| Purple | (128, 0, 255) | En passant captured pawn square |
| Orange | (255, 80, 0) | Resign gesture progress |
| Blue | (0, 0, 255) | Bot thinking, Human vs Human mode indicator |
| Lime | (100, 200, 0) | Easy difficulty level |
| Crimson | (200, 0, 50) | Hard difficulty level |
| Off | (0, 0, 0) | LED off |

`scaleColor(color, factor)` is an `inline constexpr` helper that multiplies RGB components by a float factor (0.0–1.0), clamped to 255. Used for brightness progression effects like the resign gesture.

### Animation Types

| Type | Duration | Description |
|------|----------|-------------|
| Capture | ~1s | Concentric wave rings from capture square. Red/yellow alternating with quadratic intensity falloff. |
| Promotion | ~1.6s | Yellow waterfall cascading down the promotion column. |
| Blink | Configurable | Square blinks in a given color N times. Used for check warnings (yellow, 3x), move confirmation (green, 1x), illegal move (red, 2x). |
| Firework | ~2.4s | Ring of light contracts from board edges to center, then expands back. Color matches the winner or event type. |
| Flash | Configurable | Entire board flashes a color N times. Used for critical errors (red, 3x). |
| Thinking | Continuous | Four corner squares pulse blue with sinusoidal breathing (8%–100% brightness). Slight purple hue shift at low brightness. |
| Waiting | Continuous | White chase animation traces 28 perimeter squares clockwise. Two groups of 8 LEDs travel diametrically opposite. |
| Connecting | One-shot | Two center rows fill with blue from left to right, column by column. |

## Menu System

### BoardMenu

A reusable menu primitive for the 8×8 LED grid. State is stack-allocated — no heap usage.

**Item definition** — `MenuItem` struct: `{row, col, color, id}`. Coordinates are authored in white-side orientation (row 7 = rank 1). Arrays are `constexpr` file-scoped statics in `menu_config.h`, stored in flash with zero RAM cost. The menu does not copy the array — the pointer must outlive the menu.

**Two-phase debounce** — prevents pieces already on the board from triggering selections when a menu appears:
1. Phase 1 (empty): the square must read empty for `DEBOUNCE_CYCLES` (5) consecutive sensor polls (~200ms)
2. Phase 2 (occupied): the square must then read occupied for another 5 consecutive polls
Only a deliberate "place a piece on an empty square" action registers.

After confirmed selection, the square blinks once in its own color (`blinkSquare()`) for visual feedback, then the system waits for the piece to be removed before returning — preventing input bleed into the next menu or game.

**Back button** — set via `setBackButton(row, col)`, lit in `LedColors::White`. Omit for root menus. The navigator auto-pops on back and re-shows the parent.

**Orientation** — `setFlipped(true)` mirrors row coordinates (`row' = 7 - row`) so menus face a player on the black side. Applied to bot games where the player chose black, and to the resign confirm dialog on black's turn.

**`boardConfirm()`** — standalone yes/no dialog (green at d4, red at e4). Blocking. Returns `bool`. Supports orientation flipping.

### MenuNavigator

Stack-based orchestrator with max depth 4 (`std::array<BoardMenu*, MAX_DEPTH>`). Push/pop navigation with automatic back-button handling and parent re-display with fresh debounce state. `clear()` hides the current menu and empties the stack (used when an external event like WiFi game selection overrides the menu).

### Menu Configuration

All menu layout data lives in `menu_config.h/cpp`:
- `MenuId` namespace: distinct ID ranges per menu level (0–9 root, 10–19 difficulty, 20–29 color)
- `constexpr MenuItem[]` arrays: `gameMenuItems`, `botDifficultyItems`, `botColorItems`
- `extern` instances: `gameMenu`, `botDifficultyMenu`, `botColorMenu`, `navigator`
- `initMenus(BoardDriver* bd)`: two-phase initializer — sets `BoardDriver*`, configures items, sets back buttons. Called once in `setup()`.

## External API Integration

### Stockfish

`StockfishAPI` (in `engine/stockfish/stockfish_api.h/cpp`) handles HTTPS requests to `stockfish.online` using `WiFiClientSecure`:
- Builds request URLs with FEN and depth parameters
- Parses JSON responses for best move, evaluation, and continuation line
- Connection uses TLS with `setInsecure()` (no certificate pinning)

Each engine provider defines a static `LEVELS[8]` array mapping difficulty level (1–8) to a label and search depth. `GameMeta.difficulty` stores the level number (1–8), not the raw depth. Providers are constructed with a level and player color, then passed to `BotMode` as an `EngineProvider*`.

**StockfishProvider** (depths 6–16, matching the stockfish.online API):

| Level | Name | Depth |
|-------|------|-------|
| 1 | Beginner | 6 |
| 2 | Easy | 7 |
| 3 | Intermediate | 8 |
| 4 | Medium | 9 |
| 5 | Advanced | 10 |
| 6 | Hard | 12 |
| 7 | Expert | 14 |
| 8 | Master | 16 |

**LibreChessProvider** (depths 1–8, safe for ESP32 stack):

| Level | Name | Depth |
|-------|------|-------|
| 1 | Beginner | 1 |
| 2 | Easy | 2 |
| 3 | Intermediate | 3 |
| 4 | Medium | 4 |
| 5 | Advanced | 5 |
| 6 | Hard | 6 |
| 7 | Expert | 7 |
| 8 | Master | 8 |

### LibreChess (On-Board Engine)

The on-board engine runs entirely within `lib/core/` — no network, no external API. Two namespaces:

- **`search`** (`search.h/cpp` + `move_picker.h`) — Negamax with alpha-beta pruning, quiescence search (MVV-LVA capture ordering, pawn-defended-pawn pruning), iterative deepening, check extensions, recapture extensions (cached SEE ≥ 0), singular extensions (exclusion search at TT-hit nodes with depth ≥ 6: searches all moves except TT move at half depth to verify it is significantly better; extends by 1 ply if singular), PVS (Principal Variation Search), NMP (Null Move Pruning, adaptive R), LMR (Late Move Reductions — logarithmic table + history-informed + improving-aware + non-PV adjustment), LMP (Late Move Pruning — improving-aware thresholds), history pruning (pre-make skip of quiet moves with deeply negative history at shallow depths), reverse futility pruning (staticEval - margin*depth/(1+improving) ≥ beta), razoring (drop into quiescence when static eval is far below alpha), lazy evaluation (material-only shortcut, 300cp margin), aspiration windows (gradual doubling on fail), root move reordering, internal iterative reductions (IIR), delta pruning (quiescence capture futility), futility pruning (shallow negamax leaf skipping), SEE-based capture ordering (losing captures demoted below quiets, SEE computed lazily when captures are yielded), transposition table (`TranspositionTable` in `search.h`, 12-byte `TTEntry`, inherits `HashTableBase`), move ordering (`MovePicker` in `move_picker.h` — staged: TT move → good captures (MVV-LVA, lazy SEE≥0) → killer moves → countermove heuristic → history heuristic → bad captures (SEE<0)), and improving flag (ply-2/ply-4 eval tracking). `findBestMove(pos, limits, state, info)` is the single public entry point. `SearchLimits` controls depth, time, and external stop flag. `SearchState` (required, caller-owned) holds per-search heuristics (killers, history table, countermove table, staticEvals) and infrastructure pointers (`timeFunc`, `tt`, `pawnHash`, `evalHash`) set by the caller before calling.

- **`uci`** (`uci.h/cpp`) — Transport-agnostic UCI protocol handler. `UCIStream` is the abstract I/O interface (Serial, string buffer). `UCIHandler` owns a `Position`, `TranspositionTable`, and stop flag; dispatches standard UCI commands (`uci`, `isready`, `ucinewgame`, `position`, `go`, `stop`, `quit`). Simple time management from game clocks (remaining/30 + increment/2). `StringUCIStream` provides in-memory I/O for testing and in-process use.

`LibreChessProvider` is constructed with a difficulty level (1–8) and player color, then passed to `BotMode` as an `EngineProvider*`. `SerialUCIStream` enables external UCI GUIs (Arena, CuteChess) to drive the engine over the ESP32's UART.

### Lichess

`LichessAPI` (in `engine/lichess/lichess_api.h/cpp`) handles HTTPS requests to `lichess.org`:
- **Game event polling** — checks for active or incoming games
- **Game stream polling** — retrieves the current game state (moves, status, clocks)
- **Move submission** — sends a UCI move to the active game
- **Resignation** — submits a resign request

The Lichess token is stored in NVS (namespace `"lichess"`, key `"token"`). The web UI's Lichess settings page allows entering or clearing the token. API responses to the web UI return only a masked version of the token (first 4 characters + asterisks).

`LichessProvider` spawns a FreeRTOS polling task (via `requestMove()`) that calls `LichessAPI::pollGameStream()` every 500ms. The task compares the server's move list against `lastKnownMoves_` and returns new opponent moves via `EngineResult`. `lastSentMove_` prevents the player's own move echo from being treated as an opponent move. Game-end events from the server are also detected and returned as `EngineResult::Type::GAME_ENDED`.

## Storage

### LittleFS

LittleFS partitions the ESP32's flash for file storage. Used for:

**Web assets** — gzip-compressed HTML, CSS, JS, images, and sounds placed in `data/` during the build pipeline. The web server serves them with `Content-Encoding: gzip` headers via `serveStatic`. Files matching `*.nogz.*` skip compression (used for binary assets like MP3s).

**Game history** — binary files under `/games/`:

| File | Purpose |
|------|---------|
| `/games/live.bin` | Current in-progress game (header + moves) |
| `/games/live_fen.bin` | FEN snapshots for the current game |
| `/games/<id>.bin` | Completed game (header + moves) |
| `/games/<id>_fen.bin` | FEN snapshots for completed game |

Storage limits: max 50 games, 80% of LittleFS capacity. `enforceStorageLimits()` deletes oldest games (lowest ID) when limits are reached.

### NVS (Non-Volatile Storage)

NVS stores settings that survive firmware updates and power cycles. Organized by namespace:

| Namespace | Keys | Purpose |
|-----------|------|---------|
| `ledSettings` | `brightness`, `dimMult` | LED brightness (0–255) and dark square dimming (20–100%) |
| `boardCal` | `ver`, `rowPins`, `srPins`, `row`, `col`, `led`, `swap` | Calibration version, pin config verification, logical mapping arrays, axis swap flag |
| `wifiNets` | `count`, `ssid0`–`ssid2`, `pass0`–`pass2` | Up to 3 saved WiFi networks |
| `lichess` | `token` | Lichess API token |
| `ota` | `passHash`, `salt` | OTA password (salted SHA-256 hash) |

All NVS access uses Arduino's `Preferences` library. `SystemUtils::ensureNvsInitialized()` must be called before any NVS operation — it initializes the NVS partition if needed.

### NTP Time Sync

`configTime(0, 0, "pool.ntp.org", "time.nist.gov")` is called once in `setup()`. The call is non-blocking — NTP resolves in the background over WiFi. `LittleFSStorage::getTimestamp()` returns the current Unix epoch, or 0 if NTP hasn't synced yet. Timestamps are stored in game headers for the web UI's game history display.

## Security

### WiFi Access Point Lifecycle

The AP follows strict lifecycle rules to ensure the board is always accessible for configuration:

1. **Boot**: AP starts immediately (SSID `LibreChess`, password `chess123`, IP `192.168.4.1`)
2. **STA connection established**: a 10-second stabilization timer starts
3. **Timer expires with stable STA**: AP shuts down (callback `apStabilizationCallback` calls `disableAP()`)
4. **STA disconnect**: AP re-enables immediately via `enableAP()` in the WiFi event handler
5. **No saved networks**: AP remains permanently active

The stabilization window prevents the AP from flapping on/off with unstable WiFi connections. The timer is a FreeRTOS software timer (`apStabilizationTimer`), canceled if the STA disconnects before it fires.

### OTA Password Protection

Firmware uploads via the web UI can be protected with an optional password:

- Password is stored as a **salted SHA-256 hash** in NVS namespace `"ota"` (never plaintext)
- Salt: 16 random bytes generated via `esp_random()`, stored as hex string
- Hashing: `mbedtls_sha256` (bundled with ESP-IDF, no external crypto dependencies)
- The web UI sends the password in an `X-OTA-Password` HTTP header
- `verifyOtaPassword()` re-hashes the provided password with the stored salt and compares
- Firmware validation: the upload handler checks for the ESP32 magic byte (`0xE9`) at offset 0 before accepting
- Management: `POST /ota/password` to set/change/remove, `GET /ota/status` to query if a password is set

The web UI separates password management (Security section on settings page) from firmware upload (OTA Update section).

### TLS for External Connections

Outbound HTTPS connections to Lichess and Stockfish use `WiFiClientSecure` with `setInsecure()`. TLS encryption is enabled, but certificate pinning is not performed. This trades certificate validation for reliability across different ESP32 SDK versions and certificate store limitations.

### Factory Reset

Adding `-DFACTORY_RESET` to `build_flags` in `platformio.ini` triggers a full NVS erase on next boot:
1. `nvs_flash_erase()` — wipes the entire NVS partition
2. `nvs_flash_init()` — reinitializes the partition
3. All settings are cleared: WiFi credentials, Lichess token, OTA password, calibration data, LED brightness

This is a compile-time flag — not exposed via the web UI. Remove the flag after flashing to resume normal operation. Requires physical USB access.

### Input Validation

API handlers validate all parameters at system boundaries. Internal functions trust data that has already passed validation. Sensitive data is never exposed in API responses — WiFi passwords, Lichess tokens (only masked), and OTA password hashes are excluded from all JSON responses.

## Frontend Architecture

### API Layer

The frontend uses a two-file pattern for server communication:

- **`api.js`** — low-level utilities: `getApi(url)`, `postApi(url, body)`, `deleteApi(url)` fetch wrappers with error handling, and `pollHealth(timeoutMs)` for post-OTA reboot polling (polls `GET /health` until the board responds after a reboot).
- **`provider.js`** — domain-specific `Api` object with a named method for every backend endpoint. Examples: `Api.getNetworks()`, `Api.resign()`, `Api.selectGame(mode, config)`, `Api.getBoardUpdate()`, `Api.getOtaStatus()`, `Api.setOtaPassword(pass)`, `Api.getGames()`, `Api.deleteGame(id)`.

All HTML pages include both scripts via `<script>` tags in `<head>`. All server communication goes through `Api.*` methods — no page contains raw `fetch()` calls. When adding a new API endpoint, add a corresponding method to `provider.js`.

### Pages

| Page | File | Purpose |
|------|------|---------|
| Settings | `index.html` | WiFi management, Lichess token, LED settings, calibration trigger, OTA upload, security |
| Board | `board.html` | Live board view, evaluation bar, move history, board editor, game history browser, review mode, resign button |
| Game Selection | `game.html` | Game mode cards with bot configuration panel. Redirects to board page after selection. |

### Web Asset Pipeline

Source files live in `src/web/`. Three build scripts (defined in `platformio.ini`) process them:

1. `minify.py` — minifies HTML/CSS/JS using `html-minifier-terser`, `clean-css-cli`, `terser`. Skips gracefully if npm tools aren't installed.
2. `prepare_littlefs.py` — gzip-compresses output into `data/` with `.gz` extensions. Files with `.nogz.` in their name are copied uncompressed. Deletes intermediate minified files afterward.
3. `upload_fs.py` — on `pio run -t upload`, hashes `data/` contents and compares with `.littlefs_hash`. Only re-uploads the filesystem image when assets have changed.

The `data/` directory is committed to git so users without npm tools can still build. `.littlefs_hash` is git-ignored. Always edit source files in `src/web/`, never in `data/`.

## Performance Notes

| Technique | Impact |
|-----------|--------|
| I2S DMA for LEDs | CPU-free LED signal generation, avoids WiFi interrupt conflicts |
| Shift register column scanning | 64 sensors via 11 GPIOs with efficient sequential shifting |
| Zobrist hashing (`constexpr` + PROGMEM) | O(1) position hashing, ~6.2KB flash not RAM, keys generated at compile time |
| Binary move encoding | 2 bytes/move vs 5 for text notation strings, more games per flash |
| Gzip web assets | Smaller flash footprint and faster browser transfers |
| `constexpr` menu arrays | Menu data in flash, zero RAM cost |
| Fixed-size arrays/stacks | No heap fragmentation from dynamic containers |
| FEN snapshots in game files | Bounded replay time regardless of game length |
| FreeRTOS animation task | Non-blocking LED rendering during sensor/network operations |
| Conditional FS upload (hash) | Skip redundant filesystem uploads on firmware-only changes |

## Utilities

**`piece`** (`piece.h`) — header-only namespace with piece-level functions. All `constexpr` unless noted:
- `pieceType(piece)` → `PieceType` — extract piece type from `Piece` enum
- `pieceColor(piece)` → `Color` — extract color
- `makePiece(color, type)` → `Piece` — construct piece
- `isEmpty(piece)` — piece predicate
- `~color` / `~piece` — opponent color / opponent-colored piece (operator overloads)
- `pawnForward(color)`, `homeRank(color)`, `promotionRank(color)`, `pawnStartRank(color)` — LERF-native color-derived constants
- `colorName(color)` — `"White"` / `"Black"` (inline, not constexpr)
- `charToPiece(char)` / `pieceToChar(piece)` — FEN char ↔ `Piece` conversion
- `charToPieceType(char)` / `pieceTypeToChar(type)` — FEN char ↔ `PieceType` conversion
- `pieceIndex(Piece)` / `pieceIndex(char)` — unified 0–11 piece indexing; `pieceIndex(Piece)` decomposes via `pieceColor`+`pieceType`, `pieceIndex(char)` accepts FEN chars (e.g. `'K'`→5, `'k'`→11); returns `PIECE_IDX_NONE` (-1) for `Piece::NONE` / invalid chars; `isValidPieceIndex()` bounds predicate

**`eval`** (`evaluation.h/cpp`) — namespace with tapered position evaluation:
- `evaluatePosition(bb)` — tapered score in centipawns (MG/EG PSTs + pawn structure + positional terms: bishop pair, rook files, rook on 7th, mobility, king safety, knight outposts; interpolated by game phase)

**`attacks`** (`attacks.h/cpp`) — namespace with attack tables, slider functions, and geometry helpers:
- `KNIGHT[64]`, `KING[64]`, `PAWN[2][64]` — precomputed leaper tables (~2.5 KiB), const (computed at compile time via constexpr builders, placed in .rodata). `init()` is an inline no-op.
- `rook(sq, occ)`, `bishop(sq, occ)`, `queen(sq, occ)` — O(1) slider attacks via first-rank lookup table (rank) + Hyperbola Quintessence (file/diagonal/anti-diagonal). Diagonal masks (`DIAG[15]`, `ANTI_DIAG[15]`) indexed by diagonal number, `static constexpr` in attacks.cpp (~240B, placed in .rodata)
- `xrayRook(occ, friendly, sq)`, `xrayBishop(occ, friendly, sq)` — pin detection (used by `movegen`/`rules`)
- `between(s1, s2)` — squares strictly between two colinear squares (used by `movegen`/`rules` for check masks)
- `computeAll(bb)` → `AttackInfo` — per-piece-type and per-color attack maps from scratch. Used by evaluation (mobility calculation) and available for search (move ordering)
- `isSquareUnderAttack(bb, sq, color)` — per-piece-type attack table lookups + slider functions for check detection
- `see(bb, mailbox, move)` — Static Exchange Evaluation via swap algorithm: builds a gain list by alternating least-valuable-attacker captures, then walks back with negamax. Handles en passant. Used by search for quiescence pruning and move ordering (demoting losing captures)

**`utils`** (`utils.h`, header-only) — namespace with stateless board-level helper functions:
- `isValidPromotionChar(c)` — case-insensitive promotion piece validation
- `fileChar()`, `rankChar()`, `fileIndex()`, `rankIndex()` — coordinate helpers
- `castlingRightsToString()` / `castlingRightsFromString()` — FEN castling field
- `hasCastlingRight()` — castling rights query via `BIT[2][2]` lookup table indexed by `[raw(color)][kingSide]`
- `castlingCharToBit()` — FEN character (K/Q/k/q) → bitmask (switch-based)
- `updateCastlingRights()` — lookup-table approach: 64-entry `CASTLING_MASK[sq]` indexed by LERF square, two ANDs replace 8 if-statements; square-based API
- `checkEnPassant(mailbox, from, to)` — detect EP capture / double-push EP target from mailbox + squares; shared by `Position` member methods and `movegen`
- `checkCastling(mailbox, from, to)` — detect castling and rook source/dest columns from mailbox + squares; shared by `Position` member methods and `movegen`
- `resolveKingSquare(bb, color, kingSq)` — inline king-square finder via `pieceIndex` + `lsb`; used by movegen and search to avoid repeating the 3-line king bitboard lookup pattern
- `roundDownPow2(n)` — round down to nearest power of two; shared by TT, PawnHashTable, and EvalHashTable sizing
- `forEachSquare(mailbox, fn)` — call `fn(row, col, piece)` for all 64 squares
- `forEachPiece(bb, mailbox, fn)` — call `fn(row, col, piece)` for occupied squares only via bitboard serialization

**`fen`** (`fen.h/cpp`) — namespace with FEN string handling:
- `boardToFEN(board, turn, state)` / `fenToBoard(fen, board, turn, state)` — FEN ↔ board array conversion with full state restoration (castling rights, en passant, clocks) via `PositionState*`
- `validateFEN(fen)` — format validation: rank structure, piece chars, turn, castling, en passant, clocks

**`search`** (`search.h/cpp`) — namespace with on-board chess search engine:
- `findBestMove(pos, limits, state, info)` — iterative-deepening negamax with alpha-beta pruning, quiescence search (MVV-LVA ordered, pawn-defended-pawn pruning), transposition table, reverse futility pruning, improving flag, logarithmic LMR table, history pruning, and move ordering (TT move → MVV-LVA → killers → countermove → history → bad captures). Infrastructure fields (`timeFunc`, `tt`, `pawnHash`, `evalHash`) set on `SearchState` by caller. Returns `SearchResult` (bestMove, score, depth, nodes)
- `SearchLimits` — search constraints: `maxDepth`, `softTimeMs` (stop after current iteration), `hardTimeMs` (abort mid-search), `stop` (external cancellation flag)
- `SearchState` — required caller-owned state: killer moves, history table, countermove table, staticEvals (for improving flag), node counter, infrastructure pointers (`timeFunc`, `tt`, `pawnHash`, `evalHash`)
- `TranspositionTable` — hash table of `TTEntry` (12 bytes: key32 + score + packed move + depth + flag). `resize()`, `clear()`, `probe()`, `store()`
- Constants: `MATE_SCORE=30000`, `INF_SCORE=31000`, `MAX_PLY=48`, `DEFAULT_TT_SIZE=8192` (ESP32, `HARDWARE_LIMITATION`), `DEFAULT_TT_SIZE=131072` (native)

**`uci`** (`uci.h/cpp`) — namespace with UCI protocol handler:
- `UCIStream` — abstract line-based I/O interface
- `UCIHandler` — engine controller: owns `Position`, `TranspositionTable`, stop flag. `loop(stream)` for blocking mode, `processCommand(line, out)` for single-command dispatch. Supports `setExternalStop()` for wiring FreeRTOS cancellation flags
- `StringUCIStream` — in-memory I/O for testing and in-process use (`addInput()`, `output()`, `clearOutput()`)

**`grid_scan_test.cpp`** — standalone hardware debugging utility at the repo root (not compiled in normal builds). Tests shift register column scanning and row GPIO reads. Useful for verifying sensor wiring before running the full firmware.
