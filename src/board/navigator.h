#ifndef BOARD_NAVIGATOR_H
#define BOARD_NAVIGATOR_H

#include "menu.h"
#include <array>
#include <stdint.h>

// ---------------------------
// Menu Navigator
// ---------------------------
// Stack-based menu orchestrator. Manages push/pop navigation across
// BoardMenu instances. Automatically handles back-button pops and
// re-displays the parent menu.
//
// The navigator does NOT own the BoardMenu objects — they must outlive
// the navigator (use file-scoped statics in main.cpp).

class MenuNavigator {
 public:
  static constexpr int8_t MAX_DEPTH = 4;

  MenuNavigator();

  // Delete copy
  MenuNavigator(const MenuNavigator&) = delete;
  MenuNavigator& operator=(const MenuNavigator&) = delete;

  /// Push a menu onto the stack. Calls menu->reset() and menu->show().
  /// Asserts depth < MAX_DEPTH.
  void push(BoardMenu* menu);

  /// Pop the current menu. Re-shows the parent if one exists.
  /// No-op if stack is empty.
  void pop();

  /// Non-blocking poll of the current menu.
  /// On RESULT_BACK: auto-pops and re-shows parent.
  /// Returns:
  ///   - MenuItem id on selection
  ///   - RESULT_BACK only when popping past root (stack now empty)
  ///   - RESULT_NONE if no selection yet or after an internal pop
  int poll();

  /// Pointer to the currently active menu, or nullptr if empty.
  BoardMenu* current() const;

  /// Current stack depth (0 = empty).
  int8_t depth() const;

  /// Whether the stack is empty.
  bool empty() const;

  /// Clear the entire stack. Hides the current menu.
  /// Use when an external event (WiFi, game-over) overrides the menu.
  void clear();

 private:
  std::array<BoardMenu*, MAX_DEPTH> stack_;
  int8_t depth_; // Index of top element, -1 = empty
};

#endif // BOARD_NAVIGATOR_H
