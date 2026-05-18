#include "board/services/menu/menu.h"

#include "board/runtime/runtime.h"
#include "board/services/visual/animations.h"

#include <Arduino.h>

BoardMenuRunner::BoardMenuRunner(BoardRuntime& runtime, BoardAnimations& animations)
    : runtime_(runtime),
      animations_(animations),
      selection_(runtime),
      activeMenu_(nullptr),
      finished_(false),
      pageStackDepth_(0),
      pending_(Pending::NONE),
      pendingNextTarget_(0) {}

void BoardMenuRunner::show(BoardMenu& menu, bool flipped) {
  // Tear down any in-flight menu cleanly so this call is idempotent.
  stop();
  activeMenu_ = &menu;
  finished_ = false;
  pageStackDepth_ = 0;
  pending_ = Pending::NONE;
  selection_.setFlipped(flipped);

  const uint8_t initial = activeMenu_->initialPage();
  pageStack_[pageStackDepth_++] = initial;
  activeMenu_->onOpen(initial, *this);
  applyPendingTransitions();
  if (finished_ || activeMenu_ == nullptr) {
    clearActiveMenu();
    return;
  }
  drawCurrentPage();
}

bool BoardMenuRunner::poll() {
  if (!activeMenu_) return false;

  int result = selection_.poll();
  if (result == MENU_RESULT_NONE) return false;

  return processSelectionResult(result);
}

bool BoardMenuRunner::processSelectionResult(int result) {
  if (!activeMenu_) return false;

  if (result == MENU_RESULT_BACK) {
    // Back tile press is treated as a queued back transition. Hooks may
    // observe / override via onBack/onClose.
    back();
  } else {
    const uint8_t tileId = static_cast<uint8_t>(result);
    activeMenu_->onSelect(tileId, *this);

    // Apply this tile's auto-advance after onSelect so the hook records its
    // state first. An explicit hook transition (flow.next/back/close) wins
    // because applyPendingTransitions runs the queued action; the
    // auto-advance only takes effect when pending is still NONE.
    if (pending_ == Pending::NONE) {
      const uint8_t pageId = pageStack_[pageStackDepth_ - 1];
      const MenuTile* tiles = activeMenu_->tiles();
      const uint8_t count = activeMenu_->tileCount();
      for (uint8_t i = 0; i < count; ++i) {
        if (tiles[i].pageId != pageId || tiles[i].tileId != tileId) continue;
        if (tiles[i].autoAdvance == MenuAdvance::NEXT) {
          next(tiles[i].autoAdvanceTarget);
        } else if (tiles[i].autoAdvance == MenuAdvance::CLOSE) {
          close();
        }
        break;
      }
    }
  }

  applyPendingTransitions();
  if (finished_) {
    clearActiveMenu();
    return true;
  }
  if (activeMenu_) drawCurrentPage();
  return false;
}

bool BoardMenuRunner::run(BoardMenu& menu, bool flipped) {
  show(menu, flipped);
  while (activeMenu_ != nullptr) {
    if (poll()) return true;
    delay(runtime_.cadenceMs());
  }
  return true;
}

void BoardMenuRunner::stop() {
  if (activeMenu_) {
    activeMenu_->onClose(*this);
    activeMenu_ = nullptr;
  }
  selection_.erase();
  finished_ = false;
  pageStackDepth_ = 0;
  pending_ = Pending::NONE;
}

// ---------------------------------------------------------------------------
// MenuFlow implementation — queues transitions for applyPendingTransitions().
// ---------------------------------------------------------------------------

void BoardMenuRunner::next(uint8_t pageId) {
  pending_ = Pending::NEXT;
  pendingNextTarget_ = pageId;
}

void BoardMenuRunner::back() { pending_ = Pending::BACK; }

void BoardMenuRunner::close() { pending_ = Pending::CLOSE; }

uint8_t BoardMenuRunner::currentPage() const {
  return pageStackDepth_ > 0 ? pageStack_[pageStackDepth_ - 1] : 0;
}

void BoardMenuRunner::blink(int8_t row, int8_t col, LedRGB color, int times) {
  auto g = runtime_.lockCanvas();
  animations_.startBlink(row, col, color, times, millis());
}

void BoardMenuRunner::wait(uint32_t durationMs) { delay(durationMs); }

// ---------------------------------------------------------------------------
// Transition + draw helpers.
// ---------------------------------------------------------------------------

void BoardMenuRunner::applyPendingTransitions() {
  // Hooks may queue further transitions; loop until idle. Guarded by a hard
  // stack-depth limit so a buggy menu cannot livelock the runner.
  while (pending_ != Pending::NONE && !finished_ && activeMenu_ != nullptr) {
    const Pending current = pending_;
    pending_ = Pending::NONE;

    if (current == Pending::NEXT) {
      if (pageStackDepth_ >= MENU_PAGE_STACK_DEPTH) {
        Serial.println("BoardMenuRunner: page stack overflow; closing menu");
        activeMenu_->onClose(*this);
        finished_ = true;
        return;
      }
      const uint8_t fromPage = pageStack_[pageStackDepth_ - 1];
      const uint8_t toPage = pendingNextTarget_;
      pageStack_[pageStackDepth_++] = toPage;
      activeMenu_->onNext(fromPage, toPage, *this);
    } else if (current == Pending::BACK) {
      if (pageStackDepth_ <= 1) {
        // Back from the root page closes the menu.
        activeMenu_->onClose(*this);
        finished_ = true;
        return;
      }
      const uint8_t fromPage = pageStack_[pageStackDepth_ - 1];
      --pageStackDepth_;
      const uint8_t toPage = pageStack_[pageStackDepth_ - 1];
      activeMenu_->onBack(fromPage, toPage, *this);
    } else {  // Pending::CLOSE
      activeMenu_->onClose(*this);
      finished_ = true;
      return;
    }
  }
}

void BoardMenuRunner::drawCurrentPage() {
  const uint8_t pageId = pageStack_[pageStackDepth_ - 1];
  const MenuTile* tiles = activeMenu_->tiles();
  const uint8_t count = activeMenu_->tileCount();

  MenuOption options[MENU_SELECTION_OPTION_COUNT];
  uint8_t optionCount = 0;
  for (uint8_t i = 0; i < count && optionCount < MENU_SELECTION_OPTION_COUNT; ++i) {
    if (tiles[i].pageId != pageId) continue;
    options[optionCount++] = MenuOption{
        static_cast<int8_t>(tiles[i].row),
        static_cast<int8_t>(tiles[i].col),
        tiles[i].color,
        static_cast<int8_t>(tiles[i].tileId),
    };
  }

  selection_.setOptions(options, optionCount);
  const MenuPageConfig config = activeMenu_->pageConfig(pageId);
  if (config.backRow >= 0 && config.backCol >= 0) {
    selection_.setBackButton(config.backRow, config.backCol);
  } else {
    selection_.clearBackButton();
  }
  selection_.reset();
  selection_.draw();
}

void BoardMenuRunner::clearActiveMenu() {
  selection_.erase();
  activeMenu_ = nullptr;
  finished_ = false;
  pageStackDepth_ = 0;
  pending_ = Pending::NONE;
}
