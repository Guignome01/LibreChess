#ifndef LIBRECHESS_SEARCH_PARAMS_H
#define LIBRECHESS_SEARCH_PARAMS_H

// ---------------------------------------------------------------------------
// Search parameters — all tunable constants for the search algorithm.
//
// This header is an implementation detail of search.cpp, not a public API.
// It isolates parameter data (margins, thresholds, tables) from search logic
// (negamax, quiescence, move ordering), making search tuning knobs easy to
// find and modify in one place.
//
// All constants are static constexpr (file-local, no link overhead).  The
// LMR table is the only mutable state — it requires lazy initialization
// via initLMR().
//
// Reference: https://www.chessprogramming.org/Search
// ---------------------------------------------------------------------------

#include <cstdint>

#include "search.h"  // MAX_PLY

namespace LibreChess {
namespace search {
namespace {

// ===========================================================================
// Quiescence search
// ===========================================================================

// Maximum QS depth.  Prevents stack overflow from deep check-evasion
// chains on ESP32's bounded FreeRTOS task stack.
// Reference: https://www.chessprogramming.org/Quiescence_Search
static constexpr int MAX_QS_DEPTH = 16;

// Delta pruning margin — safety buffer for capture futility in QS.
// Reference: https://www.chessprogramming.org/Delta_Pruning
static constexpr int DELTA_MARGIN = 200;

// ===========================================================================
// Null Move Pruning
// ===========================================================================

// NMP_DEPTH_THRESHOLD: minimum remaining depth to attempt null move.
// NMP_REDUCTION: base depth reduction for the null-move search (R).
// NMP_DEPTH_DIVISOR: depth/N term in adaptive R formula.
// NMP_EVAL_DIVISOR: centipawns per extra R unit from (staticEval - beta).
// NMP_EVAL_BONUS_CAP: upper bound on the eval-surplus contribution to R.
// Reference: https://www.chessprogramming.org/Null_Move_Pruning
// Reference: https://www.chessprogramming.org/Null_Move_Pruning#Adaptive
static constexpr int NMP_DEPTH_THRESHOLD = 3;
static constexpr int NMP_REDUCTION       = 3;
static constexpr int NMP_DEPTH_DIVISOR   = 4;
static constexpr int NMP_EVAL_DIVISOR    = 200;
static constexpr int NMP_EVAL_BONUS_CAP  = 3;

// ===========================================================================
// Late Move Reductions
// ===========================================================================

// LMR_FULL_DEPTH_MOVES: moves searched at full depth before reducing.
// LMR_DEPTH_THRESHOLD: minimum depth to apply LMR.
// LMR_MAX_MOVES: table width (move index cap).
// Reference: https://www.chessprogramming.org/Late_Move_Reductions
static constexpr int LMR_FULL_DEPTH_MOVES = 4;
static constexpr int LMR_DEPTH_THRESHOLD  = 3;
static constexpr int LMR_MAX_MOVES        = 64;

// Logarithmic LMR reduction table — precomputed base reduction by
// [depth][moveIndex].  Formula: max(1, int(0.75 + ln(d)*ln(m) / 2.0)).
//
// Built at compile time via a constexpr struct.  The constexpr natural
// log uses decomposition ln(x) = k*ln(2) + ln(1+f) with a 6th-order
// Taylor series for ln(1+f), accurate to ±0.0001 for x ∈ [1,64] —
// more than sufficient for integer truncation.
//
// Reference: https://www.chessprogramming.org/Late_Move_Reductions#Base_Reduction

// Constexpr natural log approximation for positive integers.
// Uses the identity ln(1+f) = 2·atanh(f/(f+2)) with a rapidly converging
// series.  For f ∈ [0,1), t = f/(f+2) ∈ [0,1/3), so 9 terms yield
// accuracy better than 1e-10 — far exceeding int-truncation needs.
static constexpr double cxLn2 = 0.6931471805599453;

static constexpr double cxLn(int x) {
  // Decompose: x = 2^k * (1+f), f ∈ [0,1)
  int k = 0;
  int pow2 = 1;
  while (pow2 * 2 <= x) { pow2 *= 2; ++k; }
  double f = static_cast<double>(x) / pow2 - 1.0;
  // ln(1+f) = 2·atanh(t) where t = f/(f+2), t ∈ [0, 1/3)
  // atanh(t) = t + t³/3 + t⁵/5 + t⁷/7 + ...
  double t = f / (f + 2.0);
  double t2 = t * t;
  double term = t;
  double sum = t;
  for (int i = 3; i <= 19; i += 2) {
    term *= t2;
    sum += term / i;
  }
  return k * cxLn2 + 2.0 * sum;
}

struct LMRTable {
  int8_t data[MAX_PLY][LMR_MAX_MOVES]{};
  constexpr LMRTable() {
    for (int d = 0; d < MAX_PLY; ++d) {
      for (int m = 0; m < LMR_MAX_MOVES; ++m) {
        if (d == 0 || m == 0)
          data[d][m] = 0;
        else {
          int val = static_cast<int>(0.75 + cxLn(d) * cxLn(m) / 2.0);
          data[d][m] = static_cast<int8_t>(val < 1 ? 1 : val);
        }
      }
    }
  }
};

static constexpr LMRTable LMR_TABLE{};

// ===========================================================================
// Aspiration Windows
// ===========================================================================

// Initial half-width of the aspiration window (centipawns).
// Reference: https://www.chessprogramming.org/Aspiration_Windows
static constexpr int ASPIRATION_DELTA = 50;

// ===========================================================================
// Futility Pruning
// ===========================================================================

// Depth-indexed margin.  At depth 1-2, quiet moves are skipped if
// staticEval + margin < alpha.
// Reference: https://www.chessprogramming.org/Futility_Pruning
static constexpr int FUTILITY_MARGIN[] = {0, 200, 500};  // indexed by depth

// ===========================================================================
// Late Move Pruning
// ===========================================================================

// At shallow depths, skip remaining quiet moves after this many searched.
// Reference: https://www.chessprogramming.org/Late_Move_Pruning
static constexpr int LMP_THRESHOLD[] = {0, 5, 12, 20, 30, 42};  // indexed by depth

// ===========================================================================
// History Pruning
// ===========================================================================

// Skip quiet moves with deeply negative history at shallow depths.
// Reference: https://www.chessprogramming.org/History_Leaf_Pruning
static constexpr int HISTORY_PRUNE_DEPTH     = 4;
static constexpr int HISTORY_PRUNE_THRESHOLD = 1024;

// ===========================================================================
// SEE Capture Pruning (Main Search)
// ===========================================================================

// At non-PV, non-check nodes, prune captures with deeply negative SEE.
// Threshold: SEE < -SEE_CAPTURE_PRUNE_MARGIN × depth.
// Reference: https://www.chessprogramming.org/Static_Exchange_Evaluation
static constexpr int SEE_CAPTURE_PRUNE_MARGIN = 20;

// ===========================================================================
// Razoring
// ===========================================================================

// At shallow depths, drop to QS if static eval + margin < alpha.
// Reference: https://www.chessprogramming.org/Razoring
static constexpr int RAZOR_MARGIN[] = {0, 300, 500};  // indexed by depth

// ===========================================================================
// Reverse Futility Pruning (Static Null Move Pruning)
// ===========================================================================

// Per-depth margin.  Prune if staticEval - margin*depth >= beta.
// Reference: https://www.chessprogramming.org/Reverse_Futility_Pruning
static constexpr int RFP_MARGIN = 120;  // per depth

// ===========================================================================
// Internal Iterative Reductions (IIR)
// ===========================================================================

// Minimum depth to trigger IIR at PV nodes without a TT move.
// Reference: https://www.chessprogramming.org/Internal_Iterative_Reductions
static constexpr int IID_DEPTH_THRESHOLD = 4;

// ===========================================================================
// Singular Extensions
// ===========================================================================

// SE_DEPTH_THRESHOLD: minimum depth to trigger singular extension.
// SE_MARGIN_SCALE: singularBeta = ttScore - SE_MARGIN_SCALE * depth.
// Reference: https://www.chessprogramming.org/Singular_Extensions
static constexpr int SE_DEPTH_THRESHOLD = 6;
static constexpr int SE_MARGIN_SCALE    = 2;

// ===========================================================================
// Pawn Endgame Extension
// ===========================================================================

// Extend when a capture transitions to a pure pawn endgame (phase drops
// to 0).  Pawn endgames are highly tactical — zugzwang, opposition, and
// tempo dominate — so deeper search is critical for accuracy.
// Reference: https://www.chessprogramming.org/Pawn_Endgame
static constexpr int PAWN_ENDGAME_EXTENSION = 2;

// ===========================================================================
// Evaluation helpers
// ===========================================================================

// Lazy evaluation margin — skip full eval if material score is far from
// the alpha-beta window.
// Reference: https://www.chessprogramming.org/Lazy_Evaluation
static constexpr int LAZY_EVAL_MARGIN = 300;

// ===========================================================================
// Time Management — Easy Move & Instability
// ===========================================================================

// Easy move: stop iterative deepening early when the best move has been
// stable for many iterations and leads the next candidate comfortably.
//   EASY_MOVE_STABLE_DEPTH: required consecutive iterations with the same
//                           best move.
//   EASY_MOVE_MIN_DEPTH   : minimum iteration depth before the heuristic
//                           may trigger (avoid shallow false positives).
//   EASY_MOVE_MARGIN      : required lead (cp) over the second-best score.
// Reference: https://www.chessprogramming.org/Time_Management#Easy_Move
static constexpr int EASY_MOVE_STABLE_DEPTH = 4;
static constexpr int EASY_MOVE_MIN_DEPTH    = 6;
static constexpr int EASY_MOVE_MARGIN       = 100;

// Instability extension: when the best root move changes, scale the
// effective soft time by factor% = BASE + STEP × bestMoveChanges,
// capped at CAP.  More changes → more thinking time, up to the hard
// limit.  Factors are in percent (e.g. 150 = 1.5×).
// Reference: https://www.chessprogramming.org/Time_Management
static constexpr int INSTABILITY_FACTOR_BASE = 150;
static constexpr int INSTABILITY_FACTOR_STEP = 25;
static constexpr int INSTABILITY_FACTOR_CAP  = 250;

// ===========================================================================
// LMR History Thresholds
// ===========================================================================

// History-informed LMR adjustments (see computeLMRReduction):
//   hist < LMR_BAD_HIST_THRESHOLD  → reduce more (+1)
//   hist > LMR_GOOD_HIST_THRESHOLD → reduce less (−1)
// Reference: https://www.chessprogramming.org/Late_Move_Reductions#Heuristic_Reductions
static constexpr int LMR_BAD_HIST_THRESHOLD  = -500;
static constexpr int LMR_GOOD_HIST_THRESHOLD = 1500;

}  // anonymous namespace
}  // namespace search
}  // namespace LibreChess

#endif  // LIBRECHESS_SEARCH_PARAMS_H
