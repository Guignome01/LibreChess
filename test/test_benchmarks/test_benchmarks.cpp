#include <unity.h>

#include <evaluation.h>
#include <movegen.h>
#include <position.h>
#include <search.h>

#include "../test_helpers.h"

#include <chrono>
#include <cstdio>

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

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint64_t nowUs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch())
          .count());
}

static uint32_t chronoMillis() {
  using namespace std::chrono;
  static const auto epoch = steady_clock::now();
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now() - epoch).count());
}

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
// Entry point
// ===========================================================================

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_bench_make_unmake);
  RUN_TEST(test_bench_make_unmake_ep);
  RUN_TEST(test_bench_evaluate);
  RUN_TEST(test_bench_perft5);
  RUN_TEST(test_bench_search_depth8);

  return UNITY_END();
}
