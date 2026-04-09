// Node count and evaluation regression tests.
//
// Detects unintended changes to search behavior (node regression) or
// evaluation (score drift) by comparing against calibrated baselines.
//
// - Node count: 10 positions searched at fixed depth, total nodes compared
//   against a baseline with 15% tolerance.
// - Eval: 15 positions, static evaluation compared for exact match.
//
// Baselines are hardcoded constants — recalibrate after intentional changes
// to search or evaluation.

#include <unity.h>

#include <climits>
#include <cstdio>

#include "../test_helpers.h"

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

/// Search depth for node count regression.
static constexpr int NODE_DEPTH = 10;

/// Maximum allowed total node count (calibrated baseline × 1.15).
/// Set to 0 to disable assertion (calibration mode).
static constexpr uint64_t NODE_BASELINE = 3054593;

// ---------------------------------------------------------------------------
// Node count regression — 10 diverse positions at fixed depth
// ---------------------------------------------------------------------------

static const char* NODE_FENS[] = {
    // Opening
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    // Sicilian
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",
    // Complex middlegame (Kiwipete)
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    // Open middlegame
    "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1BP2/PPPQ2PP/R3K2R w KQ - 4 9",
    // Tactical middlegame
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    // Rook endgame
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    // Queen vs pawns
    "8/8/8/8/8/4k3/4P3/4K2Q w - - 0 1",
    // Knight endgame
    "8/5k2/4n3/8/3N4/8/5K2/8 w - - 0 1",
    // Passed pawn race
    "8/1P4k1/8/8/8/8/1p4K1/8 w - - 0 1",
    // Bishop pair middlegame
    "r2q1rk1/ppp2ppp/2n1bn2/3p4/3P4/2NBBN2/PPP2PPP/R2Q1RK1 w - - 4 9",
};
static constexpr int NODE_NPOS = 10;

static void test_node_count_regression(void) {
  SharedTables tables;
  tables.init();

  uint64_t totalNodes = 0;
  Position pos;

  printf("\n--- Node count regression (depth %d) ---\n", NODE_DEPTH);

  for (int i = 0; i < NODE_NPOS; ++i) {
    pos.loadFEN(NODE_FENS[i]);
    tables.clear();

    search::SearchLimits limits;
    limits.maxDepth = NODE_DEPTH;
    search::SearchState state(chronoMillis, &tables.tt, &tables.pawn, &tables.eval);
    search::SearchResult result =
        search::findBestMove(pos, limits, state);
    totalNodes += result.nodes;
    printf("  pos %2d: %8u nodes\n", i, result.nodes);
  }

  printf("  TOTAL: %llu nodes\n", (unsigned long long)totalNodes);

  tables.free();

  // Hard gate — node count must not regress beyond tolerance
  if (NODE_BASELINE > 0) {
    uint64_t threshold = static_cast<uint64_t>(NODE_BASELINE * 1.15);
    TEST_ASSERT_MESSAGE(
        totalNodes <= threshold,
        "Node count regression: total nodes exceeds 115% of baseline");
  }
}

// ---------------------------------------------------------------------------
// Eval regression — 15 diverse positions, exact match
// ---------------------------------------------------------------------------

struct EvalTestCase {
  const char* fen;
  int expected;  // Expected eval from white's perspective (side-to-move adj)
};

/// Calibrated eval baselines — recalibrate after intentional eval changes.
/// Set expected to UNCALIBRATED and run once to get actual values.
static constexpr int UNCALIBRATED = INT_MIN;
static const EvalTestCase EVAL_CASES[] = {
    // Opening
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0},
    // Sicilian
    {"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", 35},
    // Kiwipete
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
     0},
    // Italian
    {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
     2},
    // Complex middlegame
    {"r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1BP2/PPPQ2PP/R3K2R w KQ - 4 9", -49},
    // Tactical
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
     0},
    // Rook endgame
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", -25},
    // Bishop pair
    {"r2q1rk1/ppp2ppp/2n1bn2/3p4/3P4/2NBBN2/PPP2PPP/R2Q1RK1 w - - 4 9", 467},
    // Queen+pawn vs queen
    {"6k1/5ppp/8/8/8/8/1Q3PPP/6K1 w - - 0 1", 1047},
    // Knight outpost
    {"r1bqkb1r/pp3ppp/2n1pn2/2ppN3/3P4/2N5/PPP1PPPP/R1BQKB1R w KQkq - 0 6",
     -2},
    // Opposite-color bishops
    {"8/5k2/4b3/8/8/3B4/5K2/8 w - - 0 1", 6},
    // Rook on open file
    {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", 0},
    // Pawn structure (isolated)
    {"8/pp2pp2/2p2p2/8/8/2P2P2/PP2PP2/8 w - - 0 1", 0},
    // Passed pawn
    {"8/8/8/3P4/8/8/5k2/4K3 w - - 0 1", 127},
    // Material imbalance
    {"r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2", 14},
};
static constexpr int EVAL_NPOS = 15;

static void test_eval_regression(void) {
  Position pos;
  eval::PawnHashTable pawnHash;
  pawnHash.resize(eval::DEFAULT_PAWN_HASH_SIZE);

  printf("\n--- Eval regression (%d positions) ---\n", EVAL_NPOS);

  bool allCalibrated = true;
  int mismatches = 0;

  for (int i = 0; i < EVAL_NPOS; ++i) {
    pos.loadFEN(EVAL_CASES[i].fen);
    pawnHash.clear();

    int score = eval::evaluatePosition(pos.bitboards(), pos.mgPST(),
                                       pos.egPST(), pos.phase(), &pawnHash);
    // Adjust for side to move (eval returns side-to-move relative)
    if (pos.sideToMove() == Color::BLACK) score = -score;

    if (EVAL_CASES[i].expected == UNCALIBRATED) {
      // Uncalibrated — print value for calibration
      allCalibrated = false;
      printf("  pos %2d: score=%d (UNCALIBRATED)\n", i, score);
    } else {
      printf("  pos %2d: score=%d expected=%d %s\n", i, score,
             EVAL_CASES[i].expected,
             (score == EVAL_CASES[i].expected) ? "OK" : "MISMATCH");
      if (score != EVAL_CASES[i].expected) ++mismatches;
    }
  }

  pawnHash.free();

  if (!allCalibrated) {
    printf("  WARNING: some positions uncalibrated — run once to get values\n");
  }

  if (allCalibrated) {
    TEST_ASSERT_EQUAL_MESSAGE(0, mismatches,
                              "Eval regression: score mismatch detected");
  }
}

// ===========================================================================
// Registration
// ===========================================================================

void register_regression_tests() {
  RUN_TEST(test_node_count_regression);
  RUN_TEST(test_eval_regression);
}
