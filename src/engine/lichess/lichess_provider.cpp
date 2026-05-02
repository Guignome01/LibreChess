#include "lichess_provider.h"
#include "game_mode/game_mode.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <new>

// Map Lichess status string to GameResult enum.
static GameResult lichessStatusToResult(const String& status) {
  if (status == "mate") return GameResult::CHECKMATE;
  if (status == "resign") return GameResult::RESIGNATION;
  if (status == "stalemate") return GameResult::STALEMATE;
  if (status == "draw") return GameResult::DRAW_AGREEMENT;
  if (status == "timeout" || status == "outoftime") return GameResult::TIMEOUT;
  if (status == "aborted") return GameResult::ABORTED;
  return GameResult::ABORTED;
}

// Map Lichess winner string to color char.
static char lichessWinnerToColor(const String& winner) {
  if (winner == "white") return 'w';
  if (winner == "black") return 'b';
  return 'd';
}

LichessProvider::LichessProvider(const LichessConfig& config, ILogger* logger)
    : EngineProvider(logger), config_(config), api_(config_, logger) {}

// ---------------------------------------------------------------
// initialize() — blocking game discovery (BotMode wraps with animation)
// ---------------------------------------------------------------

bool LichessProvider::initialize(EngineInitResult& result) {
  if (config_.apiToken.length() == 0) {
    logger_.error("LichessProvider: no API token configured");
    return false;
  }

  String username;
  if (!api_.verifyToken(username)) {
    logger_.error("LichessProvider: invalid API token");
    return false;
  }
  logger_.infof("Lichess: logged in as %s", username.c_str());
  logger_.info("Waiting for a Lichess game...");

  // Poll for a game start event (timeout after 3 minutes)
  LichessEvent event;
  event.type = LichessEventType::UNKNOWN;
  unsigned long initDeadline = millis() + 180000;
  while (event.type != LichessEventType::GAME_START) {
    if (api_.pollForGameEvent(event) && event.type == LichessEventType::GAME_START)
      break;
    if (millis() > initDeadline) {
      logger_.error("LichessProvider: timed out waiting for game");
      return false;
    }
    delay(2000);
  }

  currentGameId_ = event.gameId;
  playerColor_ = event.myColor;

  logger_.infof("Game found! ID: %s", currentGameId_.c_str());
  logger_.infof("Playing as: %s", playerColor_ == 'w' ? "White" : "Black");

  // Get full game state
  LichessGameState state;
  state.myColor = playerColor_;
  state.gameId = currentGameId_;
  state.fen = event.fen;

  if (!api_.pollGameStream(currentGameId_, state)) {
    logger_.info("Warning: could not get full game state, using initial event data");
    state.gameStarted = true;
    state.gameEnded = false;
    state.lastMove = "";
  }

  // Populate init result
  result.playerColor = playerColor_;
  result.mode = GameModeId::LICHESS;
  result.difficulty = 0;
  result.canResume = false;

  if (state.fen.length() > 0 && state.fen != "startpos")
    result.fen = std::string(state.fen.c_str());
  else
    result.fen = "";

  lastKnownMoveCount_ = state.moveCount;
  return true;
}

// ---------------------------------------------------------------
// Async opponent-move polling via FreeRTOS task
// ---------------------------------------------------------------

void LichessProvider::requestMove(const std::string& /*fen*/) {
  auto* ctx = new (std::nothrow) TaskContext();
  if (!ctx) {
    logger_.error("LichessProvider: failed to allocate task context");
    setImmediateResult(EngineResult{});
    return;
  }
  ctx->config = config_;
  ctx->gameId = currentGameId_;
  ctx->playerColor = playerColor_;
  ctx->lastKnownMoveCount = lastKnownMoveCount_;
  ctx->lastSentMove = lastSentMove_;
  spawnTask(ctx, "liTask", taskFunction);
}

bool LichessProvider::checkResult(EngineResult& result) {
  if (!peekResult(result)) return false;
  auto* ctx = static_cast<TaskContext*>(activeTask_);
  if (ctx) lastKnownMoveCount_ = ctx->lastKnownMoveCount;
  finishTask();
  return true;
}

// ---------------------------------------------------------------
// Player move → send to Lichess server (synchronous, small request)
// ---------------------------------------------------------------

bool LichessProvider::onPlayerMoveApplied(const std::string& moveCoord) {
  String coord = String(moveCoord.c_str());
  logger_.infof("Sending move to Lichess: %s", coord.c_str());
  lastSentMove_ = coord;

  const int maxRetries = 3;
  for (int attempt = 0; attempt < maxRetries; attempt++) {
    if (api_.makeMove(currentGameId_, coord)) return true;
    logger_.errorf("LichessProvider: send failed, attempt %d/%d", attempt + 1, maxRetries);
    delay(500);
  }

  logger_.error("LichessProvider: all send attempts failed");
  lastSentMove_ = "";
  return false;
}

void LichessProvider::onResignConfirmed() {
  logger_.info("Sending resign to Lichess...");
  api_.resignGame(currentGameId_);
}

// ---------------------------------------------------------------
// FreeRTOS task — persistent NDJSON stream for opponent moves.
// Opens one TLS connection and reads events as they arrive.
// Reconnects with exponential backoff on connection loss.
// ---------------------------------------------------------------

void LichessProvider::taskFunction(void* param) {
  auto* ctx = static_cast<TaskContext*>(param);
  Log log = ctx->logger;

  // Thread-local API instance — safe for FreeRTOS task (config copied by value)
  LichessAPI api(ctx->config, log);

  static constexpr int MAX_RECONNECT_ATTEMPTS = 5;
  static constexpr unsigned long INITIAL_BACKOFF_MS = 1000;
  static constexpr unsigned long MAX_BACKOFF_MS = 8000;
  // Timeout for reading stream events: generous enough to accommodate
  // Lichess heartbeat interval (~15-20s) with margin.
  static constexpr unsigned long STREAM_READ_TIMEOUT_MS = 30000;

  int reconnectAttempts = 0;
  unsigned long backoffMs = INITIAL_BACKOFF_MS;

  while (!ctx->cancel.load()) {
    WiFiClientSecure client;

    if (!api.connectGameStream(client, ctx->gameId)) {
      reconnectAttempts++;
      if (reconnectAttempts > MAX_RECONNECT_ATTEMPTS) {
        log.error("Lichess stream: all reconnect attempts exhausted, aborting");
        ctx->result.type = EngineResult::GAME_ENDED;
        ctx->result.gameResult = GameResult::ABORTED;
        ctx->result.winnerColor = ' ';
        ctx->ready.store(true);
        vTaskDelete(nullptr);
        return;
      }
      log.infof("Lichess stream: reconnect attempt %d/%d in %lums",
                           reconnectAttempts, MAX_RECONNECT_ATTEMPTS, backoffMs);
      delay(backoffMs);
      backoffMs = min(backoffMs * 2, MAX_BACKOFF_MS);
      continue;
    }

    // Connected — reset backoff
    reconnectAttempts = 0;
    backoffMs = INITIAL_BACKOFF_MS;

    // Read events from the persistent stream
    while (!ctx->cancel.load()) {
      LichessGameState state;
      state.myColor = ctx->playerColor;
      state.gameId = ctx->gameId;

      if (!api.readStreamEvent(client, state, ctx->cancel, STREAM_READ_TIMEOUT_MS)) {
        // Connection dropped or timed out — break to reconnect loop
        log.info("Lichess stream: connection lost, will reconnect...");
        client.stop();
        break;
      }

      // New move detected?
      if (state.moveCount > ctx->lastKnownMoveCount) {
        // Skip echo of our own move
        if (state.lastMove == ctx->lastSentMove) {
          ctx->lastKnownMoveCount = state.moveCount;
          continue;
        }

        log.infof("Lichess move received: %s", state.lastMove.c_str());
        ctx->lastKnownMoveCount = state.moveCount;
        ctx->result.type = EngineResult::MOVE;
        ctx->result.move = std::string(state.lastMove.c_str());
        client.stop();
        ctx->ready.store(true);
        vTaskDelete(nullptr);
        return;
      }

      // External game-end?
      if (state.gameEnded) {
        log.infof("Lichess game ended: %s", state.status.c_str());
        ctx->result.type = EngineResult::GAME_ENDED;
        ctx->result.gameResult = lichessStatusToResult(state.status);
        ctx->result.winnerColor = lichessWinnerToColor(state.winner);
        client.stop();
        ctx->ready.store(true);
        vTaskDelete(nullptr);
        return;
      }
    }
  }

  // Cancelled
  ctx->ready.store(true);
  vTaskDelete(nullptr);
}
