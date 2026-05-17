#ifndef BOARD_H
#define BOARD_H

#include "board/services/menu/menu.h"
#include "board/programs/game/program_provider.h"
#include "board/services/program/program.h"
#include "board/services/visual/animations.h"
#include "board/assistance_provider.h"

#include <cstdint>
#include <memory>

// ---------------------------------------------------------------------------
// Board — public physical-board package root
// ---------------------------------------------------------------------------
// Owns one internal BoardRuntime plus board services, factories, and runners.
// External firmware accesses the board only through this class.
// ---------------------------------------------------------------------------

class Board {
 public:
  /// Completion events produced by one board service tick.
  struct UpdateResult {
    bool menuFinished = false;
    bool programFinished = false;
  };

  /// Move-only RAII token for a board-owned animation.
  /// Auto-cancels on destruction (acquires the canvas lock).
  using Animation = BoardAnimationToken;

  Board();
  ~Board();

  Board(const Board&) = delete;
  Board& operator=(const Board&) = delete;
  Board(Board&&) = delete;
  Board& operator=(Board&&) = delete;

  /// Initialize hardware. Must be called once before any other method.
  /// Returns false when a required runtime resource could not start.
  bool begin();

  // --- LED settings ---
  uint8_t getBrightness() const;
  uint8_t getDimMultiplier() const;
  void setBrightness(uint8_t value);
  void setDimMultiplier(uint8_t value);
  void saveLedSettings();

  /// Sensor poll cadence (ms). Main loop should `delay(cadenceMs())`.
  uint16_t cadenceMs() const;

  /// Poll board-managed overlays/programs once and return completion events.
  UpdateResult update();

  // --- Programs ---

  /// Start a polled board program by registered string id. Known ids are
  /// declared in `board/programs/ids.h` (e.g. `GAME`, `DIAGNOSTICS`,
  /// `CALIBRATION`). Returns the active program, or nullptr if the id is
  /// unknown or the program failed to start. Callers may `static_cast` the
  /// result to the program's known typed interface (e.g. `IBoardGame*` for
  /// `BoardProgramIds::GAME`).
  BoardProgram* startProgram(const char* programId);

  /// Cancel and detach the active polled board program.
  void stopProgram();

  /// Display a typed physical-board menu. Idempotently clears any active menu first.
  void showMenu(BoardMenu& menu, bool flipped = false);

  /// Run a typed physical-board menu until it finishes.
  bool runMenu(BoardMenu& menu, bool flipped = false);

  /// Cancel and erase the active menu surface.
  void stopMenu();

  /// Install the active board assistance provider. Passing nullptr disables it.
  void setAssistanceProvider(std::unique_ptr<BoardAssistanceProvider> provider);

  /// Clear every canvas surface. The renderer flushes when it next wakes.
  void clearAllSurfaces();

  /// Start a named board animation. The returned token stops it automatically.
  Animation startAnimation(const char* animationId);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#endif  // BOARD_H
