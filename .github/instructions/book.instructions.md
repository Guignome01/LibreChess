---
applyTo: "lib/core/src/book.*"
description: "Internal opening book: curated opening lines, ReplayBoard replay, Zobrist hash, linear probe. Use when editing book.h or book.cpp."
---

# Opening Book (`lib/core/src/book.h/cpp`)

Compact internal opening book — ~43 curated opening lines covering all major families.  Built at static initialization, probed before iterative deepening in `findBestMove()`.

Reference: https://www.chessprogramming.org/Opening_Book

## File Organization

| File | Purpose |
|------|---------|
| `book.h` | Public API: `BookEntry`, `probe()`, `entryCount()` |
| `book.cpp` | ReplayBoard, line replay, hash computation, opening lines, probe logic |

## Architecture

### Build Phase (constexpr on GCC 6+ / runtime on GCC 5.x)
- `ReplayBoard` — 64-byte mailbox + castling/EP/side, constexpr starting position
- Each opening line (coordinate notation, e.g. `"e2e4 e7e5 g1f3"`) is replayed move-by-move
- At each ply, a `BookEntry{hash, from, to}` is recorded (hash = position BEFORE the move)
- Zobrist keys accessed via `BOOK_KEYS` macro alias — resolves to a local `constexpr Keys` on GCC 6+ or `zobrist::KEYS` reference on GCC 5.x
- ~500 raw entries (with duplicates from shared early moves, deduplicated at probe time)

### Probe Phase (runtime)
- `probe(hash, from, to, rng)` — linear scan collecting distinct matching `(from, to)` pairs
- When multiple book moves exist, one is selected uniformly at random via xorshift64 PRNG
- Called from `findBestMove()` after root move generation, before iterative deepening
- Returns `true` on hit (with `from`/`to` set), `false` on miss

### Memory Model — Conditional Constexpr
- **GCC 6+** (ESP32 xtensa-esp32-elf-g++ 8.x): `static constexpr BookData BOOK = buildBook()` — fully computed at compile time, placed in `.rodata` (flash), zero RAM
- **GCC 5.x** (MinGW native tests): `static const BookData BOOK = buildBook()` — runtime static init, entries in BSS (~12 KiB RAM), correct hashes via `zobrist::KEYS`
- GCC version check: `#if __GNUC__ > 5` gates constexpr path; `BOOK_CX` macro expands to `constexpr` or nothing accordingly
- GCC 5.x constexpr evaluator bug: produces incorrect Zobrist hashes for complex computations (individual keys are correct but compound hash evaluation is wrong)
- String literals (`LINES[]`) always in `.rodata` (flash) on both paths
- Total footprint on ESP32: ~12 KiB entries + ~3 KiB strings, all in flash

## Integration Points

### SearchState (`search.h`)
- `bool useBook = false` — opt-in; enabled by `Game::setTimeFunc()` and `UCIState` constructor
- `uint64_t bookRng` — xorshift64 PRNG state for random book move selection, seeded per-game

### findBestMove (`search.cpp`)
- Book probe inserted after root move generation and single-move early return
- On hit: returns `SearchResult` with `depth=0`, `nodes=0`, `bestMove` from book
- On miss: proceeds to normal iterative deepening

### UCI (`uci.cpp`)
- `OwnBook` option (check, default true) — maps to `searchState.useBook`
- Enabled in `UCIState` constructor

### Game (`game.cpp`)
- `setTimeFunc()` enables book and seeds `bookRng` from current time

## ReplayBoard Details

Minimal board for move replay:
- `uint8_t squares[64]` — Piece raw values (`raw(Piece::W_ROOK)` etc.), 0 = empty
- `uint8_t castling` — 4-bit mask (0x01=WK, 0x02=WQ, 0x04=BK, 0x08=BQ)
- `uint8_t epFile` — file of EP target (0–7), or 0xFF if none
- `bool whiteToMove`

`applyMove()` handles: quiet moves, captures, double pawn pushes, EP captures, castling (king+rook).  No promotion support (unnecessary within book depth).

EP legality checked via `hasAdjacentEnemyPawn()` (pin-unaware — acceptable for standard openings).

## Opening Line Coverage

43 lines organized by opening family:
- **Open Games** (1.e4 e5): Italian, Ruy Lopez (Closed + Marshall), Scotch, Petroff, Four Knights, Berlin, King's Gambit
- **Sicilian**: Najdorf, Dragon, Scheveningen, Sveshnikov, Alapin, Classical
- **French**: Winawer, Classical, Tarrasch, Exchange
- **Caro-Kann**: Classical, Advance, Two Knights
- **Other Semi-Open**: Pirc, Scandinavian
- **QGD/QGA**: Orthodox, Exchange, QGA Main, Ragozin
- **Slav/Semi-Slav/Catalan**: Slav, Semi-Slav, Catalan Open
- **Indian**: Nimzo (Rubinstein + Classical), Queen's Indian, KID (Classical + Sämisch), Grünfeld (Exchange + Russian), Benoni Modern
- **Flank**: English (Symmetrical + Reversed Sicilian), Réti, London, Dutch Leningrad
