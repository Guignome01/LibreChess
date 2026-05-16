#include "board/services/menu/menu.h"

#include "board/runtime/runtime.h"
#include "board/services/visual/animations.h"

#include <Arduino.h>
#include <utility>

BoardMenuRunner::BoardMenuRunner(BoardRuntime& runtime, BoardAnimations& animations)
    : runtime_(runtime),
      animations_(animations),
      selection_(runtime, animations),
      activeMenu_(nullptr),
      finished_(false) {}

void BoardMenuRunner::show(BoardMenu& menu, bool flipped) {
  stop();
  activeMenu_ = &menu;
  finished_ = false;
  selection_.setFlipped(flipped);
  activeMenu_->begin(*this);
  if (finished_) finishActiveMenu();
}

bool BoardMenuRunner::poll() {
  if (!activeMenu_) return false;

  int result = selection_.poll();
  if (result == MENU_RESULT_NONE) return false;

  if (result == MENU_RESULT_BACK) {
    activeMenu_->onBack(*this);
  } else {
    activeMenu_->onSelect(result, *this);
  }

  if (finished_) {
    finishActiveMenu();
    return true;
  }
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
  if (activeMenu_) activeMenu_->cancel(*this);
  selection_.erase();
  activeMenu_ = nullptr;
  finished_ = false;
}

void BoardMenuRunner::show(const MenuOption* options, uint8_t count) {
  selection_.clearBackButton();
  selection_.setOptions(options, count);
  selection_.reset();
  selection_.draw();
}

void BoardMenuRunner::showWithBack(const MenuOption* options, uint8_t count, int8_t backRow,
                                   int8_t backCol) {
  selection_.setOptions(options, count);
  selection_.setBackButton(backRow, backCol);
  selection_.reset();
  selection_.draw();
}

void BoardMenuRunner::erase() {
  selection_.erase();
}

void BoardMenuRunner::finish() {
  finished_ = true;
}

void BoardMenuRunner::blink(int8_t row, int8_t col, LedRGB color, int times) {
  auto g = runtime_.lockCanvas();
  animations_.startBlink(row, col, color, times, millis());
}

void BoardMenuRunner::wait(uint32_t durationMs) {
  delay(durationMs);
}

void BoardMenuRunner::finishActiveMenu() {
  selection_.erase();
  activeMenu_ = nullptr;
  finished_ = false;
}