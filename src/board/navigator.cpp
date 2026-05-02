#include "navigator.h"

#include <Arduino.h>

MenuNavigator::MenuNavigator()
  : stack_(),
      depth_(-1) {
  stack_.fill(nullptr);
}

void MenuNavigator::push(BoardMenu* menu) {
  if (depth_ >= MAX_DEPTH - 1) {
    Serial.println("MenuNavigator: stack overflow, ignoring push");
    return;
  }
  ++depth_;
  stack_[depth_] = menu;
  menu->reset();
  menu->show();
}

void MenuNavigator::pop() {
  if (depth_ < 0)
    return;

  stack_[depth_] = nullptr;
  --depth_;

  // Re-show the parent menu if one exists
  if (depth_ >= 0) {
    stack_[depth_]->reset();
    stack_[depth_]->show();
  }
}

int MenuNavigator::poll() {
  if (depth_ < 0)
    return BoardMenu::RESULT_NONE;

  int result = stack_[depth_]->poll();

  if (result == BoardMenu::RESULT_BACK) {
    pop();
    // If we popped past root, signal the caller
    if (depth_ < 0)
      return BoardMenu::RESULT_BACK;
    // Otherwise the parent is now showing — keep polling
    return BoardMenu::RESULT_NONE;
  }

  return result;
}

BoardMenu* MenuNavigator::current() const {
  if (depth_ < 0)
    return nullptr;
  return stack_[depth_];
}

int8_t MenuNavigator::depth() const {
  return static_cast<int8_t>(depth_ + 1); // 0 = empty, 1 = one menu, etc.
}

bool MenuNavigator::empty() const {
  return depth_ < 0;
}

void MenuNavigator::clear() {
  if (depth_ >= 0) {
    stack_[depth_]->hide();
  }
  for (int8_t i = 0; i <= depth_; ++i)
    stack_[i] = nullptr;
  depth_ = -1;
}
