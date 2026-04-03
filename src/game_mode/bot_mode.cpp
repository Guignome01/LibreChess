#include "bot_mode.h"
#include "game.h"

#include "led_colors.h"
#include "system_utils.h"
#include "wifi_manager_esp32.h"
#include <Arduino.h>

BotMode::BotMode(BoardDriver* bd, WiFiManagerESP32* wm, Game* cg, EngineProvider* provider,
                 ILogger* logger)
    : GameMode(bd, wm, cg, logger), provider_(provider) {}

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
  std::atomic<bool>* waitAnim = boardDriver_->startWaitingAnimation();

  EngineInitResult initResult;
  bool ok = provider_->initialize(initResult);

  boardDriver_->stopAndWaitForAnimation(waitAnim);

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
  if (chess_->currentTurn() != playerColor()) {
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

  boardDriver_->readSensors();

  if (processResign()) return;

  if (botState_ == BotState::PLAYER_TURN) {
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
      if (!chess_->isGameOver() && chess_->currentTurn() != playerColor()) {
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
          applyEngineMove(result.move);
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

  boardDriver_->updateSensorPrev();
}

// ---------------------------------------------------------------
// Engine move application
// ---------------------------------------------------------------

void BotMode::applyEngineMove(const std::string& move) {
  int fromRow, fromCol, toRow, toCol;
  char promotion;
  if (!Game::parseCoordinate(move, fromRow, fromCol, toRow, toCol, promotion)) {
    logger_.errorf("BotMode: failed to parse engine move: %s", move.c_str());
    return;
  }

  // Verify the move is from the correct color piece
  Piece piece = chess_->getSquare(fromRow, fromCol);
  Color engineColor = ~playerColor();

  if (Game::isEmptySquare(piece) || Game::pieceColor(piece) != engineColor) {
    logger_.errorf("BotMode: engine move from invalid square (%d,%d) piece='%c'",
                                 fromRow, fromCol, Game::pieceToChar(piece));
    return;
  }

  logger_.infof("Engine move: %s (%d,%d)->(%d,%d)", move.c_str(), fromRow, fromCol, toRow, toCol);
  applyMove(fromRow, fromCol, toRow, toCol, promotion, true);
}

void BotMode::handleRemoteGameEnd(const EngineResult& result) {
  LedRGB color = (result.winnerColor == 'd')
                     ? LedColors::Cyan
                     : SystemUtils::colorLed(result.winnerColor == 'w' ? Color::WHITE : Color::BLACK);
  boardDriver_->fireworkAnimation(color);
  chess_->endGame(result.gameResult, result.winnerColor);
}

void BotMode::abortWithError(const char* message) {
  logger_.errorf("BotMode ABORT: %s", message);
  boardDriver_->flashBoardAnimation(LedColors::Red);
  chess_->endGame(GameResult::ABORTED, ' ');
}

// ---------------------------------------------------------------
// Resign hooks
// ---------------------------------------------------------------

void BotMode::onBeforeResignConfirm() {
  wasThinkingBeforeResign_ = (thinkingAnimation_ != nullptr);
  if (wasThinkingBeforeResign_) {
    provider_->cancelRequest();
    stopThinking();
  }
}

void BotMode::onResignCancelled() {
  if (wasThinkingBeforeResign_ && chess_->currentTurn() != playerColor() && !chess_->isGameOver()) {
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
  boardDriver_->waitForAnimationQueueDrain();
  thinkingAnimation_ = boardDriver_->startThinkingAnimation();
}

void BotMode::stopThinking() {
  if (thinkingAnimation_) {
    boardDriver_->stopAndWaitForAnimation(thinkingAnimation_);
    thinkingAnimation_ = nullptr;
  }
}

// ---------------------------------------------------------------
// Remote move guidance (LED + sensor blocking)
// Guides the player to physically execute the engine's move.
// ---------------------------------------------------------------

void BotMode::waitForRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol, bool isCapture, bool isEnPassant, int enPassantCapturedPawnRow) {
  BoardDriver::LedGuard guard(boardDriver_);
  boardDriver_->clearAllLEDs(false);
  // Show source square (where to pick up from)
  boardDriver_->setSquareLED(fromRow, fromCol, LedColors::Cyan);
  // Show destination square (where to place)
  if (isCapture)
    boardDriver_->setSquareLED(toRow, toCol, LedColors::Red);
  else
    boardDriver_->setSquareLED(toRow, toCol, LedColors::White);
  if (isEnPassant)
    boardDriver_->setSquareLED(enPassantCapturedPawnRow, toCol, LedColors::Purple);
  boardDriver_->showLEDs();

  bool piecePickedUp = false;
  bool capturedPieceRemoved = false;
  bool moveCompleted = false;

  logger_.info("Waiting for you to complete the remote move...");

  while (!moveCompleted) {
    boardDriver_->readSensors();

    // For capture moves, ensure captured piece is removed first
    // For en passant, check the actual captured pawn square (not the destination)
    if (isCapture && !capturedPieceRemoved) {
      int captureCheckRow = isEnPassant ? enPassantCapturedPawnRow : toRow;
      if (!boardDriver_->getSensorState(captureCheckRow, toCol)) {
        capturedPieceRemoved = true;
        if (isEnPassant) {
          logger_.info("En passant captured pawn removed, now complete the move...");
        } else {
          logger_.info("Captured piece removed, now complete the move...");
        }
      }
    }

    // Check if piece was picked up from source
    if (!piecePickedUp && !boardDriver_->getSensorState(fromRow, fromCol)) {
      piecePickedUp = true;
      logger_.info("Piece picked up, now place it on the destination...");
    }

    // Check if piece was placed on destination
    // For captures: wait until captured piece is removed AND piece is placed
    // For normal moves: just wait for piece to be placed
    if (piecePickedUp && boardDriver_->getSensorState(toRow, toCol))
      if (!isCapture || (isCapture && capturedPieceRemoved)) {
        moveCompleted = true;
        logger_.info("Move completed on physical board!");
      }

    delay(SENSOR_READ_DELAY_MS);
    boardDriver_->updateSensorPrev();
  }

  boardDriver_->clearAllLEDs();
}  // LedGuard released
