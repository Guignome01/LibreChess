#include "board/board.h"

#include "board/programs/factory.h"
#include "board/programs/game/gameplay.h"
#include "board/programs/ids.h"
#include "board/runtime/runtime.h"
#include "board/services/menu/menu.h"
#include "board/services/program/factory.h"
#include "board/services/program/program.h"
#include "board/services/visual/animations.h"
#include "shared/utils.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include <utility>

struct Board::Impl {
  BoardRuntime runtime;
  BoardAnimations animations;
  BoardMenuRunner menuRunner;
  BoardProgramFactory programFactory;
  BoardProgramRunner programRunner;
  std::unique_ptr<BoardGameProgram> gameProgram;
  std::unique_ptr<BoardAssistanceProvider> assistanceProvider;

  Impl()
      : runtime(),
        animations(runtime.presentationScheduler(), runtime.presentationCanvas()),
        menuRunner(runtime, animations),
        programFactory(),
        programRunner(),
        gameProgram(new BoardGameplay(runtime, animations, menuRunner)),
        assistanceProvider(new BoardLegalMoveAssistanceProvider()) {
    registerBoardPrograms(programFactory);
    gameProgram->setAssistanceProvider(assistanceProvider.get());
  }

  BoardProgramContext programContext() {
    return BoardProgramContext{&runtime, &animations, &menuRunner};
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

BoardGameProgram* Board::startGame() {
  // The game program is permanent; restart only resets transient state.
  impl_->programRunner.stop();
  if (!impl_->gameProgram) return nullptr;
  impl_->gameProgram->reset();
  return impl_->gameProgram.get();
}

void Board::stopGame() {
  if (impl_->gameProgram) impl_->gameProgram->reset();
}

BoardProgram* Board::startProgram(const char* programId) {
  BoardProgramContext context = impl_->programContext();
  std::unique_ptr<BoardProgram> program = impl_->programFactory.create(programId, context);
  if (!program) {
    Serial.printf("Board: unknown program id '%s'\n", programId ? programId : "(null)");
    return nullptr;
  }
  return impl_->programRunner.set(std::move(program));
}

void Board::stopProgram() { impl_->programRunner.stop(); }

void Board::setAssistanceProvider(std::unique_ptr<BoardAssistanceProvider> provider) {
  if (impl_->gameProgram) impl_->gameProgram->setAssistanceProvider(nullptr);
  impl_->assistanceProvider = provider ? std::move(provider)
                                      : std::unique_ptr<BoardAssistanceProvider>(
                                            new BoardNoAssistanceProvider());
  if (impl_->gameProgram) {
    impl_->gameProgram->setAssistanceProvider(impl_->assistanceProvider.get());
  }
}

void Board::resetCalibration() {
  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - cannot trigger calibration");
    return;
  }
  Preferences prefs;
  if (prefs.begin("boardCal", false)) {
    prefs.clear();
    prefs.end();
  } else {
    Serial.println("Board calibration namespace could not be opened for reset");
  }
  Serial.println("Board calibration cleared - rebooting ...");
  ESP.restart();
}

void Board::clearAllSurfaces() {
  auto g = impl_->runtime.lockCanvas();
  g.canvas.clearAll();
  impl_->animations.clearAll();
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
