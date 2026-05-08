#include "player_mode.h"
#include "board/workflows/gameplay.h"
#include "game.h"
#include <Arduino.h>

PlayerMode::PlayerMode(BoardGameplay* gameplay, WiFiManagerESP32* wm, Game* cg, ILogger* logger) : GameMode(gameplay, wm, cg, logger) {}

void PlayerMode::begin() {
  logger_.info("=== Starting Chess Moves Mode ===");
  if (!tryResumeGame()) {
    GameMeta meta = { static_cast<uint8_t>(GameModeId::PLAYER), 0, 0 };
    chess_->startNewGame('?', metaBytes(meta));
  }
  waitForBoardSetup();
}

void PlayerMode::update() {
  if (processResign()) return;

  int fromRow, fromCol, toRow, toCol;
  if (tryPlayerMove(chess_->sideToMove(), fromRow, fromCol, toRow, toCol))
    applyMove(fromRow, fromCol, toRow, toCol);
}
