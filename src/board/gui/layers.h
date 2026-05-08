#ifndef BOARD_LAYERS_H
#define BOARD_LAYERS_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Board canvas layer enumeration
// ---------------------------------------------------------------------------
// The 8x8 LED canvas is composed from a fixed stack of semantic layers.
// During composition, the top-most layer that has a pixel "present" at a
// given square wins. Layers exist so that independent subsystems (game
// state, assistance hints, feedback flashes, menus, animated effects,
// emergency overrides) can paint without stomping on each other.
//
// Order is bottom-up: BACKGROUND is drawn first, OVERRIDE last.
// ---------------------------------------------------------------------------

enum class BoardLayer : uint8_t {
  BACKGROUND = 0,  ///< Persistent baseline (rarely used; reserved for future).
  GAME = 1,        ///< Authoritative game-state visuals (occupied squares, etc.).
  ASSISTANCE = 2,  ///< Move hints, setup guidance, castling/remote prompts.
  FEEDBACK = 3,    ///< Move-result feedback (capture flashes, illegal blink, resign progress).
  MENU = 4,        ///< Menu items + back button.
  EFFECT = 5,      ///< Full-board animated effects (firework, marquee, connecting, thinking).
  OVERRIDE = 6,    ///< Last-resort top layer (errors, manual overrides).
};

/// Total number of board canvas layers.
static constexpr uint8_t BOARD_LAYER_COUNT = 7;

/// Convert a BoardLayer to its numeric index.
constexpr uint8_t layerIndex(BoardLayer layer) {
  return static_cast<uint8_t>(layer);
}

#endif  // BOARD_LAYERS_H
