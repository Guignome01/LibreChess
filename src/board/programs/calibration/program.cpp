#include "board/programs/calibration/program.h"

#include "shared/utils.h"

#include <Arduino.h>
#include <Preferences.h>

BoardCalibration::BoardCalibration() : complete_(false) {}

void BoardCalibration::update() {
  if (complete_) return;
  complete_ = true;

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
