#include <unity.h>

#include "../test_helpers.h"
#include <history.h>
#include <types.h>

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

static History hist;

// ---------------------------------------------------------------------------
// Standalone History (move log) tests
// ---------------------------------------------------------------------------

void test_history_initial_empty(void) {
  History h;
  TEST_ASSERT_EQUAL(0, h.moveCount());
  TEST_ASSERT_TRUE(h.empty());
  TEST_ASSERT_FALSE(h.canUndo());
  TEST_ASSERT_FALSE(h.canRedo());
  TEST_ASSERT_EQUAL(-1, h.currentMoveIndex());
}

void test_history_clear(void) {
  hist = History();
  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  TEST_ASSERT_EQUAL(1, hist.moveCount());

  hist.clear();
  TEST_ASSERT_EQUAL(0, hist.moveCount());
  TEST_ASSERT_TRUE(hist.empty());
  TEST_ASSERT_EQUAL(-1, hist.currentMoveIndex());
}

void test_history_add_and_get_move(void) {
  hist = History();
  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));

  TEST_ASSERT_EQUAL(1, hist.moveCount());
  TEST_ASSERT_FALSE(hist.empty());
  const MoveEntry& got = hist.getMove(0);
  TEST_ASSERT_EQUAL(squareOf(6, 4), got.from);
  TEST_ASSERT_EQUAL(squareOf(4, 4), got.to);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, got.piece);
  TEST_ASSERT_ENUM_EQ(Piece::NONE, got.captured);
}

void test_history_last_move(void) {
  hist = History();
  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  hist.addMove(makeEntry(1, 4, 3, 4, Piece::B_PAWN));

  TEST_ASSERT_EQUAL(2, hist.moveCount());
  const MoveEntry& last = hist.lastMove();
  TEST_ASSERT_EQUAL(squareOf(1, 4), last.from);
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, last.piece);
}

void test_history_undo_move(void) {
  hist = History();
  hist.addMove(makeEntry(6, 0, 4, 0, Piece::W_PAWN));

  TEST_ASSERT_TRUE(hist.canUndo());
  const MoveEntry* undone = hist.undoMove();
  TEST_ASSERT_NOT_NULL(undone);
  TEST_ASSERT_EQUAL(squareOf(6, 0), undone->from);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, undone->piece);
  TEST_ASSERT_EQUAL(-1, hist.currentMoveIndex());
  TEST_ASSERT_FALSE(hist.canUndo());
  // Move is still in the log, cursor stepped back
  TEST_ASSERT_EQUAL(1, hist.moveCount());
}

void test_history_undo_empty(void) {
  hist = History();
  const MoveEntry* result = hist.undoMove();
  TEST_ASSERT_NULL(result);
}

void test_history_redo_after_undo(void) {
  hist = History();
  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  hist.undoMove();

  TEST_ASSERT_TRUE(hist.canRedo());
  const MoveEntry* redone = hist.redoMove();
  TEST_ASSERT_NOT_NULL(redone);
  TEST_ASSERT_EQUAL_INT(squareOf(6, 4), redone->from);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, redone->piece);
  TEST_ASSERT_EQUAL(0, hist.currentMoveIndex());
  TEST_ASSERT_FALSE(hist.canRedo());
}

void test_history_redo_at_end(void) {
  hist = History();
  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  // No undo — already at end
  const MoveEntry* result = hist.redoMove();
  TEST_ASSERT_NULL(result);
}

void test_history_undo_redo_sequence(void) {
  hist = History();
  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));   // move 0
  hist.addMove(makeEntry(1, 4, 3, 4, Piece::B_PAWN));   // move 1
  hist.addMove(makeEntry(6, 3, 4, 3, Piece::W_PAWN));   // move 2

  TEST_ASSERT_EQUAL(2, hist.currentMoveIndex());

  // Undo twice
  hist.undoMove();  // cursor → 1
  TEST_ASSERT_EQUAL(1, hist.currentMoveIndex());
  hist.undoMove();  // cursor → 0
  TEST_ASSERT_EQUAL(0, hist.currentMoveIndex());

  // Redo once
  hist.redoMove();  // cursor → 1
  TEST_ASSERT_EQUAL(1, hist.currentMoveIndex());
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, hist.lastMove().piece);
}

void test_history_branch_wipes_future(void) {
  hist = History();
  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));   // move 0
  hist.addMove(makeEntry(1, 4, 3, 4, Piece::B_PAWN));   // move 1

  hist.undoMove();  // cursor → 0
  // Add a different move — should wipe move 1
  hist.addMove(makeEntry(1, 3, 3, 3, Piece::B_PAWN));   // new move 1
  TEST_ASSERT_EQUAL(2, hist.moveCount());
  TEST_ASSERT_EQUAL(fileOf(hist.getMove(1).to), 3);  // d5, not e5
  TEST_ASSERT_FALSE(hist.canRedo());
}

void test_history_multiple_moves(void) {
  hist = History();
  for (int i = 0; i < 10; i++) {
    hist.addMove(makeEntry(i % 8, i % 8, (i + 1) % 8, (i + 1) % 8, Piece::W_KNIGHT));
  }
  TEST_ASSERT_EQUAL(10, hist.moveCount());
  TEST_ASSERT_EQUAL(squareOf(0, 0), hist.getMove(0).from);
  TEST_ASSERT_EQUAL(squareOf(1, 1), hist.getMove(9).from);  // 9 % 8 = 1
}

// ---------------------------------------------------------------------------
// MoveEntry::build()
// ---------------------------------------------------------------------------

void test_moveEntry_build_simple(void) {
  PositionState prevSt = PositionState::initial();
  MoveResult result{MR_VALID, SQ_NONE, Piece::NONE, GameResult::IN_PROGRESS, ' '};
  MoveEntry e = MoveEntry::build(squareOf(6, 4), squareOf(4, 4), Piece::W_PAWN, Piece::NONE, result, prevSt);
  TEST_ASSERT_EQUAL(squareOf(6, 4), e.from);
  TEST_ASSERT_EQUAL(squareOf(4, 4), e.to);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, e.piece);
  TEST_ASSERT_ENUM_EQ(Piece::NONE, e.captured);
  TEST_ASSERT_FALSE(e.isCapture());
  TEST_ASSERT_FALSE(e.isEnPassant());
  TEST_ASSERT_FALSE(e.isCastling());
  TEST_ASSERT_FALSE(e.isPromotion());
}

void test_moveEntry_build_capture(void) {
  PositionState prevSt = PositionState::initial();
  MoveResult result{static_cast<uint8_t>(MR_VALID | MR_CAPTURE), SQ_NONE, Piece::NONE, GameResult::IN_PROGRESS, ' '};
  MoveEntry e = MoveEntry::build(squareOf(4, 4), squareOf(3, 3), Piece::W_PAWN, Piece::B_PAWN, result, prevSt);
  TEST_ASSERT_TRUE(e.isCapture());
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, e.captured);
}

void test_moveEntry_build_en_passant(void) {
  PositionState prevSt{0x00, squareOf(2, 3), 0, 1};
  MoveResult result{static_cast<uint8_t>(MR_VALID | MR_CAPTURE | MR_EP), squareOf(3, 3), Piece::NONE, GameResult::IN_PROGRESS, ' '};
  // EP capture: e5xd6, target is empty, captured pawn is determined by build()
  MoveEntry e = MoveEntry::build(squareOf(3, 4), squareOf(2, 3), Piece::W_PAWN, Piece::NONE, result, prevSt);
  TEST_ASSERT_TRUE(e.isEnPassant());
  TEST_ASSERT_TRUE(e.isCapture());
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, e.captured);  // build() infers captured pawn
  TEST_ASSERT_EQUAL(squareOf(3, 3), e.epCapturedSq);
}

void test_moveEntry_build_promotion(void) {
  PositionState prevSt{0x00, SQ_NONE, 0, 1};
  MoveResult result{static_cast<uint8_t>(MR_VALID | MR_PROMOTION), SQ_NONE, Piece::W_QUEEN, GameResult::IN_PROGRESS, ' '};
  MoveEntry e = MoveEntry::build(squareOf(1, 4), squareOf(0, 4), Piece::W_PAWN, Piece::NONE, result, prevSt);
  TEST_ASSERT_TRUE(e.isPromotion());
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, e.promotion);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, e.piece);  // original piece
}

void test_moveEntry_build_preserves_prev_state(void) {
  PositionState prevSt{0x0F, squareOf(5, 4), 10, 6};
  MoveResult result{MR_VALID, SQ_NONE, Piece::NONE, GameResult::IN_PROGRESS, ' '};
  MoveEntry e = MoveEntry::build(squareOf(6, 4), squareOf(4, 4), Piece::W_PAWN, Piece::NONE, result, prevSt);
  TEST_ASSERT_EQUAL_UINT8(0x0F, e.prevState.castlingRights);
  TEST_ASSERT_EQUAL_INT(squareOf(5, 4), e.prevState.epSquare);
  TEST_ASSERT_EQUAL_INT(10, e.prevState.halfmoveClock);
  TEST_ASSERT_EQUAL_INT(6, e.prevState.fullmoveClock);
}

// ---------------------------------------------------------------------------
// Additional history edge cases
// ---------------------------------------------------------------------------

void test_history_empty_returns_true(void) {
  hist = History();
  TEST_ASSERT_TRUE(hist.empty());
  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  TEST_ASSERT_FALSE(hist.empty());
}

void test_history_lastMove_content(void) {
  hist = History();
  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  hist.addMove(makeEntry(1, 4, 3, 4, Piece::B_PAWN));
  hist.addMove(makeEntry(7, 6, 5, 5, Piece::W_KNIGHT));
  const MoveEntry& last = hist.lastMove();
  TEST_ASSERT_ENUM_EQ(Piece::W_KNIGHT, last.piece);
  TEST_ASSERT_EQUAL(squareOf(5, 5), last.to);
}

void test_history_currentMoveIndex_direct(void) {
  hist = History();
  TEST_ASSERT_EQUAL_INT(-1, hist.currentMoveIndex());  // before any move

  hist.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  TEST_ASSERT_EQUAL_INT(0, hist.currentMoveIndex());  // after 1st move

  hist.addMove(makeEntry(1, 4, 3, 4, Piece::B_PAWN));
  TEST_ASSERT_EQUAL_INT(1, hist.currentMoveIndex());  // after 2nd move

  hist.undoMove();
  TEST_ASSERT_EQUAL_INT(0, hist.currentMoveIndex());  // undo → back to 1st

  hist.undoMove();
  TEST_ASSERT_EQUAL_INT(-1, hist.currentMoveIndex());  // undo → before any

  hist.redoMove();
  TEST_ASSERT_EQUAL_INT(0, hist.currentMoveIndex());  // redo → after 1st
}

// ---------------------------------------------------------------------------
// Compact move encoding (History::encodeMove / decodeMove)
// ---------------------------------------------------------------------------

void test_encodeMove_roundtrip(void) {
  Square from = squareOf(6, 4), to = squareOf(4, 4); // e2e4
  uint16_t encoded = History::encodeMove(from, to, ' ');
  Square decFrom, decTo;
  char promo;
  History::decodeMove(encoded, decFrom, decTo, promo);
  TEST_ASSERT_EQUAL_INT(from, decFrom);
  TEST_ASSERT_EQUAL_INT(to, decTo);
  TEST_ASSERT_EQUAL_CHAR(' ', promo);
}

void test_encodeMove_with_promotion(void) {
  Square from = squareOf(1, 4), to = squareOf(0, 4); // e7e8q
  uint16_t encoded = History::encodeMove(from, to, 'q');
  Square decFrom, decTo;
  char promo;
  History::decodeMove(encoded, decFrom, decTo, promo);
  TEST_ASSERT_EQUAL_INT(from, decFrom);
  TEST_ASSERT_EQUAL_INT(to, decTo);
  TEST_ASSERT_EQUAL_CHAR('q', promo);
}

void test_encodeMove_corner_squares(void) {
  // a8 (0,0) -> h1 (7,7) with knight promotion
  Square from = squareOf(0, 0), to = squareOf(7, 7);
  uint16_t encoded = History::encodeMove(from, to, 'n');
  Square decFrom, decTo;
  char promo;
  History::decodeMove(encoded, decFrom, decTo, promo);
  TEST_ASSERT_EQUAL_INT(from, decFrom);
  TEST_ASSERT_EQUAL_INT(to, decTo);
  TEST_ASSERT_EQUAL_CHAR('n', promo);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_history_tests() {
  needsDefaultKings = false;

  // Standalone History (move log + undo/redo)
  RUN_TEST(test_history_initial_empty);
  RUN_TEST(test_history_clear);
  RUN_TEST(test_history_add_and_get_move);
  RUN_TEST(test_history_last_move);
  RUN_TEST(test_history_undo_move);
  RUN_TEST(test_history_undo_empty);
  RUN_TEST(test_history_redo_after_undo);
  RUN_TEST(test_history_redo_at_end);
  RUN_TEST(test_history_undo_redo_sequence);
  RUN_TEST(test_history_branch_wipes_future);
  RUN_TEST(test_history_multiple_moves);

  // MoveEntry::build()
  RUN_TEST(test_moveEntry_build_simple);
  RUN_TEST(test_moveEntry_build_capture);
  RUN_TEST(test_moveEntry_build_en_passant);
  RUN_TEST(test_moveEntry_build_promotion);
  RUN_TEST(test_moveEntry_build_preserves_prev_state);

  // Additional edge cases
  RUN_TEST(test_history_empty_returns_true);
  RUN_TEST(test_history_lastMove_content);

  // currentMoveIndex direct
  RUN_TEST(test_history_currentMoveIndex_direct);

  // Compact move encoding (History::encodeMove / decodeMove)
  RUN_TEST(test_encodeMove_roundtrip);
  RUN_TEST(test_encodeMove_with_promotion);
  RUN_TEST(test_encodeMove_corner_squares);
}
