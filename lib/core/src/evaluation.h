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
// stores the midgame and endgame pawn structure scores.  Because pawn
// structures change infrequently during search (~1 pawn move per 30 plies),
// hit rates of 95%+ are typical.
//
// Entry size: 8 bytes.  Default 512 entries = 4 KiB.
// Reference: https://www.chessprogramming.org/Pawn_Hash_Table
// ---------------------------------------------------------------------------

static constexpr int DEFAULT_PAWN_HASH_SIZE = 512;

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
// Entry size: 8 bytes.  Default 4096 entries = 32 KiB (2048 = 16 KiB on
// memory-constrained targets).
// Reference: https://www.chessprogramming.org/Evaluation_Hash_Table
// ---------------------------------------------------------------------------

#ifdef HARDWARE_LIMITATION
static constexpr int DEFAULT_EVAL_HASH_SIZE = 2048;
#else
static constexpr int DEFAULT_EVAL_HASH_SIZE = 4096;
#endif

struct EvalEntry {
  uint32_t key;       // Upper 32 bits of position Zobrist hash (verification)
  int16_t  score;     // White-relative evaluation score (centipawns)
  uint16_t pad;       // Padding for alignment
};

static_assert(sizeof(EvalEntry) == 8, "EvalEntry should be 8 bytes");

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

// Evaluate with precomputed material+PST scores.
// Skips the per-piece material+PST loop — uses `mgMatPST` / `egMatPST`
// directly.  Used by the search engine where Position tracks these values
// incrementally via make/unmake.
// Reference: https://www.chessprogramming.org/Incremental_Updates
int evaluatePosition(const BitboardSet& bb, int mgMatPST, int egMatPST,
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
void initPawnMasks();
bool isPassed(Square sq, Color color, Bitboard enemyPawns);
bool isIsolated(Square sq, Bitboard friendlyPawns);
bool isDoubled(Square sq, Color color, Bitboard friendlyPawns);
bool isBackward(Square sq, Color color, Bitboard friendlyPawns, Bitboard enemyPawnAttacks);

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

// Phase weight per PieceType (indexed by raw PieceType value).
// NONE=0, PAWN=0, KNIGHT=1, BISHOP=1, ROOK=2, QUEEN=4, KING=0.
// Reference: https://www.chessprogramming.org/Game_Phases
constexpr int PHASE_WEIGHT[] = {0, 0, PHASE_KNIGHT, PHASE_BISHOP,
                                PHASE_ROOK, PHASE_QUEEN, 0};

// Compute game phase from bitboards (sum of non-pawn piece weights).
// Used for initial computation; the search path uses an incremental
// phase accumulator in Position to avoid 4 popcounts per eval.
int computeGamePhase(const BitboardSet& bb);

// Opposite-color bishop scaling (numerator/denominator form, default 3/4).
constexpr int OCB_SCALE_NUM   = 3;
constexpr int OCB_SCALE_DENOM = 4;

// Maximum game phase for OCB scaling to apply.
constexpr int OCB_PHASE_THRESHOLD = 6;

// King danger table size.
constexpr int KING_DANGER_TABLE_SIZE = 13;

#ifdef TUNING

// Invalidate flat PSQT lookup tables after parameter changes.
// Must be called after modifying any MATERIAL or PST value so that
// subsequent pieceSquareMG/EG/MGEG and computeMaterialPST calls
// use the updated values.
void invalidatePSQT();

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

// Material (MG = MATERIAL[], defines centipawn unit; EG separate).
extern int MATERIAL[6];
extern int MATERIAL_EG[6];

// Piece-square tables (12 arrays × 64 squares).
extern int PST_PAWN_MG[64],   PST_KNIGHT_MG[64], PST_BISHOP_MG[64];
extern int PST_ROOK_MG[64],   PST_QUEEN_MG[64],  PST_KING_MG[64];
extern int PST_PAWN_EG[64],   PST_KNIGHT_EG[64],  PST_BISHOP_EG[64];
extern int PST_ROOK_EG[64],   PST_QUEEN_EG[64],   PST_KING_EG[64];

// Passed pawn rank bonuses.
extern int PASSED_RANK_BONUS_MG[8], PASSED_RANK_BONUS_EG[8];

// Pawn structure (separate MG/EG).
extern int CONNECTED_PASSED_MG, CONNECTED_PASSED_EG;
extern int ISOLATED_PENALTY_MG, ISOLATED_PENALTY_EG;
extern int DOUBLED_PENALTY_MG, DOUBLED_PENALTY_EG;
extern int BACKWARD_PENALTY_MG, BACKWARD_PENALTY_EG;
extern int PROTECTED_PASSER_MG;

// Piece bonuses.
extern int BISHOP_PAIR_MG, BISHOP_PAIR_EG;
extern int ROOK_OPEN_FILE_MG, ROOK_OPEN_FILE_EG;
extern int ROOK_SEMI_OPEN_FILE_MG, ROOK_SEMI_OPEN_FILE_EG;
extern int ROOK_7TH_MG, ROOK_7TH_EG;
extern int ROOK_BEHIND_OWN_PASSER_EG, ROOK_BEHIND_ENEMY_PASSER_EG;
extern int BAD_BISHOP_MG, BAD_BISHOP_EG;
extern int OUTPOST_BONUS_MG, OUTPOST_BONUS_EG;
extern int TRAPPED_BISHOP_PENALTY, TRAPPED_ROOK_PENALTY;

// Mobility.
extern int MOBILITY_KNIGHT_MG, MOBILITY_KNIGHT_EG;
extern int MOBILITY_BISHOP_MG, MOBILITY_BISHOP_EG;
extern int MOBILITY_ROOK_MG, MOBILITY_ROOK_EG;
extern int MOBILITY_QUEEN_MG, MOBILITY_QUEEN_EG;

// King safety.
extern int SHIELD_MISSING_PAWN, SHIELD_ADV_RANK3, SHIELD_ADV_RANK4PLUS, SHIELD_OPEN_FILE;
extern int KING_DANGER_TABLE[13];
extern const int KING_DANGER_WEIGHT[4];

// Space, king distance.
extern int SPACE_BONUS_MG;
extern int PASSER_OWN_KING, PASSER_ENEMY_KING;

// Threats (MG only).
extern int THREAT_PAWN_VS_MINOR_MG;
extern int THREAT_PAWN_VS_ROOK_MG;
extern int THREAT_PAWN_VS_QUEEN_MG;
extern int THREAT_MINOR_VS_ROOK_MG;
extern int THREAT_MINOR_VS_QUEEN_MG;
extern int THREAT_ROOK_VS_QUEEN_MG;

// Non-tunable constants.
extern const Bitboard WHITE_SPACE_ZONE, BLACK_SPACE_ZONE;

// Chebyshev (king) distance between two LERF squares.
int chebyshevDist(Square a, Square b);
#endif

}  // namespace eval
}  // namespace LibreChess

#endif  // LIBRECHESS_EVALUATION_H
