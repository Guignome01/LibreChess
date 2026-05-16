#include "board/services/program/program.h"

#include <utility>

BoardProgramRunner::BoardProgramRunner() : ownedProgram_(), activeProgram_(nullptr) {}

void BoardProgramRunner::set(BoardProgram& program) {
  stop();
  ownedProgram_.reset();
  activeProgram_ = &program;
  activeProgram_->begin();
  if (activeProgram_->isComplete()) activeProgram_ = nullptr;
}

BoardProgram* BoardProgramRunner::set(std::unique_ptr<BoardProgram> program) {
  stop();
  if (!program) return nullptr;

  ownedProgram_ = std::move(program);
  activeProgram_ = ownedProgram_.get();
  activeProgram_->begin();
  if (activeProgram_->isComplete()) {
    ownedProgram_.reset();
    activeProgram_ = nullptr;
  }
  return activeProgram_;
}

bool BoardProgramRunner::poll() {
  if (!activeProgram_) return false;

  activeProgram_->update();
  if (!activeProgram_->isComplete()) return false;

  activeProgram_ = nullptr;
  ownedProgram_.reset();
  return true;
}

void BoardProgramRunner::stop() {
  if (!activeProgram_) return;
  activeProgram_->cancel();
  activeProgram_ = nullptr;
  ownedProgram_.reset();
}