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
  float bias = 0.0f;   // Fixed (non-tunable) score offset (e.g. pawn material).
  bool  hasOCB = false; // Opposite-color bishop scaling (applied post-hoc).

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
};

/// Piece-square table descriptor.
struct PstDef {
  const char* prefix;   // e.g. "PST_PAWN_MG"
  int* data;
  bool isPawn;          // skip rank-1 and rank-8 squares
};

// Parameter descriptor getters — return static arrays of descriptors.
const ScalarParam* scalarParams(int& count);
const PstDef* pstDefs(int& count);

// Index-based accessor functions — thin wrappers around the runtime registry.
int paramCount();
const char* getName(int idx);
int getValue(int idx);
void setValue(int idx, int val);
int getDefault(int idx);

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
extern int ISOLATED_PENALTY_MG, ISOLATED_PENALTY_EG;
extern int DOUBLED_PENALTY_EG;
extern int BACKWARD_PENALTY_MG, BACKWARD_PENALTY_EG;
extern int CONNECTED_BONUS_MG[8], CONNECTED_BONUS_EG[8];
extern int CANDIDATE_PASSED_MG[8], CANDIDATE_PASSED_EG[8];
extern int PROTECTED_PASSER_MG, PROTECTED_PASSER_EG;
extern int PASSER_OWN_KING_DIST_EG, PASSER_ENEMY_KING_DIST_EG;

// Piece bonuses.
extern int BISHOP_PAIR_MG, BISHOP_PAIR_EG;
extern int ROOK_OPEN_FILE_MG, ROOK_OPEN_FILE_EG;
extern int ROOK_SEMI_OPEN_FILE_MG, ROOK_SEMI_OPEN_FILE_EG;
extern int ROOK_7TH_MG, ROOK_7TH_EG;
extern int ROOK_BEHIND_OWN_PASSER_EG, ROOK_BEHIND_ENEMY_PASSER_EG;
extern int BAD_BISHOP_MG, BAD_BISHOP_EG;
extern int OUTPOST_BONUS_MG, OUTPOST_BONUS_EG;
extern int BISHOP_OUTPOST_MG, BISHOP_OUTPOST_EG;

// Threats.
extern int THREAT_BY_PAWN_MG, THREAT_BY_PAWN_EG;
extern int THREAT_BY_MINOR_MG, THREAT_BY_MINOR_EG;
extern int THREAT_BY_ROOK_MG, THREAT_BY_ROOK_EG;
extern int THREAT_HANGING_MG, THREAT_HANGING_EG;

// Mobility — nonlinear lookup tables.
extern int MOB_KNIGHT_MG[9],  MOB_KNIGHT_EG[9];
extern int MOB_BISHOP_MG[14], MOB_BISHOP_EG[14];
extern int MOB_ROOK_MG[15],   MOB_ROOK_EG[15];
extern int MOB_QUEEN_MG[28],  MOB_QUEEN_EG[28];

// King safety.
extern int SHIELD_RANK[4], SHIELD_OPEN_FILE;
extern int PAWN_STORM[8];
extern int SAFE_CHECK_KNIGHT, SAFE_CHECK_BISHOP, SAFE_CHECK_ROOK, SAFE_CHECK_QUEEN;

// Space.
extern int SPACE_WEIGHT;

}  // namespace eval
}  // namespace LibreChess

#endif  // TUNING
#endif  // LIBRECHESS_TRACE_H
