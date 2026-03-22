#ifndef LIBRECHESS_TRACE_H
#define LIBRECHESS_TRACE_H

// ---------------------------------------------------------------------------
// Trace — sparse feature vector for one position.
//
// Each entry records (param_index, coefficient).  The evaluation score for
// a position is the dot product:  score = Σ θ[entry.idx] × entry.coeff
//
// Stored once per corpus position alongside the game result.  Used by the
// Adam optimizer to compute analytical gradients without re-evaluating.
//
// Only available in tuning builds (-DTUNING).
//
// Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method
// ---------------------------------------------------------------------------

#ifdef TUNING

#include <cstdint>
#include <vector>

#include "bitboard.h"

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
    if (coeff == 0.0f) return;
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

/// Build the parameter name→index map from the tuning registry.
/// Must be called once before extractTrace().
void buildParamMap();

/// Look up a parameter index by name.  Returns -1 if not found.
int findParam(const char* name);

}  // namespace eval
}  // namespace LibreChess

#endif  // TUNING

#endif  // LIBRECHESS_TRACE_H
