#ifndef BOARD_STACK_H
#define BOARD_STACK_H

#include "drawable.h"

#include <array>
#include <stdint.h>

/// Board-internal modal visual stack for menus and future stackable screens.
class BoardStack {
 public:
  static constexpr int8_t MAX_DEPTH = 4;

  BoardStack();

  BoardStack(const BoardStack&) = delete;
  BoardStack& operator=(const BoardStack&) = delete;

  /// Push a drawable onto the stack. Calls reset() and show().
  void push(BoardDrawable* drawable);

  /// Pop the current drawable and re-show the parent if one exists.
  void pop();

  /// Poll the current drawable and handle back navigation.
  int poll();

  /// Pointer to the active drawable, or nullptr when empty.
  BoardDrawable* current() const;

  /// Current stack depth, where 0 means empty.
  int8_t depth() const;

  /// Return whether no drawable is active.
  bool empty() const;

  /// Clear the stack and hide the active drawable.
  void clear();

 private:
  std::array<BoardDrawable*, MAX_DEPTH> stack_;
  int8_t topIndex_;
};

#endif  // BOARD_STACK_H
