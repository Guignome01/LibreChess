#include "board/board.h"

#include "board/programs/factory.h"
#include "board/programs/ids.h"
#include "board/runtime/runtime.h"
#include "board/services/menu/menu.h"
#include "board/services/program/factory.h"
#include "board/services/program/program.h"
#include "board/services/visual/animations.h"
#include "shared/utils.h"

#include <Arduino.h>
#include <string.h>
#include <utility>

struct Board::Impl {
  BoardRuntime runtime;
  BoardAnimations animations;
  BoardMenuRunner menuRunner;
  BoardProgramFactory programFactory;
  BoardProgramRunner programRunner;
  std::unique_ptr<BoardAssistanceProvider> assistanceProvider;
  // Observer pointer to the active game program, owned by `programRunner`.
  // Tracked separately so `setAssistanceProvider()` can re-bind the live
  // provider without scanning the runner. Cleared on `stopProgram()` and on
  // every `startProgram()` that does not start the game program.
  IBoardGame* activeGameProgram = nullptr;

  Impl()
      : runtime(),
        animations(runtime.presentationScheduler(), runtime.presentationCanvas()),
        menuRunner(runtime, animations),
        programFactory(),
        programRunner(),
        assistanceProvider(new BoardLegalMoveAssistanceProvider()) {
    registerBoardPrograms(programFactory);
  }

  BoardProgramContext programContext() {
    return BoardProgramContext{&runtime, &animations, &menuRunner, assistanceProvider.get()};
  }
};

Board::Board() : impl_(std::make_unique<Impl>()) {}
Board::~Board() {
  if (impl_) {
    impl_->programRunner.stop();
    impl_->runtime.shutdown();
  }
}

bool Board::begin() {
  const bool ok = impl_->runtime.begin();
  if (!ok) {
    Serial.println("Board runtime initialization failed");
  }
  return ok;
}

uint8_t Board::getBrightness() const { return impl_->runtime.getBrightness(); }
uint8_t Board::getDimMultiplier() const { return impl_->runtime.getDimMultiplier(); }
void Board::setBrightness(uint8_t value) { impl_->runtime.setBrightness(value); }
void Board::setDimMultiplier(uint8_t value) { impl_->runtime.setDimMultiplier(value); }
void Board::saveLedSettings() { impl_->runtime.saveLedSettings(); }
uint16_t Board::cadenceMs() const { return impl_->runtime.cadenceMs(); }

Board::UpdateResult Board::update() {
  UpdateResult result;
  result.menuFinished = impl_->menuRunner.poll();
  result.programFinished = impl_->programRunner.poll();
  return result;
}

void Board::showMenu(BoardMenu& menu, bool flipped) { impl_->menuRunner.show(menu, flipped); }
bool Board::runMenu(BoardMenu& menu, bool flipped) {
  return impl_->menuRunner.run(menu, flipped);
}
void Board::stopMenu() { impl_->menuRunner.stop(); }

BoardProgram* Board::startProgram(const char* programId) {
  BoardProgramContext context = impl_->programContext();
  std::unique_ptr<BoardProgram> program = impl_->programFactory.create(programId, context);
  if (!program) {
    Serial.printf("Board: unknown program id '%s'\n", programId ? programId : "(null)");
    impl_->activeGameProgram = nullptr;
    return nullptr;
  }
  BoardProgram* active = impl_->programRunner.set(std::move(program));
  // Track the game program (if any) for live assistance-provider re-binding.
  if (active != nullptr && programId != nullptr &&
      strcmp(programId, BoardProgramIds::GAME) == 0) {
    impl_->activeGameProgram = static_cast<IBoardGame*>(active);
  } else {
    impl_->activeGameProgram = nullptr;
  }
  return active;
}

void Board::stopProgram() {
  impl_->programRunner.stop();
  impl_->activeGameProgram = nullptr;
}

void Board::setAssistanceProvider(std::unique_ptr<BoardAssistanceProvider> provider) {
  if (impl_->activeGameProgram) impl_->activeGameProgram->setAssistanceProvider(nullptr);
  impl_->assistanceProvider = provider ? std::move(provider)
                                      : std::unique_ptr<BoardAssistanceProvider>(
                                            new BoardNoAssistanceProvider());
  if (impl_->activeGameProgram) {
    impl_->activeGameProgram->setAssistanceProvider(impl_->assistanceProvider.get());
  }
}

void Board::clearAllSurfaces() {
  auto g = impl_->runtime.lockCanvas();
  g.canvas.clearAll();
  impl_->animations.clearAll();
}

bool Board::hasActiveAnimations() {
  auto g = impl_->runtime.lockCanvas();
  return impl_->animations.any();
}

Board::Animation Board::startAnimation(const char* animationId) {
  if (animationId == nullptr) return Animation();

  auto g = impl_->runtime.lockCanvas();
  if (strcmp(animationId, "connecting") == 0) {
    return Animation(&impl_->runtime, &impl_->animations,
                     impl_->animations.startConnecting(millis()));
  }

  Serial.printf("Board: unknown animation id '%s'\n", animationId);
  return Animation();
}
