#include <unity.h>

#include "board/services/program/factory.h"
#include "board/services/program/program.h"

#include <memory>

namespace {

class FakeProgram final : public BoardProgram {
 public:
  void begin() override { ++beginCount; }
  void update() override {
    ++updateCount;
    if (completeOnUpdate) complete = true;
  }
  void cancel() override {
    ++cancelCount;
    complete = true;
  }
  bool isComplete() const override { return complete; }

  int beginCount = 0;
  int updateCount = 0;
  int cancelCount = 0;
  bool complete = false;
  bool completeOnUpdate = false;
};

class OwnedFakeProgram final : public BoardProgram {
 public:
  explicit OwnedFakeProgram(int* destroyCount) : destroyCount_(destroyCount) {}
  ~OwnedFakeProgram() override {
    if (destroyCount_) ++(*destroyCount_);
  }

  void begin() override { ++beginCount; }
  void update() override {
    ++updateCount;
    if (completeOnUpdate) complete = true;
  }
  void cancel() override {
    ++cancelCount;
    complete = true;
  }
  bool isComplete() const override { return complete; }

  int beginCount = 0;
  int updateCount = 0;
  int cancelCount = 0;
  bool complete = false;
  bool completeOnUpdate = false;

 private:
  int* destroyCount_;
};

std::unique_ptr<BoardProgram> createFactoryProgram(BoardProgramContext& context) {
  (void)context;
  return std::unique_ptr<BoardProgram>(new FakeProgram());
}

void test_program_runner_starts_program() {
  BoardProgramRunner runner;
  FakeProgram program;

  runner.set(program);

  TEST_ASSERT_TRUE(runner.active());
  TEST_ASSERT_EQUAL(1, program.beginCount);
  TEST_ASSERT_EQUAL(0, program.updateCount);
  TEST_ASSERT_EQUAL(0, program.cancelCount);
}

void test_program_runner_reports_completion_on_poll() {
  BoardProgramRunner runner;
  FakeProgram program;
  program.completeOnUpdate = true;

  runner.set(program);
  const bool completed = runner.poll();

  TEST_ASSERT_TRUE(completed);
  TEST_ASSERT_FALSE(runner.active());
  TEST_ASSERT_EQUAL(1, program.updateCount);
  TEST_ASSERT_EQUAL(0, program.cancelCount);
}

void test_program_runner_stop_cancels_active_program() {
  BoardProgramRunner runner;
  FakeProgram program;

  runner.set(program);
  runner.stop();

  TEST_ASSERT_FALSE(runner.active());
  TEST_ASSERT_EQUAL(1, program.cancelCount);
}

void test_program_runner_replaces_active_program() {
  BoardProgramRunner runner;
  FakeProgram first;
  FakeProgram second;

  runner.set(first);
  runner.set(second);

  TEST_ASSERT_TRUE(runner.active());
  TEST_ASSERT_EQUAL(1, first.cancelCount);
  TEST_ASSERT_EQUAL(1, second.beginCount);
}

void test_program_runner_owns_program_until_completion() {
  BoardProgramRunner runner;
  int destroyCount = 0;
  std::unique_ptr<OwnedFakeProgram> program(new OwnedFakeProgram(&destroyCount));
  program->completeOnUpdate = true;

  BoardProgram* active = runner.set(std::unique_ptr<BoardProgram>(program.release()));
  const bool completed = runner.poll();

  TEST_ASSERT_NOT_NULL(active);
  TEST_ASSERT_TRUE(completed);
  TEST_ASSERT_FALSE(runner.active());
  TEST_ASSERT_EQUAL(1, destroyCount);
}

void test_program_runner_stop_destroys_owned_program() {
  BoardProgramRunner runner;
  int destroyCount = 0;

  runner.set(std::unique_ptr<BoardProgram>(new OwnedFakeProgram(&destroyCount)));
  runner.stop();

  TEST_ASSERT_FALSE(runner.active());
  TEST_ASSERT_EQUAL(1, destroyCount);
}

void test_program_factory_registers_and_creates_program() {
  BoardProgramFactory factory;
  BoardProgramContext context{nullptr, nullptr, nullptr};

  TEST_ASSERT_TRUE(factory.registerCreator("fake", createFactoryProgram));
  TEST_ASSERT_TRUE(factory.has("fake"));

  std::unique_ptr<BoardProgram> program = factory.create("fake", context);
  TEST_ASSERT_NOT_NULL(program.get());
}

void test_program_factory_rejects_invalid_and_duplicate_entries() {
  BoardProgramFactory factory;

  TEST_ASSERT_FALSE(factory.registerCreator(nullptr, createFactoryProgram));
  TEST_ASSERT_FALSE(factory.registerCreator("", createFactoryProgram));
  TEST_ASSERT_FALSE(factory.registerCreator("fake", nullptr));
  TEST_ASSERT_TRUE(factory.registerCreator("fake", createFactoryProgram));
  TEST_ASSERT_FALSE(factory.registerCreator("fake", createFactoryProgram));
}

void test_program_factory_returns_null_for_unknown_id() {
  BoardProgramFactory factory;
  BoardProgramContext context{nullptr, nullptr, nullptr};

  std::unique_ptr<BoardProgram> program = factory.create("missing", context);

  TEST_ASSERT_NULL(program.get());
}

void test_program_factory_rejects_entries_when_full() {
  BoardProgramFactory factory;
  const char* ids[BoardProgramFactory::CAPACITY] = {
      "p0", "p1", "p2", "p3", "p4", "p5", "p6", "p7"};

  for (uint8_t i = 0; i < BoardProgramFactory::CAPACITY; ++i) {
    TEST_ASSERT_TRUE(factory.registerCreator(ids[i], createFactoryProgram));
  }

  TEST_ASSERT_FALSE(factory.registerCreator("overflow", createFactoryProgram));
}

}  // namespace

void register_program_tests() {
  RUN_TEST(test_program_runner_starts_program);
  RUN_TEST(test_program_runner_reports_completion_on_poll);
  RUN_TEST(test_program_runner_stop_cancels_active_program);
  RUN_TEST(test_program_runner_replaces_active_program);
  RUN_TEST(test_program_runner_owns_program_until_completion);
  RUN_TEST(test_program_runner_stop_destroys_owned_program);
  RUN_TEST(test_program_factory_registers_and_creates_program);
  RUN_TEST(test_program_factory_rejects_invalid_and_duplicate_entries);
  RUN_TEST(test_program_factory_returns_null_for_unknown_id);
  RUN_TEST(test_program_factory_rejects_entries_when_full);
}
