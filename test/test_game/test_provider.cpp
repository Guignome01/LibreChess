#include <unity.h>

#include "provider.h"

namespace {

class DummyProvider final : public EngineProvider {
 public:
  bool initialize(EngineInitResult& result) override {
    result.playerColor = 'b';
    return true;
  }

  void requestMove(const std::string& fen) override { lastFen = fen; }

  bool checkResult(EngineResult& result) override {
    result.type = EngineResult::MOVE;
    result.move = "e2e4";
    return true;
  }

  std::string lastFen;
};

void test_provider_result_defaults_are_safe() {
  EngineInitResult init;
  TEST_ASSERT_EQUAL_CHAR('w', init.playerColor);
  TEST_ASSERT_TRUE(init.fen.empty());
  TEST_ASSERT_EQUAL_UINT8(0, init.mode);
  TEST_ASSERT_EQUAL_UINT8(0, init.engineId);
  TEST_ASSERT_EQUAL_UINT8(0, init.difficulty);
  TEST_ASSERT_TRUE(init.canResume);

  EngineResult result;
  TEST_ASSERT_EQUAL_INT(EngineResult::NONE, result.type);
  TEST_ASSERT_TRUE(result.move.empty());
  TEST_ASSERT_EQUAL_INT(0, result.evaluation);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LibreChess::GameResult::IN_PROGRESS),
                        static_cast<int>(result.gameResult));
  TEST_ASSERT_EQUAL_CHAR(' ', result.winnerColor);
}

void test_provider_default_hooks_are_noops() {
  DummyProvider provider;
  TEST_ASSERT_TRUE(provider.onPlayerMoveApplied("e2e4"));
  provider.onResignConfirmed();
  provider.cancelRequest();
  TEST_ASSERT_EQUAL_INT(0, provider.getEvaluation());
}

void test_provider_virtual_contract_can_return_move() {
  DummyProvider provider;
  EngineProvider* base = &provider;

  EngineInitResult init;
  TEST_ASSERT_TRUE(base->initialize(init));
  TEST_ASSERT_EQUAL_CHAR('b', init.playerColor);

  base->requestMove("startpos");
  TEST_ASSERT_EQUAL_STRING("startpos", provider.lastFen.c_str());

  EngineResult result;
  TEST_ASSERT_TRUE(base->checkResult(result));
  TEST_ASSERT_EQUAL_INT(EngineResult::MOVE, result.type);
  TEST_ASSERT_EQUAL_STRING("e2e4", result.move.c_str());
}

}  // namespace

void register_provider_tests() {
  RUN_TEST(test_provider_result_defaults_are_safe);
  RUN_TEST(test_provider_default_hooks_are_noops);
  RUN_TEST(test_provider_virtual_contract_can_return_move);
}