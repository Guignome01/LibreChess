#ifndef LIBRECHESS_EVALUATION_H
#define LIBRECHESS_EVALUATION_H

// ---------------------------------------------------------------------------
// Position evaluation — tapered eval with midgame/endgame PSTs and pawn
// structure analysis.  Includes pawn hash and evaluation hash tables for
// caching expensive computations during search.
// ---------------------------------------------------------------------------

#include <stdint.h>

#include "bitboard.h"
#include "hash_table.h"

namespace LibreChess {
namespace eval {

// ---------------------------------------------------------------------------
// Pawn Hash Table — caches pawn structure scores (MG + EG separately).
//
// Keyed by a pawn-only Zobrist hash (zobrist::computePawnHash).  Each entry
// stores the midgame and endgame pawn structure scores plus passed pawn
// bitboards.  Because pawn structures change infrequently during search
// (~1 pawn move per 30 plies), hit rates of 95%+ are typical.
//
// Entry size: 24 bytes.  Default 256 entries = 6 KiB.
// Reference: https://www.chessprogramming.org/Pawn_Hash_Table
// ---------------------------------------------------------------------------

static constexpr int DEFAULT_PAWN_HASH_SIZE = 256;

struct PawnEntry {
  uint32_t key;       // Upper 32 bits of pawn Zobrist hash (verification)
  int16_t  mgScore;   // Midgame pawn structure score (white-relative)
  int16_t  egScore;   // Endgame pawn structure score (white-relative)
  Bitboard passedPawns[2]; // Passed pawn bitboards [WHITE][BLACK]
};

static_assert(sizeof(PawnEntry) == 24, "PawnEntry should be 24 bytes");

struct PawnHashTable : HashTableBase<PawnEntry> {
  // Probe the table for a matching entry.  Returns nullptr on miss.
  const PawnEntry* probe(uint64_t hash) const;

  // Store an entry (always-replace).
  void store(uint64_t hash, int16_t mgScore, int16_t egScore,
             Bitboard passedWhite, Bitboard passedBlack);
};

// ---------------------------------------------------------------------------
// Evaluation Hash Table — caches full evaluatePosition() results.
//
// Keyed by the full position Zobrist hash.  Avoids redundant evaluations
// when the same position is reached via different move orderings (which is
// common in search trees).  The cached score is the raw white-relative
// evaluatePosition() output — the search layer applies STM flip and tempo.
//
// Entry size: 4 bytes.  Compact 16-bit key combined with the index mask
// provides ~26 effective bits of collision resistance — sufficient for a
// soft cache.
// Default 1024 entries = 4 KiB.
// Reference: https://www.chessprogramming.org/Evaluation_Hash_Table
// ---------------------------------------------------------------------------

static constexpr int DEFAULT_EVAL_HASH_SIZE = 1024;

struct EvalEntry {
  uint16_t key;       // Upper 16 bits of (hash >> 32) — compact verification
  int16_t  score;     // White-relative evaluation score (centipawns)
};

static_assert(sizeof(EvalEntry) == 4, "EvalEntry should be 4 bytes");

struct EvalHashTable : HashTableBase<EvalEntry> {
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

// Evaluate with precomputed material+PST scores AND incremental phase.
// Eliminates 4 popcount calls per eval by receiving the phase value from
// Position's incremental accumulator.
// Reference: https://www.chessprogramming.org/Incremental_Updates
int evaluatePosition(const BitboardSet& bb, int mgMatPST, int egMatPST,
                     int phase, PawnHashTable* pawnHash = nullptr);

// Combined MG+EG material+PST score for a single piece.
// Returns both scores in one call, halving index arithmetic when both
// MG and EG are needed (e.g. incremental updates in make/unmake).
// Reference: https://www.chessprogramming.org/Piece-Square_Tables
struct PSQTPair { int mg; int eg; };
PSQTPair pieceSquareMGEG(int pieceIdx, Square sq);

// Full material+PST sum computed from scratch in a single pass.
// Returns both MG and EG scores.  Used to initialize Position's
// incremental accumulators.
PSQTPair computeMaterialPST(const BitboardSet& bb);

// Pure material sum (no PST), computed from scratch.  White-relative
// centipawns (positive = White has more material).  Used to initialize
// Position's incremental material accumulator.
// Reference: https://www.chessprogramming.org/Incremental_Updates
int computeMaterial(const BitboardSet& bb);

// Material value for a piece type, in centipawns.
// Provides a single source of truth for piece values used by evaluation,
// lazy evaluation, delta pruning, and SEE.
// PieceType::NONE returns 0; PieceType::KING returns 0 (not a material piece).
int materialValue(PieceType pt);

// Pawn-structure query functions (exposed for unit testing).
bool isPassed(Square sq, Color color, Bitboard enemyPawns);
bool isIsolated(Square sq, Bitboard friendlyPawns);
bool isDoubled(Square sq, Color color, Bitboard friendlyPawns);
bool isBackward(Square sq, Color color, Bitboard friendlyPawns, Bitboard enemyPawnAttacks);

// ---------------------------------------------------------------------------
// Evaluation Constants
// ---------------------------------------------------------------------------

// Game phase weights — used by Position for incremental phase tracking
// and by evaluatePosition() for MG/EG interpolation.
// Reference: https://www.chessprogramming.org/Game_Phases
constexpr int PHASE_KNIGHT = 1;
constexpr int PHASE_BISHOP = 1;
constexpr int PHASE_ROOK   = 2;
constexpr int PHASE_QUEEN  = 4;
constexpr int MAX_PHASE    = 24;

// Phase weight per PieceType (indexed by raw PieceType value).
// NONE=0, PAWN=0, KNIGHT=1, BISHOP=1, ROOK=2, QUEEN=4, KING=0.
constexpr int PHASE_WEIGHT[] = {0, 0, PHASE_KNIGHT, PHASE_BISHOP,
                                PHASE_ROOK, PHASE_QUEEN, 0};

// Compute game phase from bitboards (sum of non-pawn piece weights).
// Used for initial computation; the search path uses an incremental
// phase accumulator in Position to avoid 4 popcounts per eval.
int computeGamePhase(const BitboardSet& bb);

// King danger table size (entries in KING_DANGER_TABLE[]).
constexpr int KING_DANGER_TABLE_SIZE = 13;

// Mobility table sizes (max possible attack count per piece type + 1).
constexpr int MOBILITY_KNIGHT_SIZE =  9;
constexpr int MOBILITY_BISHOP_SIZE = 14;
constexpr int MOBILITY_ROOK_SIZE   = 15;
constexpr int MOBILITY_QUEEN_SIZE  = 28;

// Chebyshev (king) distance between two LERF squares.
int chebyshevDist(Square a, Square b);

}  // namespace eval
}  // namespace LibreChess

#endif  // LIBRECHESS_EVALUATION_H
