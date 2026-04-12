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

// Forward declaration — avoids #include "attacks.h" in the eval header.
namespace attacks { struct AttackInfo; }

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
bool isBackward(Square sq, Color color, Bitboard friendlyPawns,
                Bitboard enemyPawns);

// Tempo bonus applied by search (sideTomove * tempoBonus).
// Accessor avoids exposing eval_params.h (which has TUNING ODR constraints).
int tempoBonus();

// King danger table lookup — clamps weight to [0, KING_SAFETY_TABLE_SIZE-1]
// and returns the corresponding penalty.  Used by trace extraction to add the
// non-tunable S-curve penalty to bias without including eval_params.h.
int kingDangerScore(int weight);

// Chebyshev distance (king distance metric) between two squares.
// Used by passed pawn king proximity evaluation.
inline int chebyshevDistance(Square a, Square b) {
  int dr = rankOf(a) - rankOf(b);
  int df = fileOf(a) - fileOf(b);
  if (dr < 0) dr = -dr;
  if (df < 0) df = -df;
  return dr > df ? dr : df;
}

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

// King safety table size (entries in KING_SAFETY_TABLE[]).
constexpr int KING_SAFETY_TABLE_SIZE = 100;

// ---------------------------------------------------------------------------
// Feature extraction helpers — shared between evaluation and trace.
//
// Each function computes intermediate values for an eval term without
// applying parameter weights or accumulating scores.  This eliminates
// logic duplication between evaluatePosition() (which computes a scalar)
// and extractTrace() (which extracts gradient coefficients).
//
// All functions are per-side: `c` is the color index (0=WHITE, 1=BLACK).
// ---------------------------------------------------------------------------

// Threat counts for one side.
struct ThreatCounts {
  int byPawn;    // pawn attacks on enemy minors/rooks/queens
  int byMinor;   // minor attacks on enemy rooks/queens
  int byRook;    // rook attacks on enemy queens
  int hanging;   // attacked enemy pieces with no defender
};
ThreatCounts computeThreats(const BitboardSet& bb,
                            const attacks::AttackInfo& info, int c);

// Safe mobility counts for one side (clamped to table bounds).
struct MobilityCounts {
  int knight, bishop, rook, queen;
};
MobilityCounts computeMobility(const BitboardSet& bb,
                               const attacks::AttackInfo& info, int c);

// King danger intermediate values for one side (the side being attacked).
struct KingDangerInfo {
  int attackWeight;
  int attackerCount;
  bool hasQueen;
  bool knightSafeCheck, bishopSafeCheck, rookSafeCheck, queenSafeCheck;
};
KingDangerInfo computeKingDanger(const BitboardSet& bb,
                                 const attacks::AttackInfo& info, int c);

// Space intermediate values for one side.
struct SpaceInfo {
  int bonus;   // safe squares + behind-pawn double count
  int weight;  // pieceCount - 2*openFiles, clamped >= 0
};
SpaceInfo computeSpace(const BitboardSet& bb, int c, int openFiles);

// Count open files (files with no pawns at all).
int countOpenFiles(const BitboardSet& bb);

// Outpost detection: tests whether a piece on `sq` is an outpost for `c`.
bool isOutpostSquare(Square sq, int c, Bitboard friendlyPawnAtk,
                     Bitboard enemyPawns);

}  // namespace eval
}  // namespace LibreChess

#endif  // LIBRECHESS_EVALUATION_H
