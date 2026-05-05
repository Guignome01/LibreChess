#include "stack.h"

#include <Arduino.h>

BoardStack::BoardStack() : stack_(), topIndex_(-1) {
  stack_.fill(nullptr);
}

void BoardStack::push(BoardDrawable* drawable) {
  if (topIndex_ >= MAX_DEPTH - 1) {
    Serial.println("BoardStack: stack overflow, ignoring push");
    return;
  }
  ++topIndex_;
  stack_[topIndex_] = drawable;
  drawable->reset();
  drawable->show();
}

void BoardStack::pop() {
  if (topIndex_ < 0)
    return;

  stack_[topIndex_] = nullptr;
  --topIndex_;

  if (topIndex_ >= 0) {
    stack_[topIndex_]->reset();
    stack_[topIndex_]->show();
  }
}

int BoardStack::poll() {
  if (topIndex_ < 0)
    return BoardDrawable::RESULT_NONE;

  int result = stack_[topIndex_]->poll();

  if (result == BoardDrawable::RESULT_BACK) {
    pop();
    if (topIndex_ < 0)
      return BoardDrawable::RESULT_BACK;
    return BoardDrawable::RESULT_NONE;
  }

  return result;
}

BoardDrawable* BoardStack::current() const {
  if (topIndex_ < 0)
    return nullptr;
  return stack_[topIndex_];
}

int8_t BoardStack::depth() const {
  return static_cast<int8_t>(topIndex_ + 1);
}

bool BoardStack::empty() const {
  return topIndex_ < 0;
}

void BoardStack::clear() {
  if (topIndex_ >= 0) {
    stack_[topIndex_]->hide();
  }
  for (int8_t i = 0; i <= topIndex_; ++i)
    stack_[i] = nullptr;
  topIndex_ = -1;
}
