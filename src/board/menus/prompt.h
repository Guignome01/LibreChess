#ifndef BOARD_MENUS_PROMPT_H
#define BOARD_MENUS_PROMPT_H

class BoardRuntime;

// ---------------------------------------------------------------------------
// MenuPrompt — modal menu prompts built on the shared panel primitive
// ---------------------------------------------------------------------------

class MenuPrompt {
 public:
  /// Blocking yes/no confirmation: green=yes, red=no.
  static bool confirm(BoardRuntime& runtime, bool flipped = false);
};

/// Compatibility helper for existing workflows.
bool confirmBoardPrompt(BoardRuntime& runtime, bool flipped);

#endif  // BOARD_MENUS_PROMPT_H
