// Tests for typed physical-board menu state machines.

#include <unity.h>

#include "board/menus/confirm.h"
#include "board/menus/game_selection.h"

namespace {

class FakeMenuController final : public BoardMenuController {
 public:
  void show(const MenuOption* options, uint8_t count) override {
    copyOptions(options, count);
    hasBack = false;
    finished = false;
  }

  void showWithBack(const MenuOption* options, uint8_t count, int8_t row, int8_t col) override {
    copyOptions(options, count);
    hasBack = true;
    backRow = row;
    backCol = col;
    finished = false;
  }

  void erase() override {
    count = 0;
    hasBack = false;
  }

  void finish() override { finished = true; }

  void blink(int8_t row, int8_t col, LedRGB color, int times) override {
    blinkRow = row;
    blinkCol = col;
    blinkColor = color;
    blinkTimes = times;
    ++blinkCount;
  }

  void wait(uint32_t durationMs) override { waitedMs += durationMs; }

  bool hasOption(int id) const {
    for (uint8_t i = 0; i < count; ++i) {
      if (options[i].id == id) return true;
    }
    return false;
  }

  MenuOption options[MENU_PANEL_OPTION_COUNT] = {};
  uint8_t count = 0;
  bool hasBack = false;
  int8_t backRow = -1;
  int8_t backCol = -1;
  bool finished = false;
  int8_t blinkRow = -1;
  int8_t blinkCol = -1;
  LedRGB blinkColor = LedColors::Off;
  int blinkTimes = 0;
  int blinkCount = 0;
  uint32_t waitedMs = 0;

 private:
  void copyOptions(const MenuOption* source, uint8_t optionCount) {
    count = optionCount;
    for (uint8_t i = 0; i < count; ++i) options[i] = source[i];
  }
};

bool sameColor(LedRGB a, LedRGB b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

void test_game_selection_root_leaf_finishes() {
  GameSelectionMenu menu;
  FakeMenuController controller;

  menu.begin(controller);
  TEST_ASSERT_TRUE(controller.hasOption(GameSelectionMenuOptionId::CHESS_MOVES));
  TEST_ASSERT_TRUE(controller.hasOption(GameSelectionMenuOptionId::BOT));

  menu.onSelect(GameSelectionMenuOptionId::CHESS_MOVES, controller);
  TEST_ASSERT_TRUE(controller.finished);
  TEST_ASSERT_TRUE(menu.hasSelection());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BoardGameSelectionMode::CHESS_MOVES),
                          static_cast<uint8_t>(menu.selection().mode));
}

void test_game_selection_bot_difficulty_and_color_flow() {
  GameSelectionMenu menu;
  FakeMenuController controller;

  menu.begin(controller);
  menu.onSelect(GameSelectionMenuOptionId::BOT, controller);
  TEST_ASSERT_FALSE(controller.finished);
  TEST_ASSERT_TRUE(controller.hasBack);
  TEST_ASSERT_TRUE(controller.hasOption(GameSelectionMenuOptionId::DIFF_6));

  menu.onSelect(GameSelectionMenuOptionId::DIFF_6, controller);
  TEST_ASSERT_FALSE(controller.finished);
  TEST_ASSERT_TRUE(controller.hasBack);
  TEST_ASSERT_TRUE(controller.hasOption(GameSelectionMenuOptionId::PLAY_BLACK));

  menu.onSelect(GameSelectionMenuOptionId::PLAY_BLACK, controller);
  TEST_ASSERT_TRUE(controller.finished);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BoardGameSelectionMode::BOT),
                          static_cast<uint8_t>(menu.selection().mode));
  TEST_ASSERT_EQUAL_UINT8(6, menu.selection().botDifficulty);
  TEST_ASSERT_EQUAL_CHAR('b', menu.selection().playerColor);
}

void test_game_selection_back_navigation_returns_to_previous_page() {
  GameSelectionMenu menu;
  FakeMenuController controller;

  menu.begin(controller);
  menu.onSelect(GameSelectionMenuOptionId::BOT, controller);
  menu.onSelect(GameSelectionMenuOptionId::DIFF_4, controller);
  TEST_ASSERT_TRUE(controller.hasOption(GameSelectionMenuOptionId::PLAY_WHITE));

  menu.onBack(controller);
  TEST_ASSERT_TRUE(controller.hasOption(GameSelectionMenuOptionId::DIFF_4));

  menu.onBack(controller);
  TEST_ASSERT_TRUE(controller.hasOption(GameSelectionMenuOptionId::CHESS_MOVES));
  TEST_ASSERT_FALSE(controller.finished);
}

void test_confirm_menu_yes_and_no_results() {
  ConfirmMenu menu;
  FakeMenuController controller;

  menu.begin(controller);
  TEST_ASSERT_TRUE(controller.hasOption(ConfirmMenuOptionId::CONFIRM_YES));
  TEST_ASSERT_TRUE(controller.hasOption(ConfirmMenuOptionId::CONFIRM_NO));

  menu.onSelect(ConfirmMenuOptionId::CONFIRM_YES, controller);
  TEST_ASSERT_TRUE(controller.finished);
  TEST_ASSERT_TRUE(menu.accepted());

  menu.begin(controller);
  menu.onSelect(ConfirmMenuOptionId::CONFIRM_NO, controller);
  TEST_ASSERT_TRUE(controller.finished);
  TEST_ASSERT_TRUE(menu.answered());
  TEST_ASSERT_FALSE(menu.accepted());
}

void test_resume_confirm_menu_preblink_then_shows_confirm() {
  ResumeConfirmMenu menu(BoardGameSelectionMode::BOT);
  FakeMenuController controller;

  menu.begin(controller);
  TEST_ASSERT_EQUAL_INT(1, controller.blinkCount);
  TEST_ASSERT_EQUAL_INT(3, controller.blinkRow);
  TEST_ASSERT_EQUAL_INT(3, controller.blinkCol);
  TEST_ASSERT_EQUAL_INT(2, controller.blinkTimes);
  TEST_ASSERT_TRUE(sameColor(controller.blinkColor, LedColors::Green));
  TEST_ASSERT_EQUAL_UINT32(900, controller.waitedMs);
  TEST_ASSERT_TRUE(controller.hasOption(ConfirmMenuOptionId::CONFIRM_YES));
}

}  // namespace

void register_menu_tests() {
  RUN_TEST(test_game_selection_root_leaf_finishes);
  RUN_TEST(test_game_selection_bot_difficulty_and_color_flow);
  RUN_TEST(test_game_selection_back_navigation_returns_to_previous_page);
  RUN_TEST(test_confirm_menu_yes_and_no_results);
  RUN_TEST(test_resume_confirm_menu_preblink_then_shows_confirm);
}