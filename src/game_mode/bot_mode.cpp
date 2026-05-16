#include "bot_mode.h"
#include "board/programs/game/game_program.h"
#include "game.h"
#include "wifi_manager_esp32.h"

#include <Arduino.h>

BotMode::BotMode(BoardGameProgram* gameplay, WiFiManagerESP32* wm, Game* cg,
                 EngineProvider* provider, ILogger* logger)
  : GameMode(gameplay, wm, cg, logger), provider_(provider) {}

BotMode::~BotMode() {
  delete provider_;
}

// ---------------------------------------------------------------
// begin() — common initialization skeleton for all engines
// ---------------------------------------------------------------

void BotMode::begin() {
  logger_.info("=== Starting Bot Mode ===");

  if (!wifiManager_->isWiFiConnected()) {
    abortWithError("No WiFi connection");
    return;
  }

  // Provider may block (e.g., Lichess game discovery). Show waiting animation.
  BoardAnimationToken waitAnim = gameplay_->startWaitingStatus();

  EngineInitResult initResult;
  bool ok = provider_->initialize(initResult);

  waitAnim.stop();

  if (!ok) {
    abortWithError("Engine initialization failed");
    return;
  }

  playerColor_ = initResult.playerColor;
  logger_.infof("Player: %s, Engine: %s",
                              Game::colorName(playerColor()),
                              Game::colorName(~playerColor()));

  if (initResult.canResume && tryResumeGame()) {
    // Resumed from flash — skip new game
  } else {
    chess_->startNewGame(initResult.playerColor,
                          metaBytes(GameMeta{ static_cast<uint8_t>(initResult.mode),
                                              initResult.engineId,
                                              initResult.difficulty }));
    if (!initResult.fen.empty())
      setBoardStateFromFEN(initResult.fen);
  }

  waitForBoardSetup();

  // If it's the engine's turn after setup, start requesting immediately
  if (chess_->sideToMove() != playerColor()) {
    startThinking();
    provider_->requestMove(chess_->getFen());
    botState_ = BotState::ENGINE_THINKING;
  }

  logger_.info("=== Game Ready ===");
}

// ---------------------------------------------------------------
// update() — non-blocking state machine
// ---------------------------------------------------------------

bool BotMode::isNavigationAllowed() const {
  return chess_->isGameOver() || botState_ == BotState::PLAYER_TURN;
}

void BotMode::update() {
  if (chess_->isGameOver()) return;

  if (processResign()) return;

  if (botState_ == BotState::PLAYER_TURN) {
    serviceAssistance();

    int fromRow, fromCol, toRow, toCol;
    if (tryPlayerMove(playerColor(), fromRow, fromCol, toRow, toCol)) {
      MoveResult result = applyMove(fromRow, fromCol, toRow, toCol);

      // Notify provider (Lichess sends the move to the server)
      std::string coord = Game::toCoordinate(fromRow, fromCol, toRow, toCol, Game::pieceToChar(result.promotedTo));
      if (!provider_->onPlayerMoveApplied(coord)) {
        abortWithError("Failed to send move to server");
        return;
      }

      // If the game didn't end and it's now the engine's turn, start engine
      if (!chess_->isGameOver() && chess_->sideToMove() != playerColor()) {
        engineRetryCount_ = 0;
        startThinking();
        provider_->requestMove(chess_->getFen());
        botState_ = BotState::ENGINE_THINKING;
      }
    }
  } else if (botState_ == BotState::ENGINE_THINKING) {
    EngineResult result;
    if (provider_->checkResult(result)) {
      stopThinking();
      switch (result.type) {
        case EngineResult::MOVE:
          if (!applyEngineMove(result.move)) {
            abortWithError("Engine returned invalid move");
            return;
          }
          break;
        case EngineResult::GAME_ENDED:
          handleRemoteGameEnd(result);
          break;
        case EngineResult::NONE:
          if (++engineRetryCount_ > 3) {
            abortWithError("Engine failed to respond");
            return;
          }
          logger_.infof("BotMode: engine returned no result, retry %d/3", engineRetryCount_);
          startThinking();
          provider_->requestMove(chess_->getFen());
          return;  // Stay in ENGINE_THINKING
      }
      botState_ = BotState::PLAYER_TURN;
    }
  }
}

// ---------------------------------------------------------------
// Engine move application
// ---------------------------------------------------------------

bool BotMode::applyEngineMove(const std::string& move) {
  int fromRow, fromCol, toRow, toCol;
  char promotion;
  if (!Game::parseCoordinate(move, fromRow, fromCol, toRow, toCol, promotion)) {
    logger_.errorf("BotMode: failed to parse engine move: %s", move.c_str());
    return false;
  }

  // Verify the move is from the correct color piece
  Piece piece = chess_->getSquare(fromRow, fromCol);
  Color engineColor = ~playerColor();

  if (Game::isEmptySquare(piece) || Game::pieceColor(piece) != engineColor) {
    logger_.errorf("BotMode: engine move from invalid square (%d,%d) piece='%c'",
                                 fromRow, fromCol, Game::pieceToChar(piece));
    return false;
  }

  logger_.infof("Engine move: %s (%d,%d)->(%d,%d)", move.c_str(), fromRow, fromCol, toRow, toCol);
  applyMove(fromRow, fromCol, toRow, toCol, promotion, true);
  return true;
}

void BotMode::handleRemoteGameEnd(const EngineResult& result) {
  gameplay_->showRemoteGameEnd(result.winnerColor);
  chess_->endGame(result.gameResult, result.winnerColor);
}

void BotMode::abortWithError(const char* message) {
  logger_.errorf("BotMode ABORT: %s", message);
  gameplay_->showErrorFeedback();
  chess_->endGame(GameResult::ABORTED, ' ');
}

// ---------------------------------------------------------------
// Resign hooks
// ---------------------------------------------------------------

void BotMode::onBeforeResignConfirm() {
  wasThinkingBeforeResign_ = thinkingAnimation_.active();
  if (wasThinkingBeforeResign_) {
    provider_->cancelRequest();
    stopThinking();
  }
}

void BotMode::onResignCancelled() {
  if (wasThinkingBeforeResign_ && chess_->sideToMove() != playerColor() && !chess_->isGameOver()) {
    startThinking();
    provider_->requestMove(chess_->getFen());
    botState_ = BotState::ENGINE_THINKING;
  }
}

void BotMode::onResignConfirmed(Color resignColor) {
  provider_->onResignConfirmed();
}

// ---------------------------------------------------------------
// Thinking animation helpers
// ---------------------------------------------------------------

void BotMode::startThinking() {
  thinkingAnimation_ = gameplay_->startThinkingStatus();
}

void BotMode::stopThinking() {
  thinkingAnimation_.stop();
}
