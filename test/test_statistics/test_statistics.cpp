// Search statistics diagnostic suite.
//
// Runs several positions at a fixed search depth and prints formatted
// statistics tables — TT hit rates, cutoff ratios, pruning counts,
// extension counts, and QS/main node ratios.  Informational only:
// no hard assertions (values shift with search tuning).
//
// Requires -DSTATS build flag (automatically set in [env:native]).
//
// Reference: https://www.chessprogramming.org/Search_Statistics

#include <unity.h>

#include <cstdio>

#include <stats.h>

#include "../test_helpers.h"

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

static constexpr int STATS_DEPTH = 10;

static const char* STATS_FENS[] = {
    // Starting position
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    // Kiwipete — complex middlegame
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    // Open tactical middlegame
    "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1BP2/PPPQ2PP/R3K2R w KQ - 4 9",
    // Rook endgame
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    // Dense piece middlegame
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
};
static constexpr int STATS_NPOS = 5;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

static double pct(uint64_t num, uint64_t den) {
  return den > 0 ? 100.0 * num / den : 0.0;
}

// ---------------------------------------------------------------------------
// Test
// ---------------------------------------------------------------------------

static void test_search_statistics(void) {
  SharedTables tables;
  tables.init();

  Position pos;

  for (int p = 0; p < STATS_NPOS; ++p) {
    pos.loadFEN(STATS_FENS[p]);
    tables.clear();

    search::resetStats();

    search::SearchLimits limits;
    limits.maxDepth = STATS_DEPTH;
    search::SearchState state(chronoMillis, &tables.tt, &tables.pawn, &tables.eval);
    search::SearchResult result =
        search::findBestMove(pos, limits, state);

    search::SearchStats s = search::getStats();
    uint64_t totalNodes = s.mainNodes + s.qNodes;

    printf("\n=== Position %d (depth %d, %llu nodes) ===\n",
           p, result.depth, (unsigned long long)totalNodes);

    // TT statistics
    printf("  TT: probes=%llu hits=%llu (%.1f%%) exact=%llu lower=%llu upper=%llu\n",
           (unsigned long long)s.ttProbes,
           (unsigned long long)s.ttHits, pct(s.ttHits, s.ttProbes),
           (unsigned long long)s.ttExactCutoffs,
           (unsigned long long)s.ttLowerCutoffs,
           (unsigned long long)s.ttUpperCutoffs);

    // Cutoff quality
    printf("  Cutoffs: beta=%llu firstMove=%llu (%.1f%%)\n",
           (unsigned long long)s.betaCutoffs,
           (unsigned long long)s.firstMoveCutoffs,
           pct(s.firstMoveCutoffs, s.betaCutoffs));

    // Pruning
    printf("  Prune: NMP=%llu futility=%llu LMP=%llu history=%llu razor=%llu RFP=%llu\n",
           (unsigned long long)s.nullMovePrunes,
           (unsigned long long)s.futilityPrunes,
           (unsigned long long)s.lmpPrunes,
           (unsigned long long)s.historyPrunes,
           (unsigned long long)s.razoringPrunes,
           (unsigned long long)s.rfpPrunes);

    // Reductions
    printf("  LMR: searches=%llu reSearches=%llu (%.1f%% fail-high)\n",
           (unsigned long long)s.lmrSearches,
           (unsigned long long)s.lmrReSearches,
           pct(s.lmrReSearches, s.lmrSearches));

    // Extensions
    printf("  Ext: check=%llu singular=%llu recapture=%llu\n",
           (unsigned long long)s.checkExtensions,
           (unsigned long long)s.singularExtensions,
           (unsigned long long)s.recaptureExtensions);

    // PVS
    printf("  PVS: reSearches=%llu\n",
           (unsigned long long)s.pvsReSearches);

    // Node distribution
    printf("  Nodes: main=%llu (%.1f%%) qs=%llu (%.1f%%)\n",
           (unsigned long long)s.mainNodes, pct(s.mainNodes, totalNodes),
           (unsigned long long)s.qNodes, pct(s.qNodes, totalNodes));
  }

  tables.free();

  TEST_ASSERT_TRUE(true);  // informational — always passes
}

// ===========================================================================
// Entry point
// ===========================================================================

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_search_statistics);
  return UNITY_END();
}
