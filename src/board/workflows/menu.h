#ifndef BOARD_WORKFLOWS_MENU_H
#define BOARD_WORKFLOWS_MENU_H

#include "board/menus/selection.h"
#include "board/menus/view.h"

#include <stdint.h>

class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardMenu — game-selection workflow + modal confirmation prompts.
// ---------------------------------------------------------------------------
// Owns three `MenuView` instances (root / bot-difficulty / bot-color) and
// drives them through a small explicit state machine. `confirmAction` and
// `confirmResume` reuse a transient `MenuView` for a green/red yes/no prompt.
//
// The blocking `confirmAction` / `confirmResume` API is preserved for now;
// internally it polls at sensor cadence and the renderer keeps the prompt
// visible. This keeps callers (BotMode resign etc.) unchanged.
// ---------------------------------------------------------------------------

class BoardMenu {
 public:
  using GameSelectionMode = BoardGameSelectionMode;
  using GameSelection = BoardGameSelection;

  explicit BoardMenu(BoardRuntime& runtime);

  BoardMenu(const BoardMenu&) = delete;
  BoardMenu& operator=(const BoardMenu&) = delete;

  // -------------------------------------------------------------------------
  // Game-selection menu
  // -------------------------------------------------------------------------

  /// Enter the root game-selection menu. Idempotent.
  void start();

  /// Reset the navigator and erase the active menu surface.
  void clear();

  /// Non-blocking poll; returns a non-empty selection only after the user
  /// has confirmed a complete game configuration.
  GameSelection poll();

  // -------------------------------------------------------------------------
  // Modal confirmation prompts
  // -------------------------------------------------------------------------

  /// Blocking yes/no confirmation: green=d4 (yes), red=e4 (no).
  bool confirmAction(bool flipped = false);

  /// Show a resume indicator blink (mode-coloured), then prompt confirm.
  bool confirmResume(GameSelectionMode mode, bool flipped = false);

 private:
  enum class Stage : uint8_t {
    IDLE,
    GAME,
    DIFFICULTY,
    COLOR,
  };

  BoardRuntime& runtime_;
  MenuView gameMenu_;
  MenuView botDifficultyMenu_;
  MenuView botColorMenu_;
  uint8_t pendingBotDifficulty_;
  Stage stage_;

  MenuView* activeView();
};

#endif  // BOARD_WORKFLOWS_MENU_H
