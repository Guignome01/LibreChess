#include <unity.h>

#include <string>

#include <uci.h>

#include "../test_helpers.h"

using namespace LibreChess;

// ===========================================================================
// Helpers
// ===========================================================================

// Send a command and return the output.
static std::string send(uci::UCIState& state, const char* line) {
  std::string output;
  uci::processLine(state, line, output);
  return output;
}

// Check if output contains a substring.
static bool contains(const std::string& text, const std::string& sub) {
  return text.find(sub) != std::string::npos;
}

// ===========================================================================
// uci — returns id and uciok
// ===========================================================================

static void test_uci_command(void) {
  uci::UCIState state(nullptr, 64);
  std::string out = send(state, "uci");
  TEST_ASSERT_TRUE(contains(out, "id name LibreChess"));
  TEST_ASSERT_TRUE(contains(out, "uciok"));
}

// ===========================================================================
// isready — returns readyok
// ===========================================================================

static void test_uci_isready(void) {
  uci::UCIState state(nullptr, 64);
  std::string out = send(state, "isready");
  TEST_ASSERT_TRUE(contains(out, "readyok"));
}

// ===========================================================================
// position startpos + go depth 1 — returns bestmove
// ===========================================================================

static void test_uci_go_depth1(void) {
  uci::UCIState state(nullptr, 64);
  send(state, "position startpos");
  std::string out = send(state, "go depth 1");
  TEST_ASSERT_TRUE(contains(out, "bestmove"));
  // Must not be null move
  TEST_ASSERT_FALSE(contains(out, "bestmove 0000"));
}

// ===========================================================================
// position startpos moves e2e4 e7e5 — applies moves correctly
// ===========================================================================

static void test_uci_position_with_moves(void) {
  uci::UCIState state(nullptr, 64);
  send(state, "position startpos moves e2e4 e7e5");
  // After e2e4 e7e5, it's white's turn
  TEST_ASSERT_EQUAL(Color::WHITE, state.pos.sideToMove());
  std::string out = send(state, "go depth 1");
  TEST_ASSERT_TRUE(contains(out, "bestmove"));
  TEST_ASSERT_FALSE(contains(out, "bestmove 0000"));
}

// ===========================================================================
// position fen — loads a custom FEN
// ===========================================================================

static void test_uci_position_fen(void) {
  uci::UCIState state(nullptr, 64);
  send(state, "position fen 1k6/8/1K6/8/8/8/8/7R w - - 0 1");
  std::string out = send(state, "go depth 2");
  TEST_ASSERT_TRUE(contains(out, "bestmove"));
  // Known mate-in-1: Rh8#
  TEST_ASSERT_TRUE(contains(out, "h1h8"));
}

// ===========================================================================
// ucinewgame + isready — state reset works
// ===========================================================================

static void test_uci_newgame(void) {
  uci::UCIState state(nullptr, 64);
  send(state, "position startpos");
  send(state, "go depth 1");  // populate TT
  send(state, "ucinewgame");
  std::string out = send(state, "isready");
  TEST_ASSERT_TRUE(contains(out, "readyok"));
}

// ===========================================================================
// info lines contain expected fields
// ===========================================================================

static void test_uci_info_output(void) {
  uci::UCIState state(nullptr, 64);
  send(state, "setoption name OwnBook value false");
  send(state, "position startpos");
  std::string out = send(state, "go depth 3");
  TEST_ASSERT_TRUE(contains(out, "info depth"));
  TEST_ASSERT_TRUE(contains(out, "score"));
  TEST_ASSERT_TRUE(contains(out, "nodes"));
  TEST_ASSERT_TRUE(contains(out, "pv"));
}

// ===========================================================================
// quit returns false
// ===========================================================================

static void test_uci_quit(void) {
  uci::UCIState state(nullptr, 64);
  std::string output;
  bool result = uci::processLine(state, "quit", output);
  TEST_ASSERT_FALSE(result);
}

// ===========================================================================
// mate-in-1 reports mate score
// ===========================================================================

static void test_uci_mate_score(void) {
  uci::UCIState state(nullptr, 64);
  send(state, "position fen 1k6/8/1K6/8/8/8/8/7R w - - 0 1");
  std::string out = send(state, "go depth 2");
  TEST_ASSERT_TRUE(contains(out, "score mate"));
}

// ===========================================================================
// setoption — Hash resizes TT
// ===========================================================================

static void test_uci_setoption_hash(void) {
  uci::UCIState state(nullptr, 64);
  send(state, "setoption name Hash value 1");
  std::string out = send(state, "isready");
  TEST_ASSERT_TRUE(contains(out, "readyok"));
}

// ===========================================================================
// go movetime — respects time limit
// ===========================================================================

static void test_uci_go_movetime(void) {
  uci::UCIState state(chronoMillis, 64);
  send(state, "position startpos");
  // Very short movetime — should still find a move
  std::string out = send(state, "go movetime 50");
  TEST_ASSERT_TRUE(contains(out, "bestmove"));
  TEST_ASSERT_FALSE(contains(out, "bestmove 0000"));
}

// ===========================================================================
// position with invalid FEN — emits "info string error: invalid fen"
// and leaves position state unchanged.
// ===========================================================================

static void test_uci_position_invalid_fen(void) {
  uci::UCIState state(nullptr, 64);
  // Establish a known starting state first.
  send(state, "position startpos");
  Color sideBefore = state.pos.sideToMove();

  // Garbage FEN — must be rejected by validateFEN before fenToBoard runs.
  std::string out = send(state, "position fen not_a_valid_fen_string");
  TEST_ASSERT_TRUE_MESSAGE(contains(out, "info string error"),
                           "Expected error line for invalid FEN");
  TEST_ASSERT_TRUE_MESSAGE(contains(out, "invalid fen"),
                           "Error line should mention 'invalid fen'");
  // Position state must remain untouched (still startpos, white to move).
  TEST_ASSERT_EQUAL(sideBefore, state.pos.sideToMove());
}

// ===========================================================================
// position startpos with an illegal move — emits error and halts the
// move loop so the side to move does not advance.
// ===========================================================================

static void test_uci_position_illegal_move(void) {
  uci::UCIState state(nullptr, 64);
  // a1a2 is not legal from the starting position (rook blocked by own pawn).
  std::string out = send(state, "position startpos moves a1a2");
  TEST_ASSERT_TRUE_MESSAGE(contains(out, "info string error"),
                           "Expected error line for illegal move");
  TEST_ASSERT_TRUE_MESSAGE(contains(out, "illegal move"),
                           "Error line should mention 'illegal move'");
  // Move loop must halt on first error: side to move stays White.
  TEST_ASSERT_EQUAL(Color::WHITE, state.pos.sideToMove());
}

// ===========================================================================
// position startpos with an unparseable move token — emits error and
// halts the move loop.
// ===========================================================================

static void test_uci_position_unparseable_move(void) {
  uci::UCIState state(nullptr, 64);
  // "zz99" cannot be parsed as a coordinate move.
  std::string out = send(state, "position startpos moves zz99 e2e4");
  TEST_ASSERT_TRUE_MESSAGE(contains(out, "info string error"),
                           "Expected error line for unparseable move");
  // Loop halted before e2e4 was applied.
  TEST_ASSERT_EQUAL(Color::WHITE, state.pos.sideToMove());
}

// ===========================================================================
// position with neither startpos nor fen — emits error.
// ===========================================================================

static void test_uci_position_missing_keyword(void) {
  uci::UCIState state(nullptr, 64);
  std::string out = send(state, "position");
  TEST_ASSERT_TRUE_MESSAGE(contains(out, "info string error"),
                           "Expected error line for missing position keyword");
}

// ===========================================================================
// Registration
// ===========================================================================

void register_uci_tests() {
  RUN_TEST(test_uci_command);
  RUN_TEST(test_uci_isready);
  RUN_TEST(test_uci_go_depth1);
  RUN_TEST(test_uci_position_with_moves);
  RUN_TEST(test_uci_position_fen);
  RUN_TEST(test_uci_newgame);
  RUN_TEST(test_uci_info_output);
  RUN_TEST(test_uci_quit);
  RUN_TEST(test_uci_mate_score);
  RUN_TEST(test_uci_setoption_hash);
  RUN_TEST(test_uci_go_movetime);
  RUN_TEST(test_uci_position_invalid_fen);
  RUN_TEST(test_uci_position_illegal_move);
  RUN_TEST(test_uci_position_unparseable_move);
  RUN_TEST(test_uci_position_missing_keyword);
}
