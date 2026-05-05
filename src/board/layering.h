#ifndef BOARD_LAYERING_H
#define BOARD_LAYERING_H

#include "animations.h"
#include "colors.h"
#include "state.h"
#include "system.h"

#include <stdint.h>
#include <utility>

class BoardLayering;

/// Board-internal visual layer selected by BoardLayerWriter.
enum class BoardLayerTarget : uint8_t {
  BASE,
  OVERLAY,
};

/// LED-writer-compatible adapter for one logical board visual layer.
class BoardLayerWriter {
 public:
  /// Clear this writer's target layer and optionally render the composition.
  void clearAllLEDs(bool show = true);

  /// Set one square in this writer's target layer.
  void setSquareLED(int row, int col, LedRGB color);

  /// Render the composed board layers to the physical LEDs.
  void showLEDs();

 private:
  friend class BoardLayering;

  BoardLayering& layering_;
  BoardLayerTarget target_;
  bool rendered_;

  BoardLayerWriter(BoardLayering& layering, BoardLayerTarget target);
  bool rendered() const { return rendered_; }
};

/// Board-internal layering subsystem for persistent visuals plus overlays.
class BoardLayering {
 public:
  using LayerWriter = BoardLayerWriter;

  /// Bind layer rendering to the board service boundary.
  explicit BoardLayering(BoardSystem& system);

  BoardLayering(const BoardLayering&) = delete;
  BoardLayering& operator=(const BoardLayering&) = delete;

  /// Replace the persistent base layer and render the composed result.
  template <typename UpdateFn>
  void replaceBase(UpdateFn&& update) {
    clearBaseFrame();
    LayerWriter writer(*this, BoardLayerTarget::BASE);
    std::forward<UpdateFn>(update)(writer);
    renderIfNeeded(writer);
  }

  /// Update the persistent base layer and render the composed result.
  template <typename UpdateFn>
  void updateBase(UpdateFn&& update) {
    LayerWriter writer(*this, BoardLayerTarget::BASE);
    std::forward<UpdateFn>(update)(writer);
    renderIfNeeded(writer);
  }

  /// Replace the overlay layer and render it over the base layer.
  template <typename UpdateFn>
  void replaceOverlay(UpdateFn&& update) {
    clearOverlayFrame();
    LayerWriter writer(*this, BoardLayerTarget::OVERLAY);
    std::forward<UpdateFn>(update)(writer);
    renderIfNeeded(writer);
  }

  /// Update the overlay layer and render it over the base layer.
  template <typename UpdateFn>
  void updateOverlay(UpdateFn&& update) {
    LayerWriter writer(*this, BoardLayerTarget::OVERLAY);
    std::forward<UpdateFn>(update)(writer);
    renderIfNeeded(writer);
  }

  /// Clear the persistent base layer.
  void clearBase(bool show = true);

  /// Clear the overlay layer.
  void clearOverlay(bool show = true);

  /// Clear both layers.
  void clearAll(bool show = true);

  /// Clear one square on the persistent base layer.
  void clearBaseSquare(int row, int col, bool show = true);

  /// Clear one square on the overlay layer.
  void clearOverlaySquare(int row, int col, bool show = true);

  /// Render the current composed layer state to the physical LEDs.
  void render();

  /// Run a short animation and then restore the composed layer state.
  void runTemporaryAnimation(const AnimationJob& job);

 private:
  friend class BoardLayerWriter;

  BoardSystem& system_;
  LedRGB base_[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS];
  LedRGB overlay_[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS];
  bool overlayEnabled_[LibreChess::board::BOARD_ROWS][LibreChess::board::BOARD_COLS];

  static bool validSquare(int row, int col);
  void clearBaseFrame();
  void clearOverlayFrame();
  void setSquare(BoardLayerTarget target, int row, int col, LedRGB color);
  void clearTarget(BoardLayerTarget target);
  void renderIfNeeded(const LayerWriter& writer);
};

#endif  // BOARD_LAYERING_H
