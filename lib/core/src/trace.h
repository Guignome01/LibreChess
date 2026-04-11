#ifndef LIBRECHESS_TRACE_H
#define LIBRECHESS_TRACE_H

// ---------------------------------------------------------------------------
// Trace — tuning infrastructure for Texel's tuning method.
//
// Contains: sparse feature vector types, trace extraction, parameter
// descriptor types, registry accessors, and extern declarations for all
// eval params.  Everything is guarded by #ifdef TUNING — production and
// test builds compile this header to nothing.
//
// Lives in lib/core/src/ alongside evaluation.h (like Stockfish keeps its
// tuning infrastructure with the engine).  The standalone optimizer binary
// (tools/tune/tune.cpp) includes this header for all tuning needs.
//
// Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method
// ---------------------------------------------------------------------------

#ifdef TUNING

#include <cstdint>
#include <vector>

#include "bitboard.h"
#include "types.h"

namespace LibreChess {
namespace eval {

// ---------------------------------------------------------------------------
// TraceEntry — one nonzero coefficient in the sparse feature vector.
// ---------------------------------------------------------------------------

struct TraceEntry {
  int16_t idx;    // Parameter index in the tuning registry.
  float   coeff;  // Feature coefficient (phase-weighted, sign-adjusted).
};

// ---------------------------------------------------------------------------
// Trace — sparse vector of (param_index, coefficient) pairs.
// ---------------------------------------------------------------------------

struct Trace {
  std::vector<TraceEntry> entries;
  float bias = 0.0f;  // Fixed (non-tunable) score offset (e.g. pawn material).

  void add(int idx, float coeff) {
    if (idx < 0 || coeff == 0.0f) return;
    entries.push_back({static_cast<int16_t>(idx), coeff});
  }
};

// ---------------------------------------------------------------------------
// TrainingPosition — one corpus position with precomputed trace.
// ---------------------------------------------------------------------------

struct TrainingPosition {
  Trace  trace;   // Sparse feature vector.
  double result;  // 1.0 = white win, 0.5 = draw, 0.0 = black win.
};

// ---------------------------------------------------------------------------
// Trace extraction — mirrors evaluatePosition() but records coefficients
// instead of computing a score.  Each tunable parameter used in the
// evaluation produces a (param_index, coefficient) trace entry.
// ---------------------------------------------------------------------------

/// Extract a Trace from one position's bitboard set.
/// Requires the tuning registry to be initialized (buildParamMap must be
/// called first).
Trace extractTrace(const BitboardSet& bb);

/// Build the parameter name->index map from the tuning registry.
/// Must be called once before extractTrace().
void buildParamMap();

/// Look up a parameter index by name.  Returns -1 if not found.
int findParam(const char* name);

// ---------------------------------------------------------------------------
// Parameter descriptor types — metadata for the tuner's registry.
// ---------------------------------------------------------------------------

namespace tuning {

/// Single scalar tunable parameter (material, bonus, penalty, table entry).
struct ScalarParam {
  const char* name;
  int* ptr;
  int min, max, step;
};

/// Nonlinear mobility table pair (MG + EG).  Entry [0] is always fixed at 0.
struct MobilityTableDef {
  const char* prefix;   // e.g. "MOB_KNIGHT"
  int* mgData;
  int* egData;
  int size;             // table length (= max mobility + 1)
  int min, max, step;   // tuning bounds for entries [1..size-1]
};

/// Piece-square table descriptor.
struct PstDef {
  const char* prefix;   // e.g. "PST_PAWN_MG"
  int* data;
  bool isPawn;          // skip rank-1 and rank-8 squares
  int min, max, step;
};

// Parameter descriptor getters — return static arrays of descriptors.
const ScalarParam* scalarParams(int& count);
const MobilityTableDef* mobilityDefs(int& count);
const PstDef* pstDefs(int& count);

// Index-based accessor functions — thin wrappers around the runtime registry.
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
// Eval param extern declarations — external linkage for all eval parameters.
//
// Under TUNING, eval_params.h defines parameters with external linkage
// (EVAL_CONST expands to nothing).  These extern declarations let
// trace.cpp and tune.cpp reference them.  (C++17 inline variables would
// eliminate this block, but the tuner toolchain is GCC 5.1 which lacks
// support.)
// ---------------------------------------------------------------------------

// Material (MG = MATERIAL[], defines centipawn unit; EG separate).
extern int MATERIAL[6];
extern int MATERIAL_EG[6];

// Piece-square tables (12 arrays x 64 squares).
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

// Mobility — nonlinear tables indexed by safe attack count.
extern int MOBILITY_KNIGHT_MG[], MOBILITY_KNIGHT_EG[];
extern int MOBILITY_BISHOP_MG[], MOBILITY_BISHOP_EG[];
extern int MOBILITY_ROOK_MG[], MOBILITY_ROOK_EG[];
extern int MOBILITY_QUEEN_MG[], MOBILITY_QUEEN_EG[];

// King safety.
extern int SHIELD_MISSING_PAWN, SHIELD_ADV_RANK3, SHIELD_ADV_RANK4PLUS, SHIELD_OPEN_FILE;
extern int KING_DANGER_TABLE[13];
extern const int KING_DANGER_WEIGHT[4];

// Space, king distance.
extern int SPACE_BONUS_MG;
extern int PASSER_OWN_KING, PASSER_ENEMY_KING;

// Non-tunable constants.
extern const Bitboard WHITE_SPACE_ZONE, BLACK_SPACE_ZONE;

}  // namespace eval
}  // namespace LibreChess

#endif  // TUNING
#endif  // LIBRECHESS_TRACE_H
