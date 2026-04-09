// ---------------------------------------------------------------------------
// LibreChess UCI — native command-line engine executable.
//
// Entry point for SPRT testing with cutechess-cli / fastchess.
// Reads UCI commands from stdin, writes responses to stdout.
//
// Build:  mingw32-make  (Windows)  or  make  (Linux/macOS)
// Usage:  ./librechess              (interactive UCI)
//         cutechess-cli -engine cmd=./librechess ...
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "uci.h"

/// Platform time function — milliseconds since an arbitrary epoch.
/// Mirrors millis() on ESP32 but uses std::chrono on desktop.
static uint32_t nativeMillis() {
  static const auto start = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
          .count());
}

int main() {
  // Disable stdio buffering for real-time UCI communication.
  std::setbuf(stdin, nullptr);
  std::setbuf(stdout, nullptr);

  LibreChess::uci::UCIState state(nativeMillis);
  LibreChess::uci::loop(state, stdin, stdout);
  return 0;
}
