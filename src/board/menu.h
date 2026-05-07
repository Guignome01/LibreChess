#ifndef BOARD_MENU_H
#define BOARD_MENU_H

#include "gui/menu.h"
#include "gui/selection.h"
#include "workflow.h"

#include <stdint.h>

/// External board workflow that owns the physical-board selection menu flow
/// (game/difficulty/color), plus modal yes/no and resume confirmation prompts.
/// `BoardConfirm` is folded in as a method here because it is structurally a
/// modal menu prompt rather than a separate workflow.
class BoardMenu : private BoardWorkflow {
 public:
  using GameSelectionMode = BoardGameSelectionMode;
  using GameSelection = BoardGameSelection;

  explicit BoardMenu(BoardController& board);

  BoardMenu(const BoardMenu&) = delete;
  BoardMenu& operator=(const BoardMenu&) = delete;

  // --- Game selection menu ---

  /// Push the root game-selection menu onto the modal stack.
  void start();

  /// Hide the active selection menu and reset the modal stack.
  void clear();

  /// Refresh sensors and poll the modal stack. Returns a non-empty selection
  /// only after the user has fully chosen a game configuration.
  GameSelection poll();

  // --- Modal confirmation prompts ---

  /// Blocking yes/no confirmation. Green at d4 (yes), red at e4 (no).
  bool confirmAction(bool flipped = false);

  /// Show a resume indicator and then ask for confirmation.
  bool confirmResume(GameSelectionMode mode, bool flipped = false);

 private:
  MenuView gameMenu_;
  MenuView botDifficultyMenu_;
  MenuView botColorMenu_;
  uint8_t pendingBotDifficulty_;
};

#endif  // BOARD_MENU_H
