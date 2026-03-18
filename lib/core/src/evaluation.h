#ifndef LIBRECHESS_EVALUATION_H
#define LIBRECHESS_EVALUATION_H

// ---------------------------------------------------------------------------
// Position evaluation — tapered eval with midgame/endgame PSTs and pawn
// structure analysis.  Includes pawn hash and evaluation hash tables for
// caching expensive computations during search.
// ---------------------------------------------------------------------------

#include <stdint.h>

#include "bitboard.h"

namespace LibreChess {
namespace eval {

// ---------------------------------------------------------------------------
// Pawn Hash Table — caches pawn structure scores (MG + EG separately).
//
// Keyed by a pawn-only Zobrist hash (zobrist::computePawnHash).  Each entry
// stores the midgame and endgame pawn structure scores.  Because pawn
// structures change infrequently during search (~1 pawn move per 30 plies),
// hit rates of 95%+ are typical.
//
// Entry size: 8 bytes.  Default 1024 entries = 8 KiB.
// Reference: https://www.chessprogramming.org/Pawn_Hash_Table
// ---------------------------------------------------------------------------

static constexpr int DEFAULT_PAWN_HASH_SIZE = 1024;

struct PawnEntry {
  uint32_t key;       // Upper 32 bits of pawn Zobrist hash (verification)
  int16_t  mgScore;   // Midgame pawn structure score (white-relative)
  int16_t  egScore;   // Endgame pawn structure score (white-relative)
};

static_assert(sizeof(PawnEntry) == 8, "PawnEntry should be 8 bytes");

struct PawnHashTable {
  PawnEntry* entries = nullptr;
  int size = 0;   // Number of entries (power of 2)
  int mask = 0;   // size - 1

  // Allocate entries.  `numEntries` is rounded down to nearest power of 2.
  void resize(int numEntries);

  // Release memory.
  void free();

  // Clear all entries (zero-fill).
  void clear();

  // Probe the table for a matching entry.  Returns nullptr on miss.
  const PawnEntry* probe(uint64_t hash) const;

  // Store an entry (always-replace).
  void store(uint64_t hash, int16_t mgScore, int16_t egScore);
};

// ---------------------------------------------------------------------------
// Evaluation Hash Table — caches full evaluatePosition() results.
//
// Keyed by the full position Zobrist hash.  Avoids redundant evaluations
// when the same position is reached via different move orderings (which is
// common in search trees).  The cached score is the raw white-relative
// evaluatePosition() output — the search layer applies STM flip and tempo.
//
// Entry size: 8 bytes.  Default 1024 entries = 8 KiB.
// Reference: https://www.chessprogramming.org/Evaluation_Hash_Table
// ---------------------------------------------------------------------------

static constexpr int DEFAULT_EVAL_HASH_SIZE = 1024;

struct EvalEntry {
  uint32_t key;       // Upper 32 bits of position Zobrist hash (verification)
  int16_t  score;     // White-relative evaluation score (centipawns)
  uint16_t pad;       // Padding for alignment
};

static_assert(sizeof(EvalEntry) == 8, "EvalEntry should be 8 bytes");

struct EvalHashTable {
  EvalEntry* entries = nullptr;
  int size = 0;   // Number of entries (power of 2)
  int mask = 0;   // size - 1

  // Allocate entries.  `numEntries` is rounded down to nearest power of 2.
  void resize(int numEntries);

  // Release memory.
  void free();

  // Clear all entries (zero-fill).
  void clear();

  // Probe the table for a matching entry.  Returns nullptr on miss.
  const EvalEntry* probe(uint64_t hash) const;

  // Store an entry (always-replace).
  void store(uint64_t hash, int16_t score);
};

// ---------------------------------------------------------------------------
// Evaluation API
// ---------------------------------------------------------------------------

// Evaluate board position using tapered evaluation.
// Returns evaluation in centipawns (positive = White, negative = Black).
// `pawnHash` is an optional pawn hash table for caching pawn structure scores.
int evaluatePosition(const BitboardSet& bb,
                     PawnHashTable* pawnHash = nullptr);

// Pawn-structure query functions (exposed for unit testing).
void initPawnMasks();
bool isPassed(int sq, Color color, uint64_t enemyPawns);
bool isIsolated(int sq, uint64_t friendlyPawns);
bool isDoubled(int sq, Color color, uint64_t friendlyPawns);
bool isBackward(int sq, Color color, uint64_t friendlyPawns, uint64_t enemyPawnAttacks);

}  // namespace eval
}  // namespace LibreChess

#endif  // LIBRECHESS_EVALUATION_H
