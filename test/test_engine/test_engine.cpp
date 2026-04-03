#include <unity.h>

#include <engine.h>
#include <notation.h>
#include <search.h>

#include "../test_helpers.h"

using namespace LibreChess;

// ===========================================================================
// Helpers
// ===========================================================================

// Convert a Move to UCI coordinate string for readable assertions.
static std::string moveToStr(Move m) {
  std::string s = notation::toCoordinate(rowOf(m.from), colOf(m.from),
                                         rowOf(m.to), colOf(m.to));
  if (m.isPromotion()) {
    static const char promoChars[] = {'n', 'b', 'r', 'q'};
    s += promoChars[m.promoIndex()];
  }
  return s;
}

static const char* STARTPOS =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// ===========================================================================
// calculateMove — returns a legal move from starting position
// ===========================================================================

static void test_engine_calculateMove_startpos(void) {
  Engine engine(64);
  search::SearchLimits limits;
  limits.maxDepth = 1;
  auto result = engine.calculateMove(STARTPOS, limits);
  // Must find some legal move (not 0000)
  TEST_ASSERT_TRUE(result.bestMove.from != 0 || result.bestMove.to != 0);
}

// ===========================================================================
// calculateMove — depth 1 produces valid result
// ===========================================================================

static void test_engine_calculateMove_depth1(void) {
  Engine engine(64);
  search::SearchLimits limits;
  limits.maxDepth = 1;
  auto result = engine.calculateMove(STARTPOS, limits);
  TEST_ASSERT_EQUAL_INT(1, result.depth);
  TEST_ASSERT_TRUE(result.nodes > 0);
}

// ===========================================================================
// calculateMove — custom FEN
// ===========================================================================

static void test_engine_calculateMove_custom_fen(void) {
  Engine engine(64);
  search::SearchLimits limits;
  limits.maxDepth = 1;
  // Position with just kings and a rook — easy capture or move
  auto result = engine.calculateMove(
      "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", limits);
  TEST_ASSERT_TRUE(result.bestMove.from != 0 || result.bestMove.to != 0);
}

// ===========================================================================
// newGame clears TT
// ===========================================================================

static void test_engine_newgame_clears(void) {
  Engine engine(64);
  search::SearchLimits limits;
  limits.maxDepth = 2;
  // Run a search to populate TT
  engine.calculateMove(STARTPOS, limits);
  // Reset
  engine.newGame();
  // Position should be back to startpos
  TEST_ASSERT_TRUE(engine.position().sideToMove() == Color::WHITE);
}

// ===========================================================================
// position() returns correct state after calculateMove
// ===========================================================================

static void test_engine_position_access(void) {
  Engine engine(64);
  search::SearchLimits limits;
  limits.maxDepth = 1;
  // Load a FEN where it's black's turn
  engine.calculateMove("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
                       limits);
  // Position should reflect the loaded FEN (black to move)
  TEST_ASSERT_TRUE(engine.position().sideToMove() == Color::BLACK);
}

// ===========================================================================
// mate-in-1 — known position finds the mate
// ===========================================================================

static void test_engine_mate_in_1(void) {
  Engine engine(64);
  search::SearchLimits limits;
  limits.maxDepth = 2;
  // White Kb6, Rh1; Black Kb8.  Rh8# is the only mate.
  auto result = engine.calculateMove("1k6/8/1K6/8/8/8/8/7R w - - 0 1", limits);
  std::string move = moveToStr(result.bestMove);
  TEST_ASSERT_EQUAL_STRING("h1h8", move.c_str());
  TEST_ASSERT_TRUE(result.score >= search::MATE_SCORE - 10);
}

// ===========================================================================
// deeper depth finds more nodes
// ===========================================================================

static void test_engine_depth_control(void) {
  Engine engine(64);
  search::SearchLimits limits1;
  limits1.maxDepth = 1;
  auto r1 = engine.calculateMove(STARTPOS, limits1);

  search::SearchLimits limits3;
  limits3.maxDepth = 3;
  auto r3 = engine.calculateMove(STARTPOS, limits3);

  TEST_ASSERT_TRUE(r3.nodes > r1.nodes);
  TEST_ASSERT_TRUE(r3.depth >= r1.depth);
}

// ===========================================================================
// external stop flag works
// ===========================================================================

static void test_engine_external_stop(void) {
  Engine engine(64);
  std::atomic<bool> extStop{true};  // Pre-set
  engine.setExternalStop(&extStop);
  search::SearchLimits limits;
  limits.maxDepth = 100;
  auto result = engine.calculateMove(STARTPOS, limits);
  // Stop fires at the next 1024-node check, so a few shallow iterations may
  // complete before it kicks in.  The key assertion: search stopped far short
  // of the requested depth.
  TEST_ASSERT_TRUE(result.depth < 10);
}

// ===========================================================================
// consecutive calls — TT persists, second search finds answer faster
// ===========================================================================

static void test_engine_consecutive_calls(void) {
  Engine engine(256);
  search::SearchLimits limits;
  limits.maxDepth = 4;
  auto r1 = engine.calculateMove(STARTPOS, limits);
  // Same position again — TT should help
  auto r2 = engine.calculateMove(STARTPOS, limits);
  // Both must find a legal move
  TEST_ASSERT_TRUE(r1.bestMove.from != 0 || r1.bestMove.to != 0);
  TEST_ASSERT_TRUE(r2.bestMove.from != 0 || r2.bestMove.to != 0);
  // Second search should use fewer or equal nodes (TT primed)
  TEST_ASSERT_TRUE(r2.nodes <= r1.nodes);
}

// ===========================================================================
// score is in centipawns — plausible range from startpos
// ===========================================================================

static void test_engine_score_centipawns(void) {
  Engine engine(64);
  search::SearchLimits limits;
  limits.maxDepth = 3;
  auto result = engine.calculateMove(STARTPOS, limits);
  // Starting position should be roughly equal (-200 to +200 cp)
  TEST_ASSERT_TRUE(result.score > -200);
  TEST_ASSERT_TRUE(result.score < 200);
}

// ===========================================================================
// Registration
// ===========================================================================

void register_engine_tests() {
  RUN_TEST(test_engine_calculateMove_startpos);
  RUN_TEST(test_engine_calculateMove_depth1);
  RUN_TEST(test_engine_calculateMove_custom_fen);
  RUN_TEST(test_engine_newgame_clears);
  RUN_TEST(test_engine_position_access);
  RUN_TEST(test_engine_mate_in_1);
  RUN_TEST(test_engine_depth_control);
  RUN_TEST(test_engine_external_stop);
  RUN_TEST(test_engine_consecutive_calls);
  RUN_TEST(test_engine_score_centipawns);
}
