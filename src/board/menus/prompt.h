#ifndef BOARD_MENUS_PROMPT_H
#define BOARD_MENUS_PROMPT_H

class BoardRuntime;
class BoardAnimations;

// ---------------------------------------------------------------------------
// MenuPrompt — modal menu prompts built on the shared panel primitive
// ---------------------------------------------------------------------------

class MenuPrompt {
 public:
  /// Blocking yes/no confirmation: green=yes, red=no.
  static bool confirm(BoardRuntime& runtime, BoardAnimations& animations, bool flipped = false);
};

#endif  // BOARD_MENUS_PROMPT_H
