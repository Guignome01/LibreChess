#ifndef GAME_PLAYER_MODE_H
#define GAME_PLAYER_MODE_H

#include "game_mode.h"

// Human vs human local game mode
class PlayerMode : public GameMode {
 public:
  PlayerMode(BoardDriver* bd, WiFiManagerESP32* wm, Game* cg, ILogger* logger = nullptr);
  void begin() override;
  void update() override;
};

#endif // GAME_PLAYER_MODE_H
