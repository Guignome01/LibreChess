---
description: "Use when writing, modifying, or debugging unit tests. Covers test architecture, file mirroring convention, test helpers, and per-file test group descriptions."
applyTo: "test/**, lib/core/**, lib/game/**, lib/engine/**"
---

# Unit Testing Guide

## Architecture

Tests run natively on the host (no ESP32) using PlatformIO Unity framework with `[env:native]`.

The chess libraries (`lib/core/`, `lib/game/`, `lib/engine/`) have zero Arduino dependencies — all chess logic compiles natively. Tests include library headers directly.

## Running Tests

| Action | Command |
|--------|---------|
| Run all tests | `pio test -e native` |
| Run core suite | `pio test -e native -f test_core` |
| Run game suite | `pio test -e native -f test_game` |
| Run engine suite | `pio test -e native -f test_engine` |
| Run perft suite | `pio test -e native -f test_perft` |
| Run tactics suite | `pio test -e native -f test_tactics` |

## File Structure

Tests are split into three suites mirroring the library structure (`lib/core/`, `lib/game/`, `lib/engine/`), plus an independent perft suite. Each suite compiles into its own binary. Shared globals live in `test_shared.cpp` at the test root (compiled into every suite).

```
test/
├── test_helpers.h                       Shared utilities (setupInitialBoard, clearBoard, placePiece, etc.)
├── test_shared.cpp                      Shared globals (bb, mailbox, needsDefaultKings)
├── test_core/                           Core library tests (lib/core/)
│   ├── test_all.cpp                    Main entry: setUp/tearDown, register calls
│   ├── test_attacks.cpp                 attacks: leaper tables, slider rays, x-ray attacks, geometry rays (between, line), computeAll, SEE
│   ├── test_bitboard.cpp                LibreChess: square mapping, bit ops, square-color masks, BitboardSet mutations
│   ├── test_epd.cpp                     EPD parser: parseEPDLine (bm/am/id/c0/c9, quoted/comma-separated), validateEPDLine, accessors
│   ├── test_evaluation.cpp              eval: material scoring, pawn structure, tapered evaluation, pawn/eval hash tables, trapped pieces, connectivity, bad bishop, rook behind passer, protected/candidate passer, OCB scaling
│   ├── test_eval_regression.cpp        eval regression: 17 fixed-position score assertions (symmetry, material, pawn structure, threats, phase tapering, bad bishop, rook behind passer, protected passer, candidate passer, OCB scaling)
│   ├── test_fen.cpp                     FEN round-trip, boardToFEN/fenToBoard, validateFEN
│   ├── test_iterator.cpp                Board iteration: forEachSquare, forEachPiece, somePiece, findPiece
│   ├── test_movegen.cpp                 Move generation per piece type, captures, bulk generation, move flags, legal move queries
│   ├── test_notation.cpp                Coordinate/SAN/LAN output and parsing, roundtrip verification
│   ├── test_piece.cpp                   piece: type extraction, construction, predicates, FEN chars, material values, Zobrist index, color helpers
│   ├── test_position.cpp                Position: moves, special moves, draws, FEN, reverseMove, king cache, MoveList, HashHistory
│   ├── test_rules.cpp                   rules: check/checkmate/stalemate detection, pin-aware generation, castling, en passant, promotion, isDraw, isGameOver
│   ├── test_utils.cpp                   utils: 50-move rule, castling rights strings, coordinate helpers, board transforms, special-move analysis
│   └── test_zobrist.cpp                 Zobrist hashing: key determinism, computeHash, computePawnHash, position sensitivity
├── test_game/                           Game library tests (lib/game/)
│   ├── test_all.cpp                    Main entry: setUp/tearDown, register calls
│   ├── test_game.cpp                    Game: lifecycle, draws, observer, history, undo/redo, getHistory
│   ├── test_history.cpp                 History: move log with undo/redo, branch-on-undo, compact encode/decode
│   └── test_history_persistence.cpp     Recording: persistence, header flush, replay, branch-truncation, encode/decode
├── test_engine/                         Engine library tests (lib/engine/)
│   ├── test_all.cpp                    Main entry: setUp/tearDown, register calls
│   ├── test_search.cpp                  search: mate-in-1, captures, quiescence, stalemate avoidance, iterative deepening, IID, time/stop, TT, move ordering, delta pruning, futility pruning, SEE ordering, lazy eval, PV table, MDP, capture history, staged MovePicker, TT replacement, soft time, easy move
│   └── test_engine.cpp                  Engine facade: calculateMove, depth control, stop/external stop, mate-in-1, TT persistence, score range
├── test_tactics/                        Tactical test suites (standalone, heavyweight)
│   ├── test_tactics.cpp                 Suite runner: loads .epd files via EPD parser, SAN→coordinate comparison, informational pass rates
│   ├── wac.epd                          Win At Chess — 300 positions (Reinfeld/Wilson, CPW verbatim)
│   ├── bk.epd                           Bratko-Kopec — 24 positions (Bratko/Kopec, CPW verbatim)
│   └── eret.epd                         Eigenmann Rapid Engine Test — 111 positions (Eigenmann, CPW verbatim)
├── test_benchmarks/                     Performance benchmarks (standalone)
│   ├── test_all.cpp                    Main entry: register calls for benchmark tests
│   └── test_nps.cpp                    NPS benchmark: 6 standard positions, 1s/pos, informational throughput measurement
└── test_perft/                          Perft suite (standalone, heavyweight)
    └── test_perft.cpp                   Perft move-tree enumeration with detailed counters (captures, EP, castles, promotions, checks, checkmates)
```

## File Mirroring Convention

Each library source file has a corresponding test file in the matching test suite:

| Source | Test Suite | Test File |
|--------|-----------|-----------|
| `lib/core/src/position.cpp` | `test_core/` | `test_position.cpp` |
| `lib/core/src/movegen.cpp` + `rules.cpp` | `test_core/` | `test_movegen.cpp` + `test_rules.cpp` |
| `lib/core/src/piece.h` | `test_core/` | `test_piece.cpp` |
| `lib/core/src/utils.cpp` | `test_core/` | `test_utils.cpp` |
| `lib/core/src/evaluation.cpp` | `test_core/` | `test_evaluation.cpp` |
| `lib/core/src/fen.cpp` | `test_core/` | `test_fen.cpp` |
| `lib/core/src/notation.cpp` | `test_core/` | `test_notation.cpp` |
| `lib/core/src/bitboard.h` | `test_core/` | `test_bitboard.cpp` |
| `lib/core/src/attacks.h/cpp` | `test_core/` | `test_attacks.cpp` |
| `lib/core/src/iterator.h` | `test_core/` | `test_iterator.cpp` |
| `lib/core/src/zobrist.h/cpp` | `test_core/` | `test_zobrist.cpp` |
| `lib/game/src/game.cpp` | `test_game/` | `test_game.cpp` |
| `lib/game/src/history.cpp` | `test_game/` | `test_history.cpp` + `test_history_persistence.cpp` |
| `lib/engine/src/search.h/cpp` | `test_engine/` | `test_search.cpp` |
| `lib/engine/src/engine.h/cpp` | `test_engine/` | `test_engine.cpp` |
| `lib/core/src/epd.h/cpp` | `test_core/` | `test_epd.cpp` |

Place tests in the suite that mirrors the owning library. When creating a new source file in any of the three libraries, create a matching test file in the corresponding `test_<lib>/` directory and register its test functions in that suite's main file.

## Test Helpers (`test_helpers.h`)

Shared utilities available to all test files:
- `setupInitialBoard(bb, mailbox)` — sets up standard starting position in `BitboardSet` + `Piece mailbox[64]`
- `clearBoard(bb, mailbox)` — empties the bitboard set and mailbox
- `placePiece(bb, mailbox, row, col, piece)` — places a piece at specific coordinates in both representations

## Testing Principles

- **Tests guard correctness** — never modify a test to make it pass. If a test fails, fix the production code.
- **Tests must stay in sync** — when changing chess logic in `lib/core/`, update or add tests in the same change. New public APIs, new structs, renamed parameters, moved functions, and new internal state (caches, derived fields) all need test coverage. This includes:
  - Struct behavior tests for new data types (e.g. `MoveList`, `HashHistory`)
  - State maintenance tests for derived/cached fields (e.g. king cache across `makeMove`/`reverseMove`/`loadFEN`)
  - Updating existing tests when signatures or patterns change
- **Test group descriptions must stay current** — when adding new test sections, update the Test Group Details and file structure listing in this file.
- **Always test changes** — every logic change must be validated by running the test suite before committing.

## Test Group Details

### Movegen (`test_movegen.cpp`)
Move generation for each piece type (pawn, knight, bishop, rook, queen, king). Blocked paths, captures, initial pawn double-push, edge of board. Starting position move counts. Legal move queries (`hasAnyLegalMove`). Bulk move generation: `Move`/`MoveList` struct behavior, `generateAllMoves` (initial position count, under check evasions, double check king-only, stalemate zero moves, consistency with per-piece `getPossibleMoves`), `generateCaptures` (capture-only filtering, EP included, no quiet moves), move flag correctness (capture, EP, castling, promotion with all 4 piece types), promotion index round-trip.

### Rules (`test_rules.cpp`)
Check detection from every piece type. Checkmate positions. Stalemate positions. Move legality when king is in check — blocking, capturing attacker, king escape. Pin-aware generation: pinned piece cannot leave pin ray; pinned piece can move along pin ray; diagonal pin; double check (only king can move); single-check slider blocking (checkMask filtering); knight check no blocking; two-friendly shielding not pinned; EP horizontal pin. Idempotency (repeated calls produce identical results). King position finder. `isValidMove`, `isDraw`, `isGameOver`, threefold repetition. Castling: rights preservation, blocking pieces, through-check prevention, both colors, queenside/kingside. En passant: standard capture, edge cases. Promotion: all piece types.

### Position (`test_position.cpp`)
New game state. Basic moves and captures. En passant execution. Castling execution (both sides). Promotion with piece selection. Check and checkmate detection. Stalemate. 50-move draw. Insufficient material (K vs K, K+B vs K, K+N vs K, K+B vs K+B same-color). Threefold repetition via Zobrist. FEN loading and validation. Compact encode/decode. Board API queries. `reverseMove()` for normal/capture/en passant/castling/promotion. `applyMoveEntry()` replay. King cache (`kingRow`/`kingCol`) correctness after `newGame()`, `loadFEN()`, `makeMove()` (king/non-king), castling, and `reverseMove()`. `MoveList` struct (initial state, add/access, clear, capacity, integration with `getPossibleMoves`). `HashHistory` struct (initial state, add/read, MAX_SIZE constant).

### Game (`test_game.cpp`)
Lifecycle: `endGame()` with various results, `isGameOver()` state, move rejection after game-over, checkmate/stalemate/insufficient/fifty-move auto-set game-over, undo clears game-over, `newGame()`/`loadFEN()` reset game-over. Draw detection. Observer notification and batching. History integration. Undo/redo cursor. `getHistory()` in all three formats (coordinate, SAN, LAN).

### History (`test_history.cpp`)
Move log add/undo/redo. Cursor positioning. Branch-on-undo: adding a move at a branch point wipes future moves.

### History Persistence (`test_history_persistence.cpp`)
Persistence lifecycle with MockGameStorage. Header flush timing. Game replay from storage. Branch-truncation of storage. Game recording integration: startNewGame, endGame, resume, auto-finish. Compact 2-byte encode/decode stability. On-disk format compatibility.

### Utils (`test_utils.cpp`)
50-move rule counter. Castling rights string formatting and parsing (`castlingCharToBit`, `hasCastlingRight`). Coordinate helpers (`squareName`, `fileChar`, `rankChar`, `fileIndex`, `rankIndex`, `isValidSquare`). Special-move analysis (`checkEnPassant`, `checkCastling`, `updateCastlingRights`). `applyBoardTransform`. `boardToText`. `positionState`. `gameResultName`. `isValidPromotionChar`.

### Evaluation (`test_evaluation.cpp`)
Material evaluation scoring. Pawn structure evaluation (symmetry, passed pawn bonus, doubled/isolated penalties). Tapered evaluation (opening symmetry, endgame king centralization, phase-dependent king PST blend). Pawn-structure analysis functions: `isPassed`, `isIsolated`, `isDoubled`, `isBackward`. Positional terms: bishop pair bonus (single side / both sides), rook on open file, rook on semi-open file, rook on 7th rank, mobility (centralized vs edge pieces, MG/EG split weights), king safety (intact pawn shield), king danger (close piece scores higher, multiple zone attackers vs no attackers), knight outpost, center control (pawn occupation and attack), space (territory behind pawn chain). Passed pawn rank scaling (advanced passer scores higher). Passed pawn king distance (own king close vs far from passer). Trapped pieces: bishop trapped on a7/h7 by enemy pawn, rook trapped by own uncastled king, symmetry. Connectivity: defended non-pawn non-king pieces score higher, symmetry. Pawn hash table: probe miss, store/probe round-trip, clear invalidation, integration with evaluatePosition. Eval hash table: probe miss, store/probe round-trip, clear invalidation, overwrite. Bad bishop (penalty per own pawn on same color complex). Rook behind passer (Tarrasch Rule, EG-only bonus for rook behind own passer vs rook in front). Protected passer (defended by a friendly pawn scores higher). Candidate passer (one enemy blocker vs two). Opposite-color bishop scaling (×0.75 reduction in endgame vs same-color bishops).

### Search (`test_search.cpp`)
Mate-in-1 (white, black). Captures hanging piece. Quiescence avoids blunder. Stalemate avoidance. Symmetric position. Knight fork tactics. Legal move from random position. Checkmate no legal moves. Iterative deepening (deeper depth finds mate, info callback reports iterations). IID preserves tactics, completes with TT. Lazy evaluation preserves tactics, balanced position correctness, imbalanced position scoring. Time limit control. Stop flag. Mate stops early. TT store/probe exact, probe miss, clear, pack/unpack move, reduces nodes, mate score round-trip. Check extension finds mate. NMP quiet position, K+P endgame no blunder. PVS+LMR middlegame efficiency. Pruning preserves tactics. Aspiration windows correctness, depth continuity. Root move reordering consistency. Move ordering reduces nodes and finds tactics. Delta pruning quiet position. Futility pruning preserves winning capture and discovered attack. SEE ordering preserves tactics. SEE qsearch skips losing capture. LMP preserves winning capture, completes without blunder. Razoring preserves winning capture, completes correctly. Countermove heuristic preserves tactics, integrates with TT. History gravity produces negative scores for non-cutoff quiets. Recapture extension finds exchange sequence. Adaptive NMP correctness (deeper search doesn't blunder). Singular extension preserves tactics, completes with captures. PV table accuracy (mate-in-2 PV length and consistency). Mate distance pruning (mate score > winning material). Capture history ordering (tactical position node efficiency). Staged MovePicker (mate via capture found without quiet generation). TT depth-preferred replacement (deep entry survives shallow collision). Soft time stops search (between iterations with timer). Easy move early exit (mate-in-1 terminates quickly).

### Engine (`test_engine.cpp`)
Engine facade: calculateMove, depth control, stop/external stop, mate-in-1, TT persistence, score range.

### FEN (`test_fen.cpp`)
Round-trip: board → FEN → board. `boardToFEN()` output correctness. `fenToBoard()` parsing. `validateFEN()`: valid positions, invalid rank structure, bad piece chars, wrong turn field, invalid castling, bad en passant, bad clocks.

### Notation (`test_notation.cpp`)
Coordinate notation output and parsing. SAN output (disambiguation, captures, castling, promotion). LAN output. Auto-format detection from input strings. Round-trip verification: format → parse → same move.

### Piece (`test_piece.cpp`)
piece namespace: bit extraction (`pieceType()` for all 12 pieces + NONE, `pieceColor()`), construction (`makePiece()` round-trips), predicates (`isEmpty()`, `isWhite()`, `isBlack()`, `isColor()`), color flip (`~Color`, `~Piece` operator overloads), FEN char round-trip (`charToPiece()`/`pieceToChar()` for all valid chars, invalid chars, NONE), PieceType char conversion, material values (`pieceValue()`, `pieceTypeValue()` for all piece types), Zobrist index (`pieceZobristIndex()` for all pieces + NONE), color helpers (`pawnDirection()`, `homeRow()`, `promotionRow()`, `colorName()`, `charToColor()`/`colorToChar()`, `getPieceColor`, `opponentColor`), zero-initialization safety, `isPromotion()` edge cases.

### Iterator (`test_iterator.cpp`)
`forEachSquare` visits all 64 squares. `forEachPiece` skips empty. `somePiece` early-exit. `findPiece` locates specific pieces with max limit.

### Zobrist (`test_zobrist.cpp`)
Zobrist key determinism. `pieceZobristIndex` mapping. `computeHash` stability. Hash changes with turn flip, castling rights, en passant. Hash equality for identical positions. `computePawnHash`: determinism, different pawn placements produce different hashes, ignores non-pawn pieces, no pawns yields zero.

### Perft (`test_perft/test_perft.cpp`)
Exhaustive move-tree enumeration for 6 positions from the Chess Programming Wiki. Positions 1–4 verify detailed leaf-level counters: nodes, captures, en passant, castles, promotions, checks, and checkmates. Positions 5–6 verify node counts only (no wiki reference counters). Catches compensating bugs that node-only perft misses (e.g. a missed capture offset by a phantom quiet move).

### Bitboard (`test_bitboard.cpp`)
Square mapping roundtrip (`squareOf(rowOf(sq), colOf(sq)) == sq` for all 64). LERF anchor values (`squareOf(0,0) == SQ_A8`, `squareOf(7,0) == SQ_A1`). Bit manipulation (`popcount`, `lsb`, `popLsb`). Square-color masks (a1 dark, b1 light, popcount 32 each, no overlap). `BitboardSet` mutations (`setPiece`/`removePiece`/`movePiece` consistency, aggregate bitboard correctness).

### Attacks (`test_attacks.cpp`)
Leaper attack tables (knight on e4, king on a1, pawn attacks per color). Slider attack functions (rook/bishop/queen on empty board and with blockers). Bulk slider correctness (all 64 squares × 5 occupancy patterns cross-checked against reference ray implementation for both rook and bishop). X-ray attack functions (`xrayRook`, `xrayBishop`). `between` geometry (file/rank/diagonal/anti-diagonal/adjacent/non-colinear). `line` geometry (rank/file/diagonal/non-colinear/endpoints). `computeAll` validation (initial knight attacks, pawn bulk attacks, color unions, kings-only board). SEE: pawn takes undefended knight, pawn takes defended rook, knight takes defended pawn (losing), queen takes defended pawn (losing), rook takes undefended bishop, en passant.

### EPD Parser (`test_epd.cpp`)
`parseEPDLine`: basic FEN + bm opcode, multiple opcodes (bm + am + id + c0), quoted id strings, avoid move variations, comma-separated operands, c9 game result parsing (white win/draw/black win). `validateEPDLine`: valid/invalid FEN, missing fields. Accessors: `findOperation()` lookup, `id()` convenience, non-existent opcode returns nullptr yet `id()` returns empty.

### Tactical Suites (`test_tactics/test_tactics.cpp`)
Engine accuracy benchmarks using standard `.epd` files loaded at runtime via the EPD parser. Each suite reads its `.epd` file, parses each line into an `EPDRecord`, runs `search::findBestMove` at depth 6, converts both expected (SAN) and engine (Move) results to coordinate notation, and compares. Suites: **WAC** (Win At Chess, 300 positions), **BK** (Bratko-Kopec, 24 positions), **ERET** (Eigenmann Rapid Engine Test, 111 positions). Tests are informational — they assert only that at least one position is solved, printing individual mismatches and overall pass rate. Runtime ~3.5 minutes total.
