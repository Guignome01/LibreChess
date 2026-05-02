#include "player_mode.h"
#include "../board/board.h"
#include "game.h"
#include <Arduino.h>

PlayerMode::PlayerMode(Board* board, WiFiManagerESP32* wm, Game* cg, ILogger* logger) : GameMode(board, wm, cg, logger) {}

void PlayerMode::begin() {
  logger_.info("=== Starting Chess Moves Mode ===");
  if (!tryResumeGame()) {
    GameMeta meta = { static_cast<uint8_t>(GameModeId::PLAYER), 0, 0 };
    chess_->startNewGame('?', metaBytes(meta));
  }
  waitForBoardSetup();
}

void PlayerMode::update() {
  board_->readSensors();

  if (processResign()) return;

  int fromRow, fromCol, toRow, toCol;
  if (tryPlayerMove(chess_->sideToMove(), fromRow, fromCol, toRow, toCol))
    applyMove(fromRow, fromCol, toRow, toCol);

  board_->syncOccupancyBaseline();
}
