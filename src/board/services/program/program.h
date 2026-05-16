#ifndef BOARD_SERVICES_PROGRAM_PROGRAM_H
#define BOARD_SERVICES_PROGRAM_PROGRAM_H

#include <memory>

// ---------------------------------------------------------------------------
// BoardProgram — primary board behavior contract.
// ---------------------------------------------------------------------------
// A program owns the board's main behavior while it is active. Menus remain
// separate overlays and may run while a program is active. The first runner is
// intentionally single-program: one active primary behavior at a time.
// ---------------------------------------------------------------------------

class BoardProgram {
 public:
  virtual ~BoardProgram() = default;

  virtual void begin() = 0;
  virtual void update() = 0;
  virtual void cancel() {}
  virtual bool isComplete() const = 0;
};

// ---------------------------------------------------------------------------
// BoardProgramRunner — board-owned active program slot.
// ---------------------------------------------------------------------------

class BoardProgramRunner {
 public:
  BoardProgramRunner();

  BoardProgramRunner(const BoardProgramRunner&) = delete;
  BoardProgramRunner& operator=(const BoardProgramRunner&) = delete;

  /// Start a program, cancelling any currently active program first.
  void set(BoardProgram& program);

  /// Start and own a freshly-created program. Returns the active program pointer.
  BoardProgram* set(std::unique_ptr<BoardProgram> program);

  /// Poll the active program once. Returns true when it finishes this poll.
  bool poll();

  /// Cancel and detach the active program, if any.
  void stop();

  /// Return true when a primary board program is active.
  bool active() const { return activeProgram_ != nullptr; }

  /// Return the active primary program, or nullptr when idle.
  BoardProgram* activeProgram() const { return activeProgram_; }

 private:
  std::unique_ptr<BoardProgram> ownedProgram_;
  BoardProgram* activeProgram_;
};

#endif  // BOARD_SERVICES_PROGRAM_PROGRAM_H