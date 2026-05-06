#include "status.h"

#include "board.h"
#include "gui/animations.h"
#include "services.h"

BoardStatus::BoardStatus(Board& board) : services_(board.services()) {}

void BoardStatus::clearAllLEDs(bool show) {
  services_.layering().clearAll(show);
}

void BoardStatus::showConnectingAnimation() {
  services_.layering().clearAll(false);
  services_.runAnimationNow(AnimationJob::connecting());
}
