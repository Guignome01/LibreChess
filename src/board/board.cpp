#include "board/board.h"

#include "board/core/menu/menu.h"
#include "board/core/runtime.h"
#include "board/core/visual/animations.h"
#include "board/workflows/diagnostics.h"
#include "board/workflows/gameplay.h"
#include "shared/utils.h"

#include <Arduino.h>
#include <Preferences.h>

struct Board::Impl {
  BoardRuntime runtime;
  BoardAnimations animations;
  BoardMenuRunner menuRunner;
  BoardGameplay gameplay;
  BoardDiagnostics diagnostics;
  std::unique_ptr<BoardAssistanceProvider> assistanceProvider;

  Impl()
      : runtime(),
        animations(runtime.presentationScheduler(), runtime.presentationCanvas()),
        menuRunner(runtime, animations),
        gameplay(runtime, animations, menuRunner),
        diagnostics(runtime, animations),
        assistanceProvider(new BoardLegalMoveAssistanceProvider()) {
    gameplay.setAssistanceProvider(assistanceProvider.get());
  }
};

Board::Board() : impl_(std::make_unique<Impl>()) {}
Board::~Board() {
  if (impl_) impl_->runtime.shutdown();
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

BoardGameplay& Board::gameplay() { return impl_->gameplay; }
BoardDiagnostics& Board::diagnostics() { return impl_->diagnostics; }

void Board::showMenu(BoardMenu& menu, bool flipped) { impl_->menuRunner.show(menu, flipped); }
bool Board::pollMenu() { return impl_->menuRunner.poll(); }
bool Board::runMenuBlocking(BoardMenu& menu, bool flipped) {
  return impl_->menuRunner.runBlocking(menu, flipped);
}
void Board::clearMenu() { impl_->menuRunner.clear(); }
void Board::beginDiagnostics() { impl_->diagnostics.begin(); }
void Board::updateDiagnostics() { impl_->diagnostics.update(); }
bool Board::diagnosticsComplete() const { return impl_->diagnostics.isComplete(); }

void Board::setAssistanceProvider(std::unique_ptr<BoardAssistanceProvider> provider) {
  impl_->gameplay.setAssistanceProvider(nullptr);
  impl_->assistanceProvider = provider ? std::move(provider)
                                      : std::unique_ptr<BoardAssistanceProvider>(
                                            new BoardNoAssistanceProvider());
  impl_->gameplay.setAssistanceProvider(impl_->assistanceProvider.get());
}

void Board::triggerCalibration() {
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

BoardAnimationHandle Board::startConnectingStatus() {
  auto g = impl_->runtime.lockCanvas();
  return impl_->animations.startConnecting(millis());
}

void Board::stopConnectingStatus(BoardAnimationHandle& handle) {
  auto g = impl_->runtime.lockCanvas();
  impl_->animations.cancel(handle);
}
