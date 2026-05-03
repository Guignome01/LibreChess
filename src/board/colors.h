#ifndef BOARD_COLORS_H
#define BOARD_COLORS_H

#include "piece.h"

#include <stdint.h>

struct LedRGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

namespace LedColors {
static constexpr LedRGB Cyan{0, 255, 255};
static constexpr LedRGB White{255, 255, 255};
static constexpr LedRGB Red{255, 0, 0};
static constexpr LedRGB Purple{128, 0, 255};
static constexpr LedRGB Green{0, 255, 0};
static constexpr LedRGB Lime{100, 200, 0};
static constexpr LedRGB Yellow{255, 200, 0};
static constexpr LedRGB Orange{255, 80, 0};
static constexpr LedRGB Crimson{200, 0, 50};
static constexpr LedRGB Blue{0, 0, 255};
static constexpr LedRGB Off{0, 0, 0};

/// Scale an RGB color by a factor and clamp each channel to 255.
inline constexpr LedRGB scaleColor(LedRGB color, float factor) {
  return {
      static_cast<uint8_t>(color.r * factor > 255 ? 255 : color.r * factor),
      static_cast<uint8_t>(color.g * factor > 255 ? 255 : color.g * factor),
      static_cast<uint8_t>(color.b * factor > 255 ? 255 : color.b * factor),
  };
}

/// Return the board feedback color associated with a chess side.
inline constexpr LedRGB forPieceColor(LibreChess::Color color) {
  return (color == LibreChess::Color::WHITE) ? White : Blue;
}

/// Return the game-end color encoded by persisted remote-game winner metadata.
inline constexpr LedRGB forWinner(char winnerColor) {
  return winnerColor == 'w' ? forPieceColor(LibreChess::Color::WHITE)
                            : (winnerColor == 'b' ? forPieceColor(LibreChess::Color::BLACK) : Cyan);
}
}  // namespace LedColors

#endif  // BOARD_COLORS_H