// Tests for typed physical-board menu state machines.
//
// These tests exercise menu hooks directly using a FakeMenuFlow stub. They
// do not depend on BoardMenuRunner, BoardRuntime, FreeRTOS, or Arduino —
// keeping the suite native-host compatible.

#include <unity.h>

#include "board/menus/confirm.h"
#include "board/menus/game_selection.h"
#include "board/services/menu/selection.h"

namespace {

// ---------------------------------------------------------------------------
// FakeMenuFlow — records hook invocations and lets tests script the current
// page id between calls.
// ---------------------------------------------------------------------------

class FakeMenuFlow final : public MenuFlow {
 public:
  void next(uint8_t pageId) override {
    ++nextCalls;
    lastNextPage = pageId;
  }
  void back() override { ++backCalls; }
  void close() override { ++closeCalls; }
  uint8_t currentPage() const override { return currentPage_; }
  void blink(int8_t row, int8_t col, LedRGB color, int times) override {
    ++blinkCount;
    blinkRow = row;
    blinkCol = col;
    blinkColor = color;
    blinkTimes = times;
  }
  void wait(uint32_t durationMs) override { waitedMs += durationMs; }

  uint8_t currentPage_ = 0;
  int nextCalls = 0;
  uint8_t lastNextPage = 0;
  int backCalls = 0;
  int closeCalls = 0;
  int blinkCount = 0;
  int8_t blinkRow = -1;
  int8_t blinkCol = -1;
  LedRGB blinkColor = LedColors::Off;
  int blinkTimes = 0;
  uint32_t waitedMs = 0;
};

bool sameColor(LedRGB a, LedRGB b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

const MenuTile* findTile(const BoardMenu& menu, uint8_t tileId, uint8_t pageId) {
  const MenuTile* tiles = menu.tiles();
  for (uint8_t i = 0; i < menu.tileCount(); ++i) {
    if (tiles[i].pageId == pageId && tiles[i].tileId == tileId) return &tiles[i];
  }
  return nullptr;
}

bool menuHasTileOnPage(const BoardMenu& menu, uint8_t tileId, uint8_t pageId) {
  return findTile(menu, tileId, pageId) != nullptr;
}

// ---------------------------------------------------------------------------
// MenuSelection debounce
// ---------------------------------------------------------------------------

void test_menu_selection_confirms_on_release_after_press() {
  MenuSelection::SelectionDebouncer debouncer(2);
  using Result = MenuSelection::SelectionDebouncer::Result;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(true)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(true)));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(false)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(false)));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(true)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::PRESSED),
                          static_cast<uint8_t>(debouncer.update(true)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(true)));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(false)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::RELEASED),
                          static_cast<uint8_t>(debouncer.update(false)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(false)));
}

void test_menu_selection_ignores_short_press_before_release() {
  MenuSelection::SelectionDebouncer debouncer(2);
  using Result = MenuSelection::SelectionDebouncer::Result;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(false)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(false)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(true)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(false)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Result::NONE),
                          static_cast<uint8_t>(debouncer.update(false)));
}

// ---------------------------------------------------------------------------
// GameSelectionMenu
// ---------------------------------------------------------------------------

void test_game_selection_root_chess_moves_closes_with_selection() {
  GameSelectionMenu menu;
  FakeMenuFlow flow;
  flow.currentPage_ = GAME_SELECTION_PAGE_GAME;

  menu.onOpen(GAME_SELECTION_PAGE_GAME, flow);
  TEST_ASSERT_TRUE(menuHasTileOnPage(menu, GameSelectionMenuOptionId::CHESS_MOVES,
                                     GAME_SELECTION_PAGE_GAME));
  TEST_ASSERT_TRUE(menuHasTileOnPage(menu, GameSelectionMenuOptionId::BOT,
                                     GAME_SELECTION_PAGE_GAME));

  menu.onSelect(GameSelectionMenuOptionId::CHESS_MOVES, flow);
  TEST_ASSERT_TRUE(menu.hasSelection());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BoardGameSelectionMode::CHESS_MOVES),
                          static_cast<uint8_t>(menu.selection().mode));

  // CHESS_MOVES tile carries autoAdvance CLOSE.
  const MenuTile* tile = findTile(menu, GameSelectionMenuOptionId::CHESS_MOVES,
                                  GAME_SELECTION_PAGE_GAME);
  TEST_ASSERT_NOT_NULL(tile);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MenuAdvance::CLOSE),
                          static_cast<uint8_t>(tile->autoAdvance));
}

void test_game_selection_bot_difficulty_and_color_flow() {
  GameSelectionMenu menu;
  FakeMenuFlow flow;
  flow.currentPage_ = GAME_SELECTION_PAGE_GAME;
  menu.onOpen(GAME_SELECTION_PAGE_GAME, flow);

  // ---- Root → DIFFICULTY ----
  menu.onSelect(GameSelectionMenuOptionId::BOT, flow);
  const MenuTile* bot = findTile(menu, GameSelectionMenuOptionId::BOT,
                                 GAME_SELECTION_PAGE_GAME);
  TEST_ASSERT_NOT_NULL(bot);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MenuAdvance::NEXT),
                          static_cast<uint8_t>(bot->autoAdvance));
  TEST_ASSERT_EQUAL_UINT8(GAME_SELECTION_PAGE_DIFFICULTY, bot->autoAdvanceTarget);

  TEST_ASSERT_TRUE(menuHasTileOnPage(menu, GameSelectionMenuOptionId::DIFF_6,
                                     GAME_SELECTION_PAGE_DIFFICULTY));

  // ---- DIFFICULTY → COLOR ----
  flow.currentPage_ = GAME_SELECTION_PAGE_DIFFICULTY;
  menu.onSelect(GameSelectionMenuOptionId::DIFF_6, flow);
  const MenuTile* diff6 = findTile(menu, GameSelectionMenuOptionId::DIFF_6,
                                   GAME_SELECTION_PAGE_DIFFICULTY);
  TEST_ASSERT_NOT_NULL(diff6);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MenuAdvance::NEXT),
                          static_cast<uint8_t>(diff6->autoAdvance));
  TEST_ASSERT_EQUAL_UINT8(GAME_SELECTION_PAGE_COLOR, diff6->autoAdvanceTarget);

  // ---- COLOR → CLOSE ----
  flow.currentPage_ = GAME_SELECTION_PAGE_COLOR;
  TEST_ASSERT_TRUE(menuHasTileOnPage(menu, GameSelectionMenuOptionId::PLAY_BLACK,
                                     GAME_SELECTION_PAGE_COLOR));
  menu.onSelect(GameSelectionMenuOptionId::PLAY_BLACK, flow);
  const MenuTile* playBlack = findTile(menu, GameSelectionMenuOptionId::PLAY_BLACK,
                                       GAME_SELECTION_PAGE_COLOR);
  TEST_ASSERT_NOT_NULL(playBlack);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MenuAdvance::CLOSE),
                          static_cast<uint8_t>(playBlack->autoAdvance));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BoardGameSelectionMode::BOT),
                          static_cast<uint8_t>(menu.selection().mode));
  TEST_ASSERT_EQUAL_UINT8(6, menu.selection().botDifficulty);
  TEST_ASSERT_EQUAL_CHAR('b', menu.selection().playerColor);
}

void test_game_selection_back_to_root_resets_state() {
  GameSelectionMenu menu;
  FakeMenuFlow flow;
  menu.onOpen(GAME_SELECTION_PAGE_GAME, flow);
  menu.onSelect(GameSelectionMenuOptionId::BOT, flow);
  menu.onSelect(GameSelectionMenuOptionId::DIFF_4, flow);
  TEST_ASSERT_EQUAL_UINT8(4, menu.selection().botDifficulty);

  // Back COLOR → DIFFICULTY: clears player color but keeps difficulty.
  menu.onBack(GAME_SELECTION_PAGE_COLOR, GAME_SELECTION_PAGE_DIFFICULTY, flow);
  TEST_ASSERT_EQUAL_CHAR(' ', menu.selection().playerColor);

  // Back DIFFICULTY → GAME: clears the entire selection.
  menu.onBack(GAME_SELECTION_PAGE_DIFFICULTY, GAME_SELECTION_PAGE_GAME, flow);
  TEST_ASSERT_FALSE(menu.hasSelection());
}

void test_game_selection_page_config_back_tiles() {
  GameSelectionMenu menu;
  MenuPageConfig rootCfg = menu.pageConfig(GAME_SELECTION_PAGE_GAME);
  MenuPageConfig diffCfg = menu.pageConfig(GAME_SELECTION_PAGE_DIFFICULTY);
  MenuPageConfig colorCfg = menu.pageConfig(GAME_SELECTION_PAGE_COLOR);

  // Root page has no back tile; nested pages do (at (4,4)).
  TEST_ASSERT_EQUAL_INT(-1, rootCfg.backRow);
  TEST_ASSERT_EQUAL_INT(-1, rootCfg.backCol);
  TEST_ASSERT_EQUAL_INT(4, diffCfg.backRow);
  TEST_ASSERT_EQUAL_INT(4, diffCfg.backCol);
  TEST_ASSERT_EQUAL_INT(4, colorCfg.backRow);
  TEST_ASSERT_EQUAL_INT(4, colorCfg.backCol);
}

// ---------------------------------------------------------------------------
// ConfirmMenu / ResumeConfirmMenu
// ---------------------------------------------------------------------------

void test_confirm_menu_yes_and_no_results() {
  ConfirmMenu menu;
  FakeMenuFlow flow;

  menu.onOpen(0, flow);
  TEST_ASSERT_TRUE(menuHasTileOnPage(menu, ConfirmMenuOptionId::CONFIRM_YES, 0));
  TEST_ASSERT_TRUE(menuHasTileOnPage(menu, ConfirmMenuOptionId::CONFIRM_NO, 0));

  menu.onSelect(ConfirmMenuOptionId::CONFIRM_YES, flow);
  TEST_ASSERT_TRUE(menu.answered());
  TEST_ASSERT_TRUE(menu.accepted());

  // Re-opening resets the recorded answers.
  menu.onOpen(0, flow);
  TEST_ASSERT_FALSE(menu.answered());

  menu.onSelect(ConfirmMenuOptionId::CONFIRM_NO, flow);
  TEST_ASSERT_TRUE(menu.answered());
  TEST_ASSERT_FALSE(menu.accepted());

  // Both confirm tiles carry autoAdvance CLOSE.
  const MenuTile* yes = findTile(menu, ConfirmMenuOptionId::CONFIRM_YES, 0);
  const MenuTile* no = findTile(menu, ConfirmMenuOptionId::CONFIRM_NO, 0);
  TEST_ASSERT_NOT_NULL(yes);
  TEST_ASSERT_NOT_NULL(no);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MenuAdvance::CLOSE),
                          static_cast<uint8_t>(yes->autoAdvance));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MenuAdvance::CLOSE),
                          static_cast<uint8_t>(no->autoAdvance));
}

void test_resume_confirm_menu_preblink_then_shows_confirm() {
  ResumeConfirmMenu menu(BoardGameSelectionMode::BOT);
  FakeMenuFlow flow;

  menu.onOpen(0, flow);
  TEST_ASSERT_EQUAL_INT(1, flow.blinkCount);
  TEST_ASSERT_EQUAL_INT(3, flow.blinkRow);
  TEST_ASSERT_EQUAL_INT(3, flow.blinkCol);
  TEST_ASSERT_EQUAL_INT(2, flow.blinkTimes);
  TEST_ASSERT_TRUE(sameColor(flow.blinkColor, LedColors::Green));
  TEST_ASSERT_EQUAL_UINT32(900, flow.waitedMs);
  TEST_ASSERT_TRUE(menuHasTileOnPage(menu, ConfirmMenuOptionId::CONFIRM_YES, 0));
}

}  // namespace

void register_menu_tests() {
  RUN_TEST(test_menu_selection_confirms_on_release_after_press);
  RUN_TEST(test_menu_selection_ignores_short_press_before_release);
  RUN_TEST(test_game_selection_root_chess_moves_closes_with_selection);
  RUN_TEST(test_game_selection_bot_difficulty_and_color_flow);
  RUN_TEST(test_game_selection_back_to_root_resets_state);
  RUN_TEST(test_game_selection_page_config_back_tiles);
  RUN_TEST(test_confirm_menu_yes_and_no_results);
  RUN_TEST(test_resume_confirm_menu_preblink_then_shows_confirm);
}
