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

// Material value for a piece type, in centipawns.
// Provides a single source of truth for piece values used by evaluation,
// lazy evaluation, delta pruning, and SEE.
// PieceType::NONE returns 0; PieceType::KING returns 0 (not a material piece).
int materialValue(PieceType pt);

// Pawn-structure query functions (exposed for unit testing).
void initPawnMasks();
bool isPassed(int sq, Color color, uint64_t enemyPawns);
bool isIsolated(int sq, uint64_t friendlyPawns);
bool isDoubled(int sq, Color color, uint64_t friendlyPawns);
bool isBackward(int sq, Color color, uint64_t friendlyPawns, uint64_t enemyPawnAttacks);

// ---------------------------------------------------------------------------
// Tuning API — runtime parameter access for the gradient-descent optimizer.
// Only available when compiled with -DTUNING.
// ---------------------------------------------------------------------------

// Phase weights for game-phase interpolation (always available).
constexpr int PHASE_KNIGHT = 1;
constexpr int PHASE_BISHOP = 1;
constexpr int PHASE_ROOK   = 2;
constexpr int PHASE_QUEEN  = 4;
constexpr int MAX_PHASE    = 24;

// King danger table size.
constexpr int KING_DANGER_TABLE_SIZE = 13;

#ifdef TUNING
namespace tuning {

int paramCount();
const char* getName(int idx);
int getValue(int idx);
void setValue(int idx, int val);
int getDefault(int idx);
int getMin(int idx);
int getMax(int idx);
int getStep(int idx);

}  // namespace tuning

// ---------------------------------------------------------------------------
// Eval internals — exposed for trace extraction (trace.cpp) and registry.
// These mirror the file-local constants in evaluation.cpp.
// Only available in tuning builds; production builds keep them file-local.
// ---------------------------------------------------------------------------

// Material.
extern int MATERIAL[6];

// Piece-square tables (12 arrays × 64 squares).
extern int PST_PAWN_MG[64],   PST_KNIGHT_MG[64], PST_BISHOP_MG[64];
extern int PST_ROOK_MG[64],   PST_QUEEN_MG[64],  PST_KING_MG[64];
extern int PST_PAWN_EG[64],   PST_KNIGHT_EG[64],  PST_BISHOP_EG[64];
extern int PST_ROOK_EG[64],   PST_QUEEN_EG[64],   PST_KING_EG[64];

// Passed pawn rank bonuses.
extern int PASSED_RANK_BONUS_MG[8], PASSED_RANK_BONUS_EG[8];

// Pawn structure.
extern int CONNECTED_PASSED, ISOLATED_PENALTY, DOUBLED_PENALTY, BACKWARD_PENALTY;
extern int PROTECTED_PASSER_MG, PROTECTED_PASSER_EG;
extern int CANDIDATE_PASSER_MG, CANDIDATE_PASSER_EG;

// Piece bonuses.
extern int BISHOP_PAIR_MG, BISHOP_PAIR_EG;
extern int ROOK_OPEN_FILE, ROOK_SEMI_OPEN_FILE;
extern int ROOK_7TH_MG, ROOK_7TH_EG;
extern int ROOK_BEHIND_OWN_PASSER_EG, ROOK_BEHIND_ENEMY_PASSER_EG;
extern int BAD_BISHOP_MG, BAD_BISHOP_EG;
extern int OUTPOST_BONUS;
extern int TRAPPED_BISHOP_PENALTY, TRAPPED_ROOK_PENALTY;

// Mobility.
extern int MOBILITY_KNIGHT_MG, MOBILITY_KNIGHT_EG;
extern int MOBILITY_BISHOP_MG, MOBILITY_BISHOP_EG;
extern int MOBILITY_ROOK_MG, MOBILITY_ROOK_EG;
extern int MOBILITY_QUEEN_MG, MOBILITY_QUEEN_EG;

// King safety.
extern int SHIELD_MISSING_PAWN, SHIELD_ADVANCED_PAWN, SHIELD_OPEN_FILE;
extern int KING_DANGER_TABLE[13];
extern const int KING_DANGER_WEIGHT[4];

// Center, space, king distance.
extern int CENTER_OCCUPATION_BONUS, CENTER_ATTACK_BONUS;
extern int SPACE_BONUS;
extern int PASSER_OWN_KING, PASSER_ENEMY_KING;

// Threats.
extern int THREAT_PAWN_VS_MINOR_MG, THREAT_PAWN_VS_MINOR_EG;
extern int THREAT_PAWN_VS_ROOK_MG, THREAT_PAWN_VS_ROOK_EG;
extern int THREAT_PAWN_VS_QUEEN_MG, THREAT_PAWN_VS_QUEEN_EG;
extern int THREAT_MINOR_VS_ROOK_MG, THREAT_MINOR_VS_ROOK_EG;
extern int THREAT_MINOR_VS_QUEEN_MG, THREAT_MINOR_VS_QUEEN_EG;
extern int THREAT_ROOK_VS_QUEEN_MG, THREAT_ROOK_VS_QUEEN_EG;
extern int CONNECTIVITY_BONUS;

// Non-tunable constants.
extern const Bitboard CENTER_MASK;
extern const Bitboard WHITE_SPACE_ZONE, BLACK_SPACE_ZONE;

// Chebyshev (king) distance between two LERF squares.
int chebyshevDist(Square a, Square b);
#endif

}  // namespace eval
}  // namespace LibreChess

#endif  // LIBRECHESS_EVALUATION_H
