---
description: "Use when writing, modifying, or debugging unit tests. Covers test architecture, file mirroring convention, test helpers, and per-file test group descriptions."
applyTo: "test/**"
---

# Unit Testing Guide

## Architecture

Tests run natively on the host (no ESP32) using PlatformIO Unity framework with `[env:native]`.

The Arduino-free libraries (`lib/core/`, `lib/game/`) compile natively. Tests include library headers directly.

## Running Tests

Two build environments: `[env:native]` for all tests except statistics, `[env:native_stats]` (adds `-DSTATS`) for search statistics instrumentation.

| Action | Command |
|--------|---------|
| Run all tests | `pio test -e native -e native_stats` |
| Run lib tests (core+game) | `pio test -e native -f test_core -f test_game` |
| Run position tests | `pio test -e native -f test_positions_time -f test_positions_depth` |
| Run benchmarks + statistics | `pio test -e native -f test_benchmarks` then `pio test -e native_stats` |
| Run core suite | `pio test -e native -f test_core` |
| Run game suite | `pio test -e native -f test_game` |
| Run board suite | `pio test -e native -f test_board` |
| Run perft suite | `pio test -e native -f test_perft` |
| Run benchmarks | `pio test -e native -f test_benchmarks` |
| Run statistics | `pio test -e native_stats` |

## File Structure

Tests are split into suites mirroring the library structure (`lib/core/`, `lib/game/`), plus independent board primitive, position, benchmark, statistics, and perft suites. Each suite compiles into its own binary. Shared globals live in `test_shared.cpp` at the test root (compiled into every suite).

```
test/
├── test_helpers.h                       Shared utilities (setupInitialBoard, clearBoard, placePiece, etc.)
├── test_shared.cpp                      Shared globals (bb, mailbox, needsDefaultKings)
├── test_core/                           Core library tests (lib/core/)
│   ├── test_all.cpp                    Main entry: setUp/tearDown, register calls
│   ├── test_attacks.cpp                 attacks: leaper tables, slider rays, x-ray attacks, between geometry, computeAll, SEE, isSquareUnderAttackOcc
│   ├── test_bitboard.cpp                LibreChess: square mapping, bit ops, square-color masks, BitboardSet mutations
│   ├── test_epd.cpp                     EPD parser: parseEPDLine (bm/am/id/c0/c9, quoted/comma-separated), validateEPDLine, strict FEN fields, cap rejection, accessors
│   ├── test_evaluation.cpp              eval: material scoring, pawn structure, tapered evaluation, pawn/eval hash tables, hash allocation status, trapped pieces, bad bishop, rook behind passer, OCB scaling
│   ├── test_eval_regression.cpp        eval regression: 17 fixed-position score assertions (symmetry, material, pawn structure, threats, phase tapering, bad bishop, rook behind passer, OCB scaling)
│   ├── test_fen.cpp                     FEN round-trip, boardToFEN/fenToBoard, validateFEN, clock range/no-wrap checks
│   ├── test_movegen.cpp                 Move generation per piece type, captures, bulk generation, staged generation, move flags, legal move queries
│   ├── test_notation.cpp                Coordinate/SAN/LAN output and parsing, roundtrip verification
│   ├── test_piece.cpp                   piece: type extraction, construction, predicates, FEN chars, material values, Zobrist index, color helpers
│   ├── test_position.cpp                Position: moves, special moves, draws, FEN, reverseMove, king cache, MoveList, HashHistory, check/checkmate/stalemate detection, pin-aware generation, castling, en passant, promotion, isDraw, isGameOver
│   ├── test_utils.cpp                   utils: 50-move rule, castling rights strings, coordinate helpers, board transforms, special-move analysis (via Position), resolveKingSquare, forEachSquare, forEachPiece
│   ├── test_zobrist.cpp                 Zobrist hashing: key determinism, computeHash, computePawnHash, position sensitivity
│   ├── test_search.cpp                  search: mate-in-1, captures, quiescence, stalemate avoidance, iterative deepening, IID, time/stop, depth clamp, TT, move ordering, delta pruning, futility pruning, SEE ordering, lazy eval, PV table, MDP, capture history, staged MovePicker, TT replacement, soft time, easy move, instability time extension
│   ├── test_uci.cpp                     UCI protocol: uci command, isready, go depth, bounded clocks, position/fen, newgame, info output, quit, mate score, hash diagnostics, setoption Hash, go movetime
│   └── test_book.cpp                    Opening book: entry count, probe hits/misses, variety, hash correctness, findBestMove integration
├── test_game/                           Game library tests (lib/game/)
│   ├── test_all.cpp                    Main entry: setUp/tearDown, register calls
│   ├── test_game.cpp                    Game: lifecycle, draws, observer, history, undo/redo, getHistory
│   ├── test_history.cpp                 History: move log with undo/redo, branch-on-undo, compact encode/decode
│   └── test_history_persistence.cpp     Recording: persistence, header flush, replay, branch-truncation, encode/decode
├── test_board/                          Arduino-free board primitive tests
│   ├── test_all.cpp                    Main entry: includes pure board source units once and registers board tests
│   ├── test_canvas.cpp                  BoardCanvas: ordered-surface composition, dirty flag, bounds, rect/fill/line/ring helpers
│   ├── test_input.cpp                   BoardInput: baseline sync, edge events, queue consumption, overflow metrics, bounds
│   └── test_effects.cpp                 BoardScheduler + BoardAnimations: slot lifecycle, stale handles, sibling composition, looping/finite animations
├── suites/                              Shared EPD test files (no .cpp — not compiled)
│   ├── wac.epd                          Win At Chess — 300 positions (Reinfeld/Wilson, CPW verbatim)
│   ├── bk.epd                           Bratko-Kopec — 24 positions (Bratko/Kopec, CPW verbatim)
│   └── eret.epd                         Eigenmann Rapid Engine Test — 111 positions (Eigenmann, CPW verbatim)
├── test_positions_time/                 Time-based position test suites (standalone, heavyweight)
│   └── test_positions_time.cpp          Suite runner: loads .epd files from ../suites/ via EPD parser, SAN→coordinate comparison, 500ms/position, informational pass rates
├── test_positions_depth/                Depth-based position test (standalone, deterministic)
│   └── test_positions_depth.cpp         WAC 300 at fixed depth 10 — hard assert on solve count vs calibrated baseline
├── test_benchmarks/                     Performance benchmarks + regression tests
│   ├── test_all.cpp                    Main entry: setUp/tearDown, register calls
│   ├── test_timing.cpp                  Micro-benchmarks: make/unmake timing, EP make/unmake, evaluatePosition (single + multi-position), bishop() attacks, perft(5) Mnps, search depth-8 (single + multi-position) knps, generateMoves throughput (calls/s)
│   └── test_regression.cpp              Node count regression (10 positions × depth 10, 15% threshold) + eval regression (15 positions, exact match)
├── test_statistics/                     Search statistics diagnostic (standalone)
│   └── test_statistics.cpp              Runs 5 positions at depth 10, prints TT hit rates, cutoff ratios, pruning/extension counts, QS/main node ratios. Requires -DSTATS.
└── test_perft/                          Perft suite (standalone, heavyweight)
    └── test_perft.cpp                   Perft move-tree enumeration with detailed counters (captures, EP, castles, promotions, checks, checkmates)
```

## File Mirroring Convention

Each library source file has a corresponding test file in the matching test suite:

| Source | Test Suite | Test File |
|--------|-----------|-----------|
| `lib/core/src/position.cpp` | `test_core/` | `test_position.cpp` |
| `lib/core/src/movegen.cpp` | `test_core/` | `test_movegen.cpp` |
| `lib/core/src/piece.h` | `test_core/` | `test_piece.cpp` |
| `lib/core/src/utils.h` | `test_core/` | `test_utils.cpp` |
| `lib/core/src/evaluation.cpp` | `test_core/` | `test_evaluation.cpp` |
| `lib/core/src/fen.cpp` | `test_core/` | `test_fen.cpp` |
| `lib/core/src/notation.cpp` | `test_core/` | `test_notation.cpp` |
| `lib/core/src/bitboard.h` | `test_core/` | `test_bitboard.cpp` |
| `lib/core/src/attacks.h/cpp` | `test_core/` | `test_attacks.cpp` |
| `lib/core/src/zobrist.h/cpp` | `test_core/` | `test_zobrist.cpp` |
| `lib/game/src/game.cpp` | `test_game/` | `test_game.cpp` |
| `lib/game/src/history.cpp` | `test_game/` | `test_history.cpp` + `test_history_persistence.cpp` |
| `lib/core/src/search.h/cpp` | `test_core/` | `test_search.cpp` |
| `lib/core/src/uci.h/cpp` | `test_core/` | `test_uci.cpp` |
| `lib/core/src/epd.h/cpp` | `test_core/` | `test_epd.cpp` |
| `lib/core/src/book.h/cpp` | `test_core/` | `test_book.cpp` |
Place tests in the suite that mirrors the owning library. When creating a new source file in a native library, create a matching test file in the corresponding `test_<lib>/` directory and register its test functions in that suite's main file.

## Test Helpers (`test_helpers.h`)

Shared utilities available to all test files:
- `setupInitialBoard(bb, mailbox)` — sets up standard starting position in `BitboardSet` + `Piece mailbox[64]`
- `clearBoard(bb, mailbox)` — empties the bitboard set and mailbox
- `placePiece(bb, mailbox, row, col, piece)` — places a piece at specific coordinates in both representations (uses `squareOf(row, col)` internally)

## Testing Principles

- **Tests guard correctness** — never modify a test to make it pass. If a test fails, fix the production code.
- **Tests must stay in sync** — when changing chess logic in `lib/core/`, update or add tests in the same change. New public APIs, new structs, renamed parameters, moved functions, and new internal state (caches, derived fields) all need test coverage. This includes:
  - Struct behavior tests for new data types (e.g. `MoveList`, `HashHistory`)
  - State maintenance tests for derived/cached fields (e.g. king cache across `makeMove`/`reverseMove`/`loadFEN`)
  - Updating existing tests when signatures or patterns change
- **Test group descriptions must stay current** — when adding new test sections, update the Test Group Details and file structure listing in this file.
- **Always test changes** — every logic change must be validated by running the test suite before committing.
- **Always run regression tests** — any change to search, evaluation, or move generation must be validated with the regression test suite (`test_benchmarks`) to verify node counts and eval scores remain within tolerance. This catches unintended behavioral changes that unit tests may miss.

## Test Group Details

### Movegen (`test_movegen.cpp`)
Move generation for each piece type (pawn, knight, bishop, rook, queen, king). Blocked paths, captures, initial pawn double-push, edge of board. Starting position move counts. Legal move queries (`hasAnyLegalMove`). Bulk move generation: `Move`/`MoveList` struct behavior, `generateMoves(filter=ALL)` (initial position count, under check evasions, double check king-only, stalemate zero moves, consistency with per-piece `getPossibleMoves`), `generateMoves(filter=CAPTURES_PROMOS)` (capture-only filtering, EP included, no quiet moves), move flag correctness (capture, EP, castling, promotion with all 4 piece types), promotion index round-trip. Staged move generation: `buildLegalityContext` + `generateMoves(ctx, CAPTURES_PROMOS)` + `generateMoves(ctx, QUIETS)` produces same moves as bulk `generateMoves(ALL)` (initial position, complex middlegame, under check). `FilterMode` enum controls generation (`ALL`, `CAPTURES_PROMOS`, `QUIETS`). File-local `filterPieceMoves()` template centralizes per-piece legality filtering for both `enumerateLegalMoves` and `getPossibleMoves`.

### Position (`test_position.cpp`)
New game state. Basic moves and captures. En passant execution. Castling execution (both sides). Promotion with piece selection. Check and checkmate detection. Stalemate. 50-move draw. Insufficient material (K vs K, K+B vs K, K+N vs K, K+B vs K+B same-color). Threefold repetition via Zobrist. FEN loading and validation. Compact encode/decode. Board API queries. `reverseMove()` for normal/capture/en passant/castling/promotion. `applyMoveEntry()` replay. King cache (`kingSq`) correctness after `newGame()`, `loadFEN()`, `makeMove()` (king/non-king), castling, and `reverseMove()`. `MoveList` struct (initial state, add/access, clear, capacity, integration with `getPossibleMoves`). `HashHistory` struct (initial state, add/read, MAX_SIZE constant, sliding-window overflow). Halfmove clock increment/reset/saturation. Incremental material tracking: `material()` matches `eval::computeMaterial()` at startpos, after capture, EP capture, promotion, make/unmake sequences, null move, and loadFEN. Position static methods for game-end detection: check detection from every piece type, checkmate/stalemate positions, move legality when king is in check (blocking, capturing, king escape), pin-aware generation (pinned piece restrictions, diagonal pin, double check king-only, single-check slider blocking, knight check no blocking, two-friendly shielding, EP horizontal pin), idempotency, `isValidMove`, `isDraw`, `isGameOver`, threefold repetition, castling (rights preservation, blocking, through-check prevention), en passant (standard capture, edge cases, `hasLegalEnPassantCapture` boundaries), promotion (all piece types), special-move helpers (`checkEnPassant`, `checkCastling`).

### Game (`test_game.cpp`)
Lifecycle: `endGame()` with various results, `isGameOver()` state, move rejection after game-over, checkmate/stalemate/insufficient/fifty-move auto-set game-over, undo clears game-over, `newGame()`/`loadFEN()` reset game-over. Draw detection. Observer notification and batching. History integration. Undo/redo cursor. `getHistory()` in all three formats (coordinate, SAN, LAN). Search facade coverage verifies `calculateMove()` returns an applicable move from reported bot-game positions without mutating the live FEN before the move is applied, plus search initialization/hash diagnostics.

### History (`test_history.cpp`)
Move log add/undo/redo. Cursor positioning. Branch-on-undo: adding a move at a branch point wipes future moves.

### History Persistence (`test_history_persistence.cpp`)
Persistence lifecycle with MockGameStorage. Header flush timing. Game replay from storage. Branch-truncation of storage. Game recording integration: startNewGame, endGame, resume, auto-finish. Compact 2-byte encode/decode stability. On-disk format compatibility.

### Utils (`test_utils.cpp`)
50-move rule counter. Castling rights string formatting and parsing (`castlingCharToBit`, `hasCastlingRight`). Coordinate helpers (`squareName`, `fileChar`, `rankChar`, `fileIndex`, `rankIndex`). Special-move analysis via `Position` member methods (`checkEnPassant`, `checkCastling` — tests load FEN into a `Position` then call the member method). `updateCastlingRights`. `boardToText` (via `Position::boardToText()`). `positionState`. `gameResultName` (from `types.h`). `isValidPromotionChar`. `resolveKingSquare` (initial position, no king, king-only). Board iteration helpers: `utils::forEachSquare` (visits all 64), `utils::forEachPiece` (skips empty).

### Evaluation (`test_evaluation.cpp`)
Material evaluation scoring. Pawn structure evaluation (symmetry, passed pawn bonus, doubled/isolated penalties). Tapered evaluation (opening symmetry, endgame king centralization, phase-dependent king PST blend). Pawn-structure analysis functions: `isPassed`, `isIsolated`, `isDoubled`. Positional terms: bishop pair bonus (single side / both sides), rook on open file, rook on semi-open file, rook on 7th rank, mobility (centralized vs edge pieces, MG/EG split weights), king safety (intact pawn shield), king danger (close piece scores higher, multiple zone attackers vs no attackers), knight outpost. Passed pawn rank scaling (advanced passer scores higher). Trapped pieces: bishop trapped on a7/h7 by enemy pawn, rook trapped by own uncastled king, symmetry. Pawn hash table: probe miss, store/probe round-trip, clear invalidation, integration with evaluatePosition. Eval hash table: probe miss, store/probe round-trip, clear invalidation, overwrite. Bad bishop (penalty per own pawn on same color complex). Rook behind passer (Tarrasch Rule, EG-only bonus for rook behind own passer vs rook in front). Opposite-color bishop scaling (×0.75 reduction in endgame vs same-color bishops).

### Search (`test_search.cpp`)
Mate-in-1 (white, black). Captures hanging piece. Quiescence avoids blunder. Stalemate avoidance. Symmetric position. Knight fork tactics. Legal move from random position. Checkmate no legal moves. Iterative deepening (deeper depth finds mate, info callback reports iterations). IID preserves tactics, completes with TT. Lazy evaluation preserves tactics, balanced position correctness, imbalanced position scoring. Depth clamp for oversized external limits. Time limit control. Stop flag. Mate stops early. TT store/probe exact, probe miss, clear, pack/unpack move, reduces nodes, mate score round-trip. Check extension finds mate. NMP quiet position, K+P endgame no blunder. PVS+LMR middlegame efficiency. Pruning preserves tactics. Aspiration windows correctness, depth continuity. Root move reordering consistency. Move ordering reduces nodes and finds tactics. Delta pruning quiet position. Futility pruning preserves winning capture and discovered attack. SEE ordering preserves tactics. SEE qsearch skips losing capture. LMP preserves winning capture, completes without blunder. Razoring preserves winning capture, completes correctly. Countermove heuristic preserves tactics, integrates with TT. History gravity produces negative scores for non-cutoff quiets. Recapture extension finds exchange sequence. Adaptive NMP correctness (deeper search doesn't blunder). Singular extension preserves tactics, completes with captures. PV table accuracy (mate-in-2 PV length and consistency). Mate distance pruning (mate score > winning material). Capture history ordering (tactical position node efficiency). Staged MovePicker (mate via capture found without quiet generation). TT depth-preferred replacement (deep entry survives shallow collision). Soft time stops search (between iterations with timer). Easy move early exit (mate-in-1 terminates quickly). Instability time extension (complex middlegame extends search time when best move changes frequently).

### UCI (`test_uci.cpp`)
UCI protocol: uci command, isready, go depth, bounded negative/oversized clock parsing, zero-clock time-management bounds, position with moves, position fen, ucinewgame, info output, quit, mate score, Engine hash-table readiness diagnostics, setoption Hash, go movetime. Tests via `processLine()` — pure string-in/string-out, no I/O.

### FEN (`test_fen.cpp`)
Round-trip: board → FEN → board. `boardToFEN()` output correctness. `fenToBoard()` parsing. `validateFEN()`: valid positions, invalid rank structure, bad piece chars, wrong turn field, invalid castling, bad en passant, bad clocks, halfmove/fullmove storage bounds, oversized clocks do not wrap in lenient parsing.

### Notation (`test_notation.cpp`)
Coordinate notation output and parsing. SAN output (disambiguation, captures, castling, promotion). LAN output. Auto-format detection from input strings. Round-trip verification: format → parse → same move.

### Piece (`test_piece.cpp`)
piece namespace: bit extraction (`pieceType()` for all 12 pieces + NONE, `pieceColor()`), construction (`makePiece()` round-trips), predicates (`isEmpty()`), color flip (`~Color`, `~Piece` operator overloads), FEN char round-trip (`charToPiece()`/`pieceToChar()` for all valid chars, invalid chars, NONE), PieceType char conversion, piece indexing (`pieceIndex(Piece)` for all pieces + NONE, `pieceIndex(char)` for FEN chars + invalid, `PIECE_IDX_NONE` constant, `isValidPieceIndex()` bounds predicate, cross-overload consistency), `pieceIndex(Color, PieceType)` (BitboardSet indexing for each color+piece-type pair), LERF color helpers (`pawnForward()`, `homeRank()`, `promotionRank()`, `pawnStartRank()`, `colorName()`, `colorToChar()`, `getPieceColor`, `opponentColor`), test-only `charToColor()` (via test_helpers.h), zero-initialization safety.

### Zobrist (`test_zobrist.cpp`)
Zobrist key determinism. `pieceIndex` mapping. `computeHash` stability. Hash changes with turn flip, castling rights, en passant. Hash equality for identical positions. `computePawnHash`: determinism, different pawn placements produce different hashes, ignores non-pawn pieces, no pawns yields zero.

### Perft (`test_perft/test_perft.cpp`)
Exhaustive move-tree enumeration for 6 positions from the Chess Programming Wiki. Positions 1–4 verify detailed leaf-level counters: nodes, captures, en passant, castles, promotions, checks, and checkmates. Positions 5–6 verify node counts only (no wiki reference counters). Catches compensating bugs that node-only perft misses (e.g. a missed capture offset by a phantom quiet move).

### Bitboard (`test_bitboard.cpp`)
Square mapping roundtrip (`squareOf(rowOf(sq), fileOf(sq)) == sq` for all 64, using test-only helpers from `test_helpers.h`). LERF anchor values (`squareOf(0,0) == SQ_A8`, `squareOf(7,0) == SQ_A1`). Bit manipulation (`popcount`, `lsb`, `popLsb`). Square-color masks (a1 dark, b1 light, popcount 32 each, no overlap). `BitboardSet` mutations (`setPiece`/`removePiece`/`movePiece` consistency, aggregate bitboard correctness).

### Attacks (`test_attacks.cpp`)
Leaper attack tables (knight on e4, king on a1, pawn attacks per color). Slider attack functions (rook/bishop/queen on empty board and with blockers). Bulk slider correctness (all 64 squares × 5 occupancy patterns cross-checked against reference ray implementation for both rook and bishop). X-ray attack functions (`xrayRook`, `xrayBishop`). `between` geometry (file/rank/diagonal/anti-diagonal/adjacent/non-colinear). `computeAll` validation (initial knight attacks, pawn bulk attacks, color unions, kings-only board). SEE: pawn takes undefended knight, pawn takes defended rook, knight takes defended pawn (losing), queen takes defended pawn (losing), rook takes undefended bishop, en passant. Occupancy-aware attack detection (`isSquareUnderAttackOcc`): slider through custom occupancy (blocker removal reveals attack), leaper unaffected by occupancy, king removal from occupancy reveals slider attack.

### EPD Parser (`test_epd.cpp`)
`parseEPDLine`: basic FEN + bm opcode, multiple opcodes (bm + am + id + c0), quoted id strings, avoid move variations, comma-separated operands, c9 game result parsing (white win/draw/black win), over-cap input returns empty. `validateEPDLine`: valid/invalid FEN, missing fields, invalid rank sums, too many operands, too many operations. Accessors: `findOperation()` lookup, `id()` convenience, non-existent opcode returns nullptr yet `id()` returns empty.

### Opening Book (`test_book.cpp`)
Entry count validation (>350, <700). Probe hits for starting position, after 1.e4, after 1.d4. Probe miss for middlegame position. Variety: ≥3 distinct first moves from startpos across different PRNG seeds. Hash correctness: probe hits for 4 known opening positions (startpos, 1.e4 e5 2.Nf3 Nc6, 1.d4 Nf6 2.c4 e6, 1.e4 c5). `findBestMove` integration: returns depth 0 / nodes 0 with book enabled; searches normally when book disabled.

### Tactical Suites (`test_positions_time/test_positions_time.cpp`)
Engine accuracy benchmarks using standard `.epd` files loaded from `../suites/` at runtime via the EPD parser. Each suite reads its `.epd` file, parses each line into an `EPDRecord`, runs `search::findBestMove` with a fixed time budget (500ms/position), converts both expected (SAN) and engine (Move) results to coordinate notation, and compares. Shared `TranspositionTable`, `PawnHashTable`, and `EvalHashTable` are allocated once at startup and cleared between suites. Suites: **WAC** (Win At Chess, 300 positions), **BK** (Bratko-Kopec, 24 positions), **ERET** (Eigenmann Rapid Engine Test, 111 positions). Tests are informational — they assert only that at least one position is solved, printing individual mismatches and overall pass rate.

### Depth-Based Positions (`test_positions_depth/test_positions_depth.cpp`)
Deterministic, machine-independent strength gate. Runs all 300 WAC positions at fixed depth 10 (no time limit), counts how many the engine solves, and asserts the count is ≥ a calibrated baseline (`WAC_DEPTH_BASELINE`). Used to detect search quality regressions before and after optimization changes.

### Performance Benchmarks (`test_benchmarks/test_timing.cpp`)
Micro-benchmark suite for hot-path timing baselines. Nine tests measure `std::chrono::steady_clock` timing: make/unmake round-trip (no EP), make/unmake round-trip (EP position), `evaluatePosition` call (single position), multi-position `evaluatePosition` (5 positions, averaged), `bishop()` attack generation (500K calls with varied sq+occupancy), perft(5) throughput in Mnps, single-position search at depth 8, multi-position search (3 positions, averaged) reporting knps/nodes/score, and `generateMoves` throughput (5 diverse positions × 100K iterations, calls/s). Tests are informational (always PASS) — they print ns/op or throughput metrics for regression tracking. No assertions on timing values.

### Regression Tests (`test_benchmarks/test_regression.cpp`)
Node count and evaluation regression tests. Node count: 10 diverse positions searched at depth 10, total nodes compared against a calibrated baseline with 15% tolerance. Eval: 15 positions, static evaluation compared for exact match against calibrated values. Used to detect unintended changes to search behavior or evaluation.

### Search Statistics (`test_statistics/test_statistics.cpp`)
Diagnostic search statistics suite. Runs 5 positions at depth 10 with `search::resetStats()`/`search::getStats()` (requires `-DSTATS` build flag, set in `[env:native]`). Prints formatted tables: TT probe/hit rates, exact/lower/upper cutoff counts, beta cutoff quality (first-move cutoff %), pruning counts (NMP, futility, LMP, history, razoring, RFP), LMR search/re-search ratios, extension counts (check, singular, recapture), PVS re-search count, QS/main node distribution. Informational only — no hard assertions.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Core library (incl. search/UCI) tested in `test_core/` |
| `game-library.instructions.md` | Game library tested in `test_game/` |
| `search.instructions.md` | Search algorithm tested in `test_core/test_search.cpp` |
| `uci.instructions.md` | UCI protocol tested in `test_core/test_uci.cpp` |
| `epd.instructions.md` | EPD parser used by positional test suites (`test_positions_*`) |
