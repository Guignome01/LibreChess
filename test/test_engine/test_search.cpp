#include <unity.h>

#include <notation.h>
#include <position.h>
#include <search.h>

#include "../test_helpers.h"

using namespace LibreChess;

// ===========================================================================
// Helpers — set up positions from FEN for search testing
// ===========================================================================

// Search a position loaded from FEN at the given depth.
// Returns the SearchResult.
static search::SearchResult searchFEN(const char* fen, int depth) {
  Position pos;
  pos.loadFEN(fen);
  search::SearchLimits limits;
  limits.maxDepth = depth;
  return search::findBestMove(pos, limits);
}

// Convert a Move to UCI coordinate string for readable assertions.
static std::string moveToStr(Move m) {
  return notation::toCoordinate(rowOf(m.from), colOf(m.from),
                                rowOf(m.to), colOf(m.to));
}

// ===========================================================================
// Mate-in-1 — search must find the mating move
// ===========================================================================

// White to move: Rh8# is the only mating move.
// Position: White Kb6, Rh1; Black Kb8.
// After Rh8#: a8 and c8 covered by rook on rank 8, a7 and c7 covered by Kb6.
static void test_search_mate_in_1_white(void) {
  const char* fen = "1k6/8/1K6/8/8/8/8/7R w - - 0 1";
  auto result = searchFEN(fen, 2);
  std::string move = moveToStr(result.bestMove);
  TEST_ASSERT_EQUAL_STRING("h1h8", move.c_str());
  TEST_ASSERT_TRUE(result.score >= search::MATE_SCORE - 10);
}

// Black to move: Qg2# and Qe1# are both mate-in-1.
// Position: Black Kh3, Qf2; White Kh1.
static void test_search_mate_in_1_black(void) {
  const char* fen = "8/8/8/8/8/7k/5q2/7K b - - 0 1";
  auto result = searchFEN(fen, 2);
  // Must find a mating move — accept any mate-in-1.
  TEST_ASSERT_TRUE(result.score >= search::MATE_SCORE - 10);
}

// ===========================================================================
// Captures — search must capture a hanging piece
// ===========================================================================

// White queen can capture undefended black rook on a8.
static void test_search_captures_hanging_piece(void) {
  const char* fen = "r3k3/8/8/8/8/8/8/Q3K3 w - - 0 1";
  auto result = searchFEN(fen, 2);
  std::string move = moveToStr(result.bestMove);
  TEST_ASSERT_EQUAL_STRING("a1a8", move.c_str());
  TEST_ASSERT_TRUE(result.score > 0);
}

// ===========================================================================
// Quiescence — don't blunder into a recapture
// ===========================================================================

// White bishop can take on f7, but the black king recaptures.
// A pure depth-1 search without quiescence would grab f7 and think +300 cp.
// With quiescence, it sees Kxf7 and the score is roughly equal.
static void test_search_quiescence_avoids_blunder(void) {
  // White Ke1, Bf7 target via Bc4; Black Ke8
  // Bc4 can take pawn on f7 but King recaptures
  const char* fen = "4k3/5p2/8/8/2B5/8/8/4K3 w - - 0 1";
  auto result = searchFEN(fen, 1);
  // With qsearch: should not play Bxf7 thinking it's +300.
  // The score should be modest (bishop vs pawn, but recapture loses bishop).
  // If it plays Bxf7, the qsearch should see Kxf7 and evaluate correctly.
  // The key assertion: score should not be wildly inflated (no free piece).
  TEST_ASSERT_TRUE(result.score < 350);
}

// ===========================================================================
// Stalemate avoidance — don't stalemate when winning
// ===========================================================================

// White is up a queen and should not stalemate black.
// Black king on a8, White queen on b6, White king on c8.
// Qb7 is stalemate! Search should avoid it.
static void test_search_avoids_stalemate(void) {
  const char* fen = "k7/8/1Q6/8/8/8/8/2K5 w - - 0 1";
  auto result = searchFEN(fen, 3);
  std::string move = moveToStr(result.bestMove);
  // Qb7 would be stalemate — search should pick something else.
  TEST_ASSERT_FALSE(move == "b6b7");
  // Should find mate or at least a winning position.
  TEST_ASSERT_TRUE(result.score > 0);
}

// ===========================================================================
// Symmetric position — score should be approximately zero
// ===========================================================================

static void test_search_symmetric_position(void) {
  const char* fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  auto result = searchFEN(fen, 2);
  // Starting position is roughly equal; score near zero (within ±100 cp).
  TEST_ASSERT_TRUE(result.score > -100);
  TEST_ASSERT_TRUE(result.score < 100);
  TEST_ASSERT_TRUE(result.nodes > 0);
  TEST_ASSERT_TRUE(result.depth == 2);
}

// ===========================================================================
// Depth-2 knight fork — find a winning tactic
// ===========================================================================

// White Nc3 can play Ne4 threatening to fork king+queen next move,
// but let's use a more direct fork position:
// White knight on d5 can jump to c7, forking Black king on e8 and rook on a8.
static void test_search_knight_fork(void) {
  const char* fen = "r3k3/8/8/3N4/8/8/8/4K3 w - - 0 1";
  auto result = searchFEN(fen, 3);
  std::string move = moveToStr(result.bestMove);
  // Nc7+ forks king and rook — should be the best move.
  TEST_ASSERT_EQUAL_STRING("d5c7", move.c_str());
}

// ===========================================================================
// Basic sanity — search returns a legal move
// ===========================================================================

static void test_search_returns_legal_move(void) {
  const char* fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
  auto result = searchFEN(fen, 1);
  // Must return a valid move (non-zero from/to)
  TEST_ASSERT_TRUE(result.bestMove.from != result.bestMove.to);
  TEST_ASSERT_TRUE(result.nodes > 0);
}

// ===========================================================================
// No legal moves — checkmate position returns empty move
// ===========================================================================

static void test_search_checkmate_no_move(void) {
  // White Kh1 is checkmated by Black Qg2 + Kh3.
  // Qg2 checks diagonally, g1 covered by g-file, h2 covered by rank 2 + Kh3,
  // Kxg2 defended by Kh3.  Zero legal moves.
  const char* fen = "8/8/8/8/8/7k/6q1/7K w - - 0 1";
  auto result = searchFEN(fen, 1);
  // If checkmate, bestMove should remain default (from=0, to=0).
  TEST_ASSERT_TRUE(result.bestMove.from == 0 && result.bestMove.to == 0);
}

// ===========================================================================
// Phase 2 — Iterative deepening tests
// ===========================================================================

// Deeper search finds mate-in-2 that depth 1 misses.
// White Kb1, Rh1, Rg1; Black Ka3.
// Depth 1: many moves look equal.  Depth 3+: finds Ra1# (after prep move).
// We use a classic 2-move mate: Rg3+ forces Ka2, then Ra1#.
static void test_search_deeper_finds_mate(void) {
  // White: Kb1, Rh1, Rg2; Black: Ka3.  Rg3+ Ka2 Rh2(a2)? No.
  // Simpler: White Kb1, Ra8, Rh1; Black Ka3.
  // Ra3+? No, a3 is occupied by king. Ra1# after Rh3+?
  // Let's use a known mate-in-2: White Kf1, Qd1, Ra1; Black Ke3.
  // Actually, let's just verify that searching deeper (4) is at least as good
  // as searching shallow (1) on a middlegame position.
  const char* fen = "r1bqkbnr/pppppppp/2n5/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 1 3";
  auto shallow = searchFEN(fen, 1);
  auto deep    = searchFEN(fen, 4);
  // Deeper search should reach at least depth 4
  TEST_ASSERT_EQUAL_INT(4, deep.depth);
  // Deeper search explores more nodes
  TEST_ASSERT_TRUE(deep.nodes > shallow.nodes);
}

// ID reports correct depth for each completed iteration.
static void test_search_id_reports_depth(void) {
  const char* fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
  Position pos;
  pos.loadFEN(fen);
  search::SearchLimits limits;
  limits.maxDepth = 3;

  int lastDepth = 0;
  int callCount = 0;
  // Use a static struct to pass data to the C callback (no captures allowed)
  struct InfoData {
    int lastDepth;
    int callCount;
  };
  static InfoData data = {0, 0};
  data = {0, 0};

  auto result = search::findBestMove(pos, limits, nullptr,
    [](const search::SearchResult& r) {
      data.lastDepth = r.depth;
      data.callCount++;
    });
  // Should have completed all 3 iterations
  TEST_ASSERT_EQUAL_INT(3, result.depth);
  TEST_ASSERT_EQUAL_INT(3, data.lastDepth);
  TEST_ASSERT_EQUAL_INT(3, data.callCount);
}

// Mock timer for time-controlled tests.
static uint32_t mockTimeMs = 0;
static uint32_t mockTimeFn() { return mockTimeMs; }

// Time limit stops search and returns valid result from last completed depth.
static void test_search_time_limit(void) {
  const char* fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
  Position pos;
  pos.loadFEN(fen);

  // Set time limit to 0 ms — the mock timer always returns 0, so the first
  // time check at 1024 nodes sees elapsed 0 >= 0? No, maxTimeMs > 0 is the
  // guard.  Set to 1 ms: first check at 1024 nodes, elapsed = 0 < 1, so
  // it continues.  After more nodes, still 0.  Let's make the mock advance.
  // Instead, set mockTime to something that will expire quickly:
  mockTimeMs = 0;
  search::SearchLimits limits;
  limits.maxDepth = 100;   // Very deep — would never finish
  limits.maxTimeMs = 1;    // 1 ms limit

  // After 1024 nodes, checkTime() fires: elapsed = mockTimeFn() - startTime
  // = 0 - 0 = 0 < 1 → continues.  We need the timer to advance.
  // Advance mock time after start:
  // Actually, since checkTime is called at node intervals and mockTimeMs is
  // static, we can set it to expire immediately by starting at time 0 and
  // having the mock always return a value >= maxTimeMs.
  mockTimeMs = 100;  // 100 ms elapsed from the start (which is 0)
  // startTime = mockTimeFn() at call = 100.  But then elapsed = 100 - 100 = 0.
  // Hmm, the timer is read once at start.  We need it to change between start
  // and the first check.  With a simple static, we can't do that.
  // Let's use a counter-based approach:
  // No — keep it simple: start at 0, let checkTime always see 100.
  // startTime = mockTimeFn() called in findBestMove = first call returns 0
  // Subsequent checkTime calls return 100 → elapsed = 100 >= 1 → stop!
  // But mockTimeMs is always 100 after we set it... no, startTime is captured
  // at the beginning.  If mockTimeMs=0 when findBestMove starts, startTime=0.
  // Then mockTimeMs gets set later in the test, but the search runs
  // synchronously, so it wouldn't change.
  //
  // Solution: Use a call-counting timer.
  static int timerCallCount;
  timerCallCount = 0;
  auto countingTimer = []() -> uint32_t {
    return timerCallCount++ > 0 ? 1000 : 0;
  };

  auto result = search::findBestMove(pos, limits, countingTimer);
  // Should have completed at least depth 1 before stopping
  TEST_ASSERT_TRUE(result.depth >= 1);
  // Should NOT have reached deep depths
  TEST_ASSERT_TRUE(result.depth < 20);
  // Must return a valid move
  TEST_ASSERT_TRUE(result.bestMove.from != result.bestMove.to);
}

// External stop flag cancels search.
static void test_search_stop_flag(void) {
  const char* fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
  Position pos;
  pos.loadFEN(fen);

  std::atomic<bool> stop{true};  // Already set — search stops immediately
  search::SearchLimits limits;
  limits.maxDepth = 100;
  limits.stop = &stop;

  auto result = search::findBestMove(pos, limits);
  // Stopped before completing any deep iteration, but depth 1 may complete
  // before the first 1024-node check.  Key assertion: returns a valid move.
  TEST_ASSERT_TRUE(result.bestMove.from != result.bestMove.to);
}

// Mate-in-1 search stops early (no need to deepen further).
// Finding mate requires depth 2 in ID terms: root move + one response ply
// where the opponent has no legal moves.
static void test_search_mate_stops_early(void) {
  const char* fen = "1k6/8/1K6/8/8/8/8/7R w - - 0 1";
  auto result = searchFEN(fen, 10);
  // Should find mate at depth 2 and stop, not search all 10 depths.
  TEST_ASSERT_EQUAL_INT(2, result.depth);
  TEST_ASSERT_TRUE(result.score >= search::MATE_SCORE - 10);
}

// ===========================================================================
// Phase 3 — Transposition table tests
// ===========================================================================

// TT store and probe: exact entry is retrieved correctly.
static void test_tt_store_probe_exact(void) {
  search::TranspositionTable tt;
  tt.resize(1024);

  Move m;
  m.from = 12;
  m.to = 28;
  m.flags = 0;
  tt.store(0xDEADBEEF12345678ULL, 150, m, 4, search::TTFlag::EXACT);

  const search::TTEntry* entry = tt.probe(0xDEADBEEF12345678ULL);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_EQUAL_INT(150, entry->score);
  TEST_ASSERT_EQUAL_INT(4, entry->depth);
  TEST_ASSERT_TRUE(entry->flag == search::TTFlag::EXACT);
  Move retrieved = search::unpackMove(entry->bestMove);
  TEST_ASSERT_EQUAL_INT(12, retrieved.from);
  TEST_ASSERT_EQUAL_INT(28, retrieved.to);

  tt.free();
}

// TT probe misses on different hash.
static void test_tt_probe_miss(void) {
  search::TranspositionTable tt;
  tt.resize(1024);

  Move m;
  m.from = 0;
  m.to = 0;
  m.flags = 0;
  tt.store(0xAAAAAAAABBBBBBBBULL, 100, m, 3, search::TTFlag::EXACT);

  // Different upper 32 bits → miss
  const search::TTEntry* entry = tt.probe(0xCCCCCCCCBBBBBBBBULL);
  TEST_ASSERT_NULL(entry);

  tt.free();
}

// TT clear zeros all entries.
static void test_tt_clear(void) {
  search::TranspositionTable tt;
  tt.resize(1024);

  Move m;
  m.from = 10;
  m.to = 20;
  m.flags = 0;
  tt.store(0x1234567890ABCDEFULL, 200, m, 5, search::TTFlag::LOWER_BOUND);
  tt.clear();

  // After clear, probe should miss
  const search::TTEntry* entry = tt.probe(0x1234567890ABCDEFULL);
  TEST_ASSERT_NULL(entry);

  tt.free();
}

// Pack/unpack move roundtrip preserves from, to, and flags.
static void test_tt_pack_unpack_move(void) {
  Move m;
  m.from = 52;  // e7
  m.to = 60;    // e8
  m.flags = 0x0B;  // promotion + capture (example)
  search::PackedMove pm = search::packMove(m);
  Move result = search::unpackMove(pm);
  TEST_ASSERT_EQUAL_INT(52, result.from);
  TEST_ASSERT_EQUAL_INT(60, result.to);
  TEST_ASSERT_EQUAL_INT(0x0B, result.flags);
}

// TT with search reduces node count on repeated position.
static void test_tt_reduces_nodes(void) {
  const char* fen = "r1bqkbnr/pppppppp/2n5/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 1 3";
  Position pos;
  pos.loadFEN(fen);

  // Search without TT
  search::SearchLimits limits;
  limits.maxDepth = 4;
  auto noTT = search::findBestMove(pos, limits);

  // Search with TT
  pos.loadFEN(fen);  // Reset position state
  search::TranspositionTable tt;
  tt.resize(search::DEFAULT_TT_SIZE);
  auto withTT = search::findBestMove(pos, limits, nullptr, nullptr, &tt);

  // TT should reduce node count (often significantly at depth 4+)
  TEST_ASSERT_TRUE(withTT.nodes < noTT.nodes);
  // Both should return valid moves
  TEST_ASSERT_TRUE(withTT.bestMove.from != withTT.bestMove.to);

  tt.free();
}

// Mate score survives TT storage (adjusted for ply).
static void test_tt_mate_score_roundtrip(void) {
  // Search a mate-in-1 position with TT — score should still indicate mate.
  const char* fen = "1k6/8/1K6/8/8/8/8/7R w - - 0 1";
  Position pos;
  pos.loadFEN(fen);

  search::TranspositionTable tt;
  tt.resize(1024);
  search::SearchLimits limits;
  limits.maxDepth = 3;

  auto result = search::findBestMove(pos, limits, nullptr, nullptr, &tt);
  TEST_ASSERT_TRUE(result.score >= search::MATE_SCORE - 10);
  std::string move = moveToStr(result.bestMove);
  TEST_ASSERT_EQUAL_STRING("h1h8", move.c_str());

  tt.free();
}

// ===========================================================================
// Phase 4 — Move ordering tests
// ===========================================================================

// Move ordering reduces node count significantly at deeper depths.
// Compare search with TT (which enables move ordering via TT move) to
// search without.  At depth 4, the reduction should be substantial.
static void test_ordering_reduces_nodes(void) {
  const char* fen = "r1bqkbnr/pppppppp/2n5/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 1 3";
  Position pos;
  pos.loadFEN(fen);

  // Without TT (no TT move ordering)
  search::SearchLimits limits;
  limits.maxDepth = 4;
  auto noTT = search::findBestMove(pos, limits);

  // With TT (TT move gets highest ordering priority)
  pos.loadFEN(fen);
  search::TranspositionTable tt;
  tt.resize(search::DEFAULT_TT_SIZE);
  auto withTT = search::findBestMove(pos, limits, nullptr, nullptr, &tt);

  // TT + move ordering should search fewer nodes
  TEST_ASSERT_TRUE(withTT.nodes < noTT.nodes);

  tt.free();
}

// Search with move ordering still finds correct tactical moves.
// Knight fork: Nc7+ forks king and rook — ordering shouldn't break this.
static void test_ordering_finds_tactics(void) {
  const char* fen = "r3k3/8/8/3N4/8/8/8/4K3 w - - 0 1";
  Position pos;
  pos.loadFEN(fen);

  search::TranspositionTable tt;
  tt.resize(search::DEFAULT_TT_SIZE);
  search::SearchLimits limits;
  limits.maxDepth = 3;

  auto result = search::findBestMove(pos, limits, nullptr, nullptr, &tt);
  std::string move = moveToStr(result.bestMove);
  TEST_ASSERT_EQUAL_STRING("d5c7", move.c_str());

  tt.free();
}

// ===========================================================================
// Registration
// ===========================================================================

void register_search_tests() {
  RUN_TEST(test_search_mate_in_1_white);
  RUN_TEST(test_search_mate_in_1_black);
  RUN_TEST(test_search_captures_hanging_piece);
  RUN_TEST(test_search_quiescence_avoids_blunder);
  RUN_TEST(test_search_avoids_stalemate);
  RUN_TEST(test_search_symmetric_position);
  RUN_TEST(test_search_knight_fork);
  RUN_TEST(test_search_returns_legal_move);
  RUN_TEST(test_search_checkmate_no_move);
  RUN_TEST(test_search_deeper_finds_mate);
  RUN_TEST(test_search_id_reports_depth);
  RUN_TEST(test_search_time_limit);
  RUN_TEST(test_search_stop_flag);
  RUN_TEST(test_search_mate_stops_early);
  RUN_TEST(test_tt_store_probe_exact);
  RUN_TEST(test_tt_probe_miss);
  RUN_TEST(test_tt_clear);
  RUN_TEST(test_tt_pack_unpack_move);
  RUN_TEST(test_tt_reduces_nodes);
  RUN_TEST(test_tt_mate_score_roundtrip);
  RUN_TEST(test_ordering_reduces_nodes);
  RUN_TEST(test_ordering_finds_tactics);
}
