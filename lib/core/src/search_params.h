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

#include <cmath>
#include <cstdint>

#include "search.h"  // MAX_PLY

namespace LibreChess {
namespace search {
namespace {

// ===========================================================================
// Time control
// ===========================================================================

// Node check interval — every N nodes, poll time and external stop.
constexpr uint32_t CHECK_INTERVAL = 512;

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
// Reference: https://www.chessprogramming.org/Null_Move_Pruning
static constexpr int NMP_DEPTH_THRESHOLD = 3;
static constexpr int NMP_REDUCTION       = 3;

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
// Reference: https://www.chessprogramming.org/Late_Move_Reductions#Base_Reduction
static int8_t LMR_TABLE[MAX_PLY][LMR_MAX_MOVES];
static bool lmrInitialized = false;

static void initLMR() {
  if (lmrInitialized) return;
  for (int d = 0; d < MAX_PLY; ++d) {
    for (int m = 0; m < LMR_MAX_MOVES; ++m) {
      if (d == 0 || m == 0)
        LMR_TABLE[d][m] = 0;
      else
        LMR_TABLE[d][m] = static_cast<int8_t>(std::max(
            1, static_cast<int>(0.75 + std::log(d) * std::log(m) / 2.0)));
    }
  }
  lmrInitialized = true;
}

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
// Evaluation helpers
// ===========================================================================

// Lazy evaluation margin — skip full eval if material score is far from
// the alpha-beta window.
// Reference: https://www.chessprogramming.org/Lazy_Evaluation
static constexpr int LAZY_EVAL_MARGIN = 300;

// Tempo bonus — small bonus for the side to move (initiative).
// Reference: https://www.chessprogramming.org/Tempo
static constexpr int TEMPO_BONUS = 10;

}  // anonymous namespace
}  // namespace search
}  // namespace LibreChess

#endif  // LIBRECHESS_SEARCH_PARAMS_H
