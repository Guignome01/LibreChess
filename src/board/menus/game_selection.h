#ifndef BOARD_MENUS_GAME_SELECTION_H
#define BOARD_MENUS_GAME_SELECTION_H

#include "board/services/menu/menu.h"

#include <stdint.h>

/// Public game mode selected through the physical board menu.
enum class BoardGameSelectionMode : uint8_t {
  NONE = 0,
  CHESS_MOVES = 1,
  BOT = 2,
  LICHESS = 3,
  BOARD_DIAGNOSTICS = 4,
};

/// Public game selection returned by the physical board menu flow.
struct BoardGameSelection {
  BoardGameSelectionMode mode = BoardGameSelectionMode::NONE;
  uint8_t botDifficulty = 0;
  char playerColor = ' ';

  bool hasSelection() const { return mode != BoardGameSelectionMode::NONE; }
};

/// Identifiers returned when a game-selection menu option is selected.
namespace GameSelectionMenuOptionId {
constexpr int8_t CHESS_MOVES = 0;
constexpr int8_t BOT = 1;
constexpr int8_t LICHESS = 2;
constexpr int8_t BOARD_DIAGNOSTICS = 3;

constexpr int8_t DIFF_1 = 10;
constexpr int8_t DIFF_2 = 11;
constexpr int8_t DIFF_3 = 12;
constexpr int8_t DIFF_4 = 13;
constexpr int8_t DIFF_5 = 14;
constexpr int8_t DIFF_6 = 15;
constexpr int8_t DIFF_7 = 16;
constexpr int8_t DIFF_8 = 17;

constexpr int8_t PLAY_WHITE = 20;
constexpr int8_t PLAY_BLACK = 21;
constexpr int8_t PLAY_RANDOM = 22;
}  // namespace GameSelectionMenuOptionId

/// Return the mode-coloured indicator used before resume confirmation.
LedRGB gameSelectionResumeIndicatorColor(BoardGameSelectionMode mode);

// ---------------------------------------------------------------------------
// GameSelectionMenu — predefined game-selection tree.
// ---------------------------------------------------------------------------

class GameSelectionMenu final : public BoardMenu {
 public:
  GameSelectionMenu();

  void begin(BoardMenuController& controller) override;
  void onSelect(int optionId, BoardMenuController& controller) override;
  void onBack(BoardMenuController& controller) override;
  void cancel(BoardMenuController& controller) override;

  const BoardGameSelection& selection() const { return selection_; }
  bool hasSelection() const { return selection_.hasSelection(); }

 private:
  enum class Stage : uint8_t {
    IDLE,
    GAME,
    DIFFICULTY,
    COLOR,
  };

  BoardGameSelection selection_;
  uint8_t pendingBotDifficulty_;
  Stage stage_;

  void reset();
  void showGame(BoardMenuController& controller);
  void showDifficulty(BoardMenuController& controller);
  void showColor(BoardMenuController& controller);
  void finish(BoardMenuController& controller, BoardGameSelection selection);
  char randomPlayerColor() const;
};

#endif  // BOARD_MENUS_GAME_SELECTION_H