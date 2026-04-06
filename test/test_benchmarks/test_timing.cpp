#include <unity.h>

#include <cstdio>

#include <attacks.h>

#include "../test_helpers.h"

using namespace LibreChess;

// ===========================================================================
// Micro-benchmarks for hot-path functions.
//
// These tests measure wall-clock time to establish baselines and verify
// that optimizations produce measurable improvements.  Not assertions —
// they print timing results for manual comparison.
//
// Run:  pio test -e native -f test_benchmarks
// ===========================================================================

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Recursive perft (node counting only — used as integrated make/unmake
// + movegen stress test).
static uint64_t perft(Position& pos, int depth) {
  if (depth == 0) return 1;
  uint64_t nodes = 0;
  MoveList moves;
  movegen::generateAllMoves(pos.bitboards(), pos.mailbox(),
                            pos.currentTurn(), pos.positionState(), moves);
  for (int i = 0; i < moves.count; i++) {
    UndoInfo undo = pos.make(moves.moves[i]);
    nodes += perft(pos, depth - 1);
    pos.unmake(moves.moves[i], undo);
  }
  return nodes;
}

// ===========================================================================
// Benchmark: make/unmake round-trip
// ===========================================================================

static void test_bench_make_unmake(void) {
  Position pos;

  // Middlegame position with many legal moves including EP opportunity
  pos.loadFEN("r1bqkbnr/pppppppp/2n5/4P3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2");

  MoveList moves;
  movegen::generateAllMoves(pos.bitboards(), pos.mailbox(),
                            pos.currentTurn(), pos.positionState(), moves);
  TEST_ASSERT_GREATER_THAN(0, moves.count);

  constexpr int ITERS = 200000;
  uint64_t start = nowUs();

  for (int i = 0; i < ITERS; i++) {
    Move m = moves.moves[i % moves.count];
    UndoInfo undo = pos.make(m);
    pos.unmake(m, undo);
  }

  uint64_t elapsed = nowUs() - start;
  double nsPerOp = (elapsed * 1000.0) / ITERS;
  printf("  make/unmake: %d iters in %llu us (%.0f ns/roundtrip)\n",
         ITERS, (unsigned long long)elapsed, nsPerOp);

  TEST_ASSERT_TRUE(true);  // timing test — always passes
}

// ===========================================================================
// Benchmark: make/unmake with EP positions
// ===========================================================================

static void test_bench_make_unmake_ep(void) {
  Position pos;

  // Position with active EP square — stresses hasLegalEnPassantCapture
  pos.loadFEN(
      "rnbqkbnr/pppp1ppp/8/4pP2/8/8/PPPPP1PP/RNBQKBNR w KQkq e6 0 3");

  MoveList moves;
  movegen::generateAllMoves(pos.bitboards(), pos.mailbox(),
                            pos.currentTurn(), pos.positionState(), moves);
  TEST_ASSERT_GREATER_THAN(0, moves.count);

  constexpr int ITERS = 200000;
  uint64_t start = nowUs();

  for (int i = 0; i < ITERS; i++) {
    Move m = moves.moves[i % moves.count];
    UndoInfo undo = pos.make(m);
    pos.unmake(m, undo);
  }

  uint64_t elapsed = nowUs() - start;
  double nsPerOp = (elapsed * 1000.0) / ITERS;
  printf("  make/unmake (EP): %d iters in %llu us (%.0f ns/roundtrip)\n",
         ITERS, (unsigned long long)elapsed, nsPerOp);

  TEST_ASSERT_TRUE(true);
}

// ===========================================================================
// Benchmark: evaluatePosition (search overload with incremental PST)
// ===========================================================================

static void test_bench_evaluate(void) {
  Position pos;

  // Complex middlegame position
  pos.loadFEN(
      "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1BP2/PPPQ2PP/R3K2R w KQ - 4 9");

  eval::PawnHashTable pawnHash;

  constexpr int ITERS = 100000;
  volatile int dummy = 0;  // prevent optimization

  uint64_t start = nowUs();

  for (int i = 0; i < ITERS; i++) {
    int score = eval::evaluatePosition(pos.bitboards(), pos.mgPST(),
                                       pos.egPST(), &pawnHash);
    dummy += score;
  }

  uint64_t elapsed = nowUs() - start;
  double nsPerOp = (elapsed * 1000.0) / ITERS;
  printf("  evaluatePosition: %d iters in %llu us (%.0f ns/call)\n",
         ITERS, (unsigned long long)elapsed, nsPerOp);

  TEST_ASSERT_TRUE(true);
}

// ===========================================================================
// Benchmark: perft(startpos, 5) — integrated make/unmake + movegen
// ===========================================================================

static void test_bench_perft5(void) {
  Position pos;
  pos.newGame();

  uint64_t start = nowUs();
  uint64_t nodes = perft(pos, 5);
  uint64_t elapsed = nowUs() - start;

  double mnps = (nodes / 1000000.0) / (elapsed / 1000000.0);
  printf("  perft(5): %llu nodes in %llu us (%.2f Mnps)\n",
         (unsigned long long)nodes, (unsigned long long)elapsed, mnps);

  TEST_ASSERT_EQUAL_UINT64(4865609, nodes);
}

// ===========================================================================
// Benchmark: engine search (depth 8, standard starting position)
// ===========================================================================

static void test_bench_search_depth8(void) {
  Position pos;
  pos.newGame();

  search::SearchLimits limits;
  limits.maxDepth = 8;

  uint64_t start = nowUs();
  search::SearchResult result =
      search::findBestMove(pos, limits, chronoMillis);
  uint64_t elapsed = nowUs() - start;

  double knps = (result.nodes / 1000.0) / (elapsed / 1000000.0);
  printf("  search(d8): %u nodes in %llu us (%.0f knps), score=%d\n",
         result.nodes,
         (unsigned long long)elapsed, knps, result.score);

  TEST_ASSERT_TRUE(true);
}

// ===========================================================================
// Benchmark: multi-position evaluatePosition
//
// Averages eval time across diverse positions to reduce single-position bias.
// ===========================================================================

static void test_bench_evaluate_multi(void) {
  static const char* FENS[] = {
    "r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2",
    "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1BP2/PPPQ2PP/R3K2R w KQ - 4 9",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
  };
  constexpr int NPOS = 5;
  constexpr int ITERS_PER_POS = 50000;

  Position pos;
  eval::PawnHashTable pawnHash;
  volatile int dummy = 0;

  uint64_t totalUs = 0;
  for (int p = 0; p < NPOS; ++p) {
    pos.loadFEN(FENS[p]);
    pawnHash.clear();
    uint64_t start = nowUs();
    for (int i = 0; i < ITERS_PER_POS; i++) {
      dummy += eval::evaluatePosition(pos.bitboards(), pos.mgPST(),
                                      pos.egPST(), &pawnHash);
    }
    totalUs += nowUs() - start;
  }

  int totalIters = NPOS * ITERS_PER_POS;
  double nsPerOp = (totalUs * 1000.0) / totalIters;
  printf("  evaluate (5 pos): %d iters in %llu us (%.0f ns/call avg)\n",
         totalIters, (unsigned long long)totalUs, nsPerOp);

  TEST_ASSERT_TRUE(true);
}

// ===========================================================================
// Benchmark: bishop() attack generation
//
// Measures Hyperbola Quintessence diagonal lookups in isolation —
// the function that will be affected by diagonal mask compaction.
// ===========================================================================

static void test_bench_bishop_attacks(void) {
  // Build a set of varied (square, occupancy) pairs for realistic testing.
  // Use a simple PRNG to generate non-trivial occupancies.
  constexpr int ITERS = 500000;
  attacks::init();

  // Precompute random occupancies with a fast xorshift32.
  static uint64_t occs[64];
  uint32_t rng = 0xDEADBEEF;
  for (int i = 0; i < 64; ++i) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    uint64_t hi = rng;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    occs[i] = (hi << 32) | rng;
  }

  volatile uint64_t dummy = 0;
  uint64_t start = nowUs();
  for (int i = 0; i < ITERS; ++i) {
    Square sq = static_cast<Square>(i & 63);
    dummy |= attacks::bishop(sq, occs[sq]);
  }
  uint64_t elapsed = nowUs() - start;

  double nsPerOp = (elapsed * 1000.0) / ITERS;
  printf("  bishop(): %d iters in %llu us (%.1f ns/call)\n",
         ITERS, (unsigned long long)elapsed, nsPerOp);

  TEST_ASSERT_TRUE(true);
}

// ===========================================================================
// Benchmark: multi-position search depth 8
//
// Averages search performance across 3 different position types
// (opening, middlegame, endgame) to reduce variance.
// ===========================================================================

static void test_bench_search_multi(void) {
  static const char* FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1BP2/PPPQ2PP/R3K2R w KQ - 4 9",
    "8/5k2/8/8/8/4K3/4P3/8 w - - 0 1",
  };
  constexpr int NPOS = 3;

  Position pos;

  uint64_t totalNodes = 0;
  uint64_t totalUs = 0;

  for (int p = 0; p < NPOS; ++p) {
    pos.loadFEN(FENS[p]);

    search::SearchLimits limits;
    limits.maxDepth = 8;

    uint64_t start = nowUs();
    search::SearchResult result =
        search::findBestMove(pos, limits, chronoMillis);
    uint64_t elapsed = nowUs() - start;

    totalNodes += result.nodes;
    totalUs += elapsed;

    printf("    pos %d: %u nodes, %llu us, score=%d\n",
           p, result.nodes, (unsigned long long)elapsed, result.score);
  }

  double knps = (totalNodes / 1000.0) / (totalUs / 1000000.0);
  printf("  search(d8, 3 pos): %llu nodes in %llu us (%.0f knps avg)\n",
         (unsigned long long)totalNodes, (unsigned long long)totalUs, knps);

  TEST_ASSERT_TRUE(true);
}

// ===========================================================================
// Registration
// ===========================================================================

void register_timing_tests() {
  RUN_TEST(test_bench_make_unmake);
  RUN_TEST(test_bench_make_unmake_ep);
  RUN_TEST(test_bench_evaluate);
  RUN_TEST(test_bench_evaluate_multi);
  RUN_TEST(test_bench_bishop_attacks);
  RUN_TEST(test_bench_perft5);
  RUN_TEST(test_bench_search_depth8);
  RUN_TEST(test_bench_search_multi);
}
