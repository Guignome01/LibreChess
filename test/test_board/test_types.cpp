// Tests for board-owned gameplay DTOs and assistance policy helpers.

#include <unity.h>

#include "board/assistance_provider.h"
#include "board/types.h"

namespace {

void test_assistance_engine_gate_only_for_best_move() {
  TEST_ASSERT_FALSE(boardAssistanceUsesEngine(BoardAssistanceLevel::NONE));
  TEST_ASSERT_FALSE(boardAssistanceUsesEngine(BoardAssistanceLevel::LEGAL_MOVES));
  TEST_ASSERT_TRUE(boardAssistanceUsesEngine(BoardAssistanceLevel::BEST_MOVE));
}

void test_fixed_assistance_providers_do_not_service_engine_hints() {
  BoardBestMoveHint hint;
  BoardNoAssistanceProvider none;
  BoardLegalMoveAssistanceProvider legal;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BoardAssistanceLevel::NONE),
                          static_cast<uint8_t>(none.level()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BoardAssistanceLevel::LEGAL_MOVES),
                          static_cast<uint8_t>(legal.level()));
  TEST_ASSERT_FALSE(none.service(hint));
  TEST_ASSERT_FALSE(legal.service(hint));
}

void test_target_list_merges_duplicate_destinations() {
  BoardMoveTargetList targets;
  TEST_ASSERT_TRUE(targets.addOrMerge(BoardMoveTarget{3, 4, false, false, -1, -1}));
  TEST_ASSERT_TRUE(targets.addOrMerge(BoardMoveTarget{3, 4, true, false, 3, 4}));

  TEST_ASSERT_EQUAL_UINT8(1, targets.count);
  const BoardMoveTarget* target = targets.find(3, 4);
  TEST_ASSERT_NOT_NULL(target);
  TEST_ASSERT_TRUE(target->capture);
  TEST_ASSERT_FALSE(target->enPassant);
  TEST_ASSERT_EQUAL_INT(3, target->capturedRow);
  TEST_ASSERT_EQUAL_INT(4, target->capturedCol);
}

void test_target_list_finds_en_passant_capture_square() {
  BoardMoveTargetList targets;
  TEST_ASSERT_TRUE(targets.addOrMerge(BoardMoveTarget{2, 4, true, true, 3, 4}));

  BoardMoveTarget capture;
  TEST_ASSERT_TRUE(targets.captureForLiftedSquare(3, 4, capture));
  TEST_ASSERT_EQUAL_INT(2, capture.row);
  TEST_ASSERT_EQUAL_INT(4, capture.col);
  TEST_ASSERT_TRUE(capture.enPassant);
  TEST_ASSERT_FALSE(targets.captureForLiftedSquare(2, 4, capture));
}

void test_target_list_rejects_out_of_bounds() {
  BoardMoveTargetList targets;
  TEST_ASSERT_FALSE(targets.addOrMerge(BoardMoveTarget{-1, 4, false, false, -1, -1}));
  TEST_ASSERT_FALSE(targets.addOrMerge(BoardMoveTarget{8, 4, false, false, -1, -1}));
  TEST_ASSERT_FALSE(targets.addOrMerge(BoardMoveTarget{4, 8, false, false, -1, -1}));
  TEST_ASSERT_EQUAL_UINT8(0, targets.count);
}

}  // namespace

void register_board_type_tests() {
  RUN_TEST(test_assistance_engine_gate_only_for_best_move);
  RUN_TEST(test_fixed_assistance_providers_do_not_service_engine_hints);
  RUN_TEST(test_target_list_merges_duplicate_destinations);
  RUN_TEST(test_target_list_finds_en_passant_capture_square);
  RUN_TEST(test_target_list_rejects_out_of_bounds);
}
