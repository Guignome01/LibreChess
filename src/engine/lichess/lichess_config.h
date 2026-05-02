#ifndef LICHESS_CONFIG_H
#define LICHESS_CONFIG_H

#include <Arduino.h>

// Lichess game configuration (extracted from the former LichessMode).
struct LichessConfig {
  String apiToken;
};

#endif  // LICHESS_CONFIG_H
