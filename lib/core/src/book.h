#ifndef LIBRECHESS_BOOK_H
#define LIBRECHESS_BOOK_H

// ---------------------------------------------------------------------------
// Internal opening book — compact, static-memory-resident.
//
// ~43 curated opening lines covering all major families, built at static
// initialization into packed (hash, from, to) storage.  Probed via
// linear scan in findBestMove() before iterative deepening — if a position
// is in the book, a randomly-selected book move is returned instantly.
//
// Zero heap usage — the entire book lives in static memory.
// PRNG state (xorshift64) is maintained by the caller for per-game variety.
//
// Reference: https://www.chessprogramming.org/Opening_Book
// ---------------------------------------------------------------------------

#include <cstdint>

namespace LibreChess {
namespace book {

// ---------------------------------------------------------------------------
// BookEntry — public one (position, move) pair shape.  The internal compiled
// book uses packed parallel arrays to avoid per-entry struct padding.  No
// promotion field — no promotions occur within the first ~10 half-moves of
// standard opening lines.
// ---------------------------------------------------------------------------

struct BookEntry {
  uint64_t hash = 0;   // Zobrist hash of the position *before* the move
  uint8_t from  = 0;   // LERF source square (0–63)
  uint8_t to    = 0;   // LERF destination square (0–63)
};

// ---------------------------------------------------------------------------
// Probe the opening book for the given position hash.
//
// If a book move exists, sets `from` and `to` (LERF squares) and advances
// the PRNG state `rng`.  Returns true on hit, false on miss.
//
// When multiple book moves exist for the same position, one is selected
// uniformly at random using the caller-supplied xorshift64 state.
//
// `rng` must be non-zero on first call (seeded by the caller).
// ---------------------------------------------------------------------------

bool probe(uint64_t hash, uint8_t& from, uint8_t& to, uint64_t& rng);

// Number of entries in the book (for diagnostics / tests).
int entryCount();

}  // namespace book
}  // namespace LibreChess

#endif  // LIBRECHESS_BOOK_H
