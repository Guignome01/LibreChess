#include <unity.h>

#include <iterator.h>

#include "../test_helpers.h"

// ── forEachSquare ────────────────────────────────────────────────────────────

static void test_forEachSquare_visits_all_64(void) {
  int count = 0;
  iterator::forEachSquare(mailbox, [&](int r, int c, Piece) { ++count; });
  TEST_ASSERT_EQUAL(64, count);
}

static void test_forEachSquare_visits_occupied_and_empty(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");

  int occupied = 0, empty = 0;
  iterator::forEachSquare(mailbox, [&](int, int, Piece p) {
    if (p != Piece::NONE)
      ++occupied;
    else
      ++empty;
  });
  TEST_ASSERT_EQUAL(2, occupied);
  TEST_ASSERT_EQUAL(62, empty);
}

// ── forEachPiece ─────────────────────────────────────────────────────────────

static void test_forEachPiece_empty_board(void) {
  int count = 0;
  iterator::forEachPiece(bb, mailbox, [&](int, int, Piece) { ++count; });
  TEST_ASSERT_EQUAL(0, count);
}

static void test_forEachPiece_skips_empty_squares(void) {
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "c6");
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");

  int count = 0;
  iterator::forEachPiece(bb, mailbox, [&](int, int, Piece) { ++count; });
  TEST_ASSERT_EQUAL(3, count);
}

static void test_forEachPiece_initial_position(void) {
  setupInitialBoard(bb, mailbox);

  int count = 0;
  iterator::forEachPiece(bb, mailbox, [&](int, int, Piece) { ++count; });
  TEST_ASSERT_EQUAL(32, count);
}

// ── Registration ─────────────────────────────────────────────────────────────

void register_iterator_tests() {
  // forEachSquare
  RUN_TEST(test_forEachSquare_visits_all_64);
  RUN_TEST(test_forEachSquare_visits_occupied_and_empty);

  // forEachPiece
  RUN_TEST(test_forEachPiece_empty_board);
  RUN_TEST(test_forEachPiece_skips_empty_squares);
  RUN_TEST(test_forEachPiece_initial_position);
}
