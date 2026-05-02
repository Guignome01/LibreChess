#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

#include "led_colors.h"
#include "piece.h"

using namespace LibreChess;

namespace SystemUtils {

/// Map a player Color to an LED color.
LedRGB colorLed(Color color);

/// Initialize NVS for ESP32 (required before Preferences.begin).
bool ensureNvsInitialized();

} // namespace SystemUtils

#endif // SYSTEM_UTILS_H
