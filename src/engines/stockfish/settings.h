#ifndef ENGINES_STOCKFISH_SETTINGS_H
#define ENGINES_STOCKFISH_SETTINGS_H

// HTTP request configuration for the Stockfish online API.
// Depth and timeout are resolved from the provider's difficulty level table.
struct StockfishSettings {
  int depth;      // Search depth (Stockfish API valid range: 6–16)
  int timeoutMs;  // API timeout in milliseconds
  int maxRetries; // Max API call retries on failure

  StockfishSettings(int depth = 9, int timeoutMs = 60000, int maxRetries = 3)
      : depth(depth), timeoutMs(timeoutMs), maxRetries(maxRetries) {}
};

#endif  // ENGINES_STOCKFISH_SETTINGS_H
