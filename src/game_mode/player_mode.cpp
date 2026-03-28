#include "player_mode.h"
#include "game.h"
#include <Arduino.h>

PlayerMode::PlayerMode(BoardDriver* bd, WiFiManagerESP32* wm, Game* cg, ILogger* logger) : GameMode(bd, wm, cg, logger) {}

void PlayerMode::begin() {
  logger_.info("=== Starting Chess Moves Mode ===");
  if (!tryResumeGame()) {
    GameMeta meta = { static_cast<uint8_t>(GameModeId::PLAYER), 0, 0 };
    chess_->startNewGame('?', metaBytes(meta));
  }
  waitForBoardSetup();
}

void PlayerMode::update() {
  boardDriver_->readSensors();

  if (processResign()) return;

  int fromRow, fromCol, toRow, toCol;
  if (tryPlayerMove(chess_->currentTurn(), fromRow, fromCol, toRow, toCol))
    applyMove(fromRow, fromCol, toRow, toCol);

  boardDriver_->updateSensorPrev();
}
