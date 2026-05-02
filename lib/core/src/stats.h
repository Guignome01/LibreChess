// ---------------------------------------------------------------------------
// Search statistics — optional instrumentation for native test builds.
//
// All counters are compiled away in production builds unless -DSTATS is
// defined.  The STAT_INC(field) macro either increments a file-scoped
// global counter or expands to ((void)0).
//
// Public API (guarded by #ifdef STATS):
//   search::resetStats()   — zero all counters
//   search::getStats()     — return a copy of current counters
//
// Instrumentation in search.cpp uses only the STAT_INC() macro —
// no #ifdef blocks are scattered through the search logic.
//
// Reference: https://www.chessprogramming.org/Search_Statistics
// ---------------------------------------------------------------------------

#ifndef LIBRECHESS_STATS_H
#define LIBRECHESS_STATS_H

#include <cstdint>

namespace LibreChess {
namespace search {

// ---------------------------------------------------------------------------
// SearchStats — counters for diagnostic analysis.
// Zero-initialized by resetStats() before each measured search.
// ---------------------------------------------------------------------------

struct SearchStats {
  // --- Transposition Table ---
  uint64_t ttProbes        = 0;  // Total TT lookups
  uint64_t ttHits          = 0;  // Probe returned a valid entry
  uint64_t ttExactCutoffs  = 0;  // EXACT hit caused cutoff
  uint64_t ttLowerCutoffs  = 0;  // LOWER_BOUND narrowed alpha → cutoff
  uint64_t ttUpperCutoffs  = 0;  // UPPER_BOUND narrowed beta → cutoff

  // --- Pruning ---
  uint64_t nullMovePrunes  = 0;  // Null move caused beta cutoff
  uint64_t futilityPrunes  = 0;  // Quiet moves skipped by futility
  uint64_t lmpPrunes       = 0;  // Late quiet moves skipped by LMP
  uint64_t historyPrunes   = 0;  // Quiet moves skipped by history pruning
  uint64_t razoringPrunes  = 0;  // Fell back to QS via razoring
  uint64_t rfpPrunes       = 0;  // Static eval returned via RFP
  uint64_t seeCapPrunes    = 0;  // Captures pruned by SEE in main search

  // --- Reductions ---
  uint64_t lmrSearches     = 0;  // Moves searched with LMR
  uint64_t lmrReSearches   = 0;  // LMR scout failed high → full re-search

  // --- Extensions ---
  uint64_t checkExtensions      = 0;  // Depth extended for check
  uint64_t singularExtensions   = 0;  // TT move confirmed singular
  uint64_t recaptureExtensions  = 0;  // Recapture on same square

  // --- PVS ---
  uint64_t pvsReSearches   = 0;  // Zero-window scout failed high → full window

  // --- Node counts ---
  uint64_t mainNodes       = 0;  // Nodes in negamax
  uint64_t qNodes          = 0;  // Nodes in quiescence

  // --- Cutoffs ---
  uint64_t betaCutoffs        = 0;  // Total beta cutoffs in negamax
  uint64_t firstMoveCutoffs   = 0;  // Beta cutoff on the first move searched
};

// ---------------------------------------------------------------------------
// Macro interface — compiled away without -DSTATS.
// Usage in search.cpp:  STAT_INC(ttProbes);
// ---------------------------------------------------------------------------

#ifdef STATS

/// File-scoped stats instance — defined in search.cpp.
extern SearchStats g_stats;

#define STAT_INC(field) (::LibreChess::search::g_stats.field++)

#else  // !STATS

#define STAT_INC(field) ((void)0)

#endif  // STATS

/// Reset all counters to zero (no-op without -DSTATS).
void resetStats();

/// Return a snapshot of the current counters (zeros without -DSTATS).
SearchStats getStats();

}  // namespace search
}  // namespace LibreChess

#endif  // LIBRECHESS_STATS_H
