#ifndef ENGINES_LICHESS_CONFIG_H
#define ENGINES_LICHESS_CONFIG_H

#include <Arduino.h>

// Lichess game configuration (extracted from the former LichessMode).
struct LichessConfig {
  String apiToken;
};

#endif  // ENGINES_LICHESS_CONFIG_H
