#include "board/board.h"

#include "board/core/runtime.h"
#include "board/workflows/calibration.h"
#include "board/workflows/diagnostics.h"
#include "board/workflows/gameplay.h"
#include "board/workflows/menu.h"

#include <Arduino.h>

struct Board::Impl {
  BoardRuntime runtime;
  BoardGameplay gameplay;
  BoardDiagnostics diagnostics;
  BoardCalibration calibration;
  BoardMenu menu;

  Impl()
      : runtime(),
        gameplay(runtime),
        diagnostics(runtime),
        calibration(runtime),
        menu(runtime) {}
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
BoardMenu& Board::menu() { return impl_->menu; }
BoardDiagnostics& Board::diagnostics() { return impl_->diagnostics; }
BoardCalibration& Board::calibration() { return impl_->calibration; }

void Board::clearAllSurfaces() {
  auto g = impl_->runtime.lockCanvas();
  g.canvas.clearAll();
  g.animations.clearAll(g.canvas);
}

BoardAnimationHandle Board::startConnectingStatus() {
  auto g = impl_->runtime.lockCanvas();
  return g.animations.startConnecting(millis());
}

void Board::stopConnectingStatus(BoardAnimationHandle& handle) {
  auto g = impl_->runtime.lockCanvas();
  g.animations.cancel(handle);
}
