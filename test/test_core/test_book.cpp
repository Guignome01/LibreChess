#include <unity.h>

#include "../test_helpers.h"
#include <book.h>

using namespace LibreChess;

// ===========================================================================
// Opening book tests
//
// Verifies the constexpr flash-resident book: entry count, probe hits for
// known opening positions, probe misses for non-book positions, random
// variety across seeds, and hash correctness vs. full Position.
// ===========================================================================

// ---------------------------------------------------------------------------
// Entry count — the compiled book should have a reasonable number of entries
// ---------------------------------------------------------------------------

static void test_entryCount(void) {
  int count = book::entryCount();
  // ~48 lines × ~8–13 moves each = ~450–600 raw entries (with duplicates
  // from shared early moves, which are deduplicated at probe time).
  TEST_ASSERT_GREATER_THAN(350, count);
  TEST_ASSERT_LESS_THAN(700, count);
}

// ---------------------------------------------------------------------------
// Probe — starting position should return a known first move
// ---------------------------------------------------------------------------

static void test_probe_startpos(void) {
  Position pos;
  pos.newGame();

  uint8_t from, to;
  uint64_t rng = 0xDEADBEEF12345678ULL;
  bool hit = book::probe(pos.hash(), from, to, rng);
  TEST_ASSERT_TRUE(hit);

  // The returned move must be a legal first move from the startpos.
  // Verify it's in the root move list.
  MoveList moves;
  movegen::generateMoves(pos.bitboards(), pos.mailbox(),
                         pos.sideToMove(), pos.positionState(),
                         moves, movegen::FilterMode::ALL);
  bool found = false;
  for (int i = 0; i < moves.count; ++i) {
    if (moves.moves[i].from == from && moves.moves[i].to == to) {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Book move is not a legal move from startpos");
}

// ---------------------------------------------------------------------------
// Probe — after 1.e4, should return a known reply (e.g. e7e5, c7c5, e7e6)
// ---------------------------------------------------------------------------

static void test_probe_after_e4(void) {
  Position pos;
  pos.newGame();
  pos.makeMove(12, 28);  // e2e4

  uint8_t from, to;
  uint64_t rng = 0xCAFEBABE00000001ULL;
  bool hit = book::probe(pos.hash(), from, to, rng);
  TEST_ASSERT_TRUE(hit);

  // Must be a legal reply
  MoveList moves;
  movegen::generateMoves(pos.bitboards(), pos.mailbox(),
                         pos.sideToMove(), pos.positionState(),
                         moves, movegen::FilterMode::ALL);
  bool found = false;
  for (int i = 0; i < moves.count; ++i) {
    if (moves.moves[i].from == from && moves.moves[i].to == to) {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Book move is not a legal reply to 1.e4");
}

// ---------------------------------------------------------------------------
// Probe — after 1.d4, should return a known reply
// ---------------------------------------------------------------------------

static void test_probe_after_d4(void) {
  Position pos;
  pos.newGame();
  pos.makeMove(11, 27);  // d2d4

  uint8_t from, to;
  uint64_t rng = 0x1234567800000001ULL;
  bool hit = book::probe(pos.hash(), from, to, rng);
  TEST_ASSERT_TRUE(hit);

  MoveList moves;
  movegen::generateMoves(pos.bitboards(), pos.mailbox(),
                         pos.sideToMove(), pos.positionState(),
                         moves, movegen::FilterMode::ALL);
  bool found = false;
  for (int i = 0; i < moves.count; ++i) {
    if (moves.moves[i].from == from && moves.moves[i].to == to) {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Book move is not a legal reply to 1.d4");
}

// ---------------------------------------------------------------------------
// Probe miss — a random middlegame position should not be in the book
// ---------------------------------------------------------------------------

static void test_probe_miss(void) {
  // Arbitrary middlegame: Sicilian Najdorf, move 15+ — well past book depth
  Position pos;
  pos.loadFEN("r1b1k2r/1pq1bppp/p1nppn2/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQkq - 0 10");

  uint8_t from, to;
  uint64_t rng = 0xAAAABBBBCCCCDDDDULL;
  bool hit = book::probe(pos.hash(), from, to, rng);
  TEST_ASSERT_FALSE(hit);
}

// ---------------------------------------------------------------------------
// Variety — different PRNG seeds should produce different selections
// when multiple book moves exist for the starting position
// ---------------------------------------------------------------------------

static void test_probe_variety(void) {
  Position pos;
  pos.newGame();

  // Probe many times with different seeds, collect distinct moves.
  // The starting position should have at least 3–4 book moves
  // (e2e4, d2d4, c2c4, g1f3, etc.).
  int distinctMoves = 0;
  uint8_t seenFrom[20]{}, seenTo[20]{};

  for (uint64_t seed = 1; seed <= 200; ++seed) {
    uint8_t from, to;
    uint64_t rng = seed * 0x9E3779B97F4A7C15ULL;  // vary seed widely
    if (rng == 0) rng = 1;
    book::probe(pos.hash(), from, to, rng);

    bool already = false;
    for (int i = 0; i < distinctMoves; ++i) {
      if (seenFrom[i] == from && seenTo[i] == to) {
        already = true;
        break;
      }
    }
    if (!already && distinctMoves < 20) {
      seenFrom[distinctMoves] = from;
      seenTo[distinctMoves] = to;
      ++distinctMoves;
    }
  }
  // With ~48 lines, the starting position should have at least 3 distinct
  // book moves (e4, d4, c4, Nf3, etc.).
  TEST_ASSERT_GREATER_OR_EQUAL(3, distinctMoves);
}

// ---------------------------------------------------------------------------
// Hash correctness — the constexpr ReplayBoard hash must match Position::hash()
// for several known opening positions.
// ---------------------------------------------------------------------------

static void test_hash_matches_position(void) {
  // 1. Starting position — probe must hit
  {
    Position pos;
    pos.newGame();
    uint8_t from, to;
    uint64_t rng = 1;
    TEST_ASSERT_TRUE_MESSAGE(
        book::probe(pos.hash(), from, to, rng),
        "Starting position hash mismatch — no book hit");
  }

  // 2. After 1.e4 e5 2.Nf3 Nc6 (open game position)
  {
    Position pos;
    pos.newGame();
    pos.makeMove(12, 28);  // e2e4
    pos.makeMove(52, 36);  // e7e5
    pos.makeMove(6, 21);   // g1f3
    pos.makeMove(57, 42);  // b8c6
    uint8_t from, to;
    uint64_t rng = 1;
    TEST_ASSERT_TRUE_MESSAGE(
        book::probe(pos.hash(), from, to, rng),
        "1.e4 e5 2.Nf3 Nc6 hash mismatch — no book hit");
  }

  // 3. After 1.d4 Nf6 2.c4 e6 (Indian setup)
  {
    Position pos;
    pos.newGame();
    pos.makeMove(11, 27);  // d2d4
    pos.makeMove(62, 45);  // g8f6
    pos.makeMove(10, 26);  // c2c4
    pos.makeMove(52, 44);  // e7e6
    uint8_t from, to;
    uint64_t rng = 1;
    TEST_ASSERT_TRUE_MESSAGE(
        book::probe(pos.hash(), from, to, rng),
        "1.d4 Nf6 2.c4 e6 hash mismatch — no book hit");
  }

  // 4. After 1.e4 c5 (Sicilian)
  {
    Position pos;
    pos.newGame();
    pos.makeMove(12, 28);  // e2e4
    pos.makeMove(50, 34);  // c7c5
    uint8_t from, to;
    uint64_t rng = 1;
    TEST_ASSERT_TRUE_MESSAGE(
        book::probe(pos.hash(), from, to, rng),
        "1.e4 c5 hash mismatch — no book hit");
  }
}

// ---------------------------------------------------------------------------
// findBestMove integration — book move returned with depth 0
// ---------------------------------------------------------------------------

static void test_findBestMove_returns_book_move(void) {
  Position pos;
  pos.newGame();

  search::SearchLimits limits;
  limits.maxDepth = 6;
  search::SearchState state;
  state.useBook = true;

  search::SearchResult result = search::findBestMove(pos, limits, state);
  // Book hit → depth should be 0, nodes should be 0
  TEST_ASSERT_EQUAL(0, result.depth);
  TEST_ASSERT_EQUAL(0u, result.nodes);
  TEST_ASSERT_FALSE(result.bestMove.isNull());
}

// ---------------------------------------------------------------------------
// findBestMove with book disabled — should search normally
// ---------------------------------------------------------------------------

static void test_findBestMove_no_book_when_disabled(void) {
  Position pos;
  pos.newGame();

  search::SearchLimits limits;
  limits.maxDepth = 2;
  search::SearchState state;
  state.useBook = false;

  search::SearchResult result = search::findBestMove(pos, limits, state);
  // With book disabled, search ran → depth > 0, nodes > 0
  TEST_ASSERT_GREATER_THAN(0, result.depth);
  TEST_ASSERT_GREATER_THAN(0u, result.nodes);
  TEST_ASSERT_FALSE(result.bestMove.isNull());
}

// ===========================================================================
// Registration
// ===========================================================================

void register_book_tests() {
  RUN_TEST(test_entryCount);
  RUN_TEST(test_probe_startpos);
  RUN_TEST(test_probe_after_e4);
  RUN_TEST(test_probe_after_d4);
  RUN_TEST(test_probe_miss);
  RUN_TEST(test_probe_variety);
  RUN_TEST(test_hash_matches_position);
  RUN_TEST(test_findBestMove_returns_book_move);
  RUN_TEST(test_findBestMove_no_book_when_disabled);
}
