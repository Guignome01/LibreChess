#include "stockfish_provider.h"
#include "engine/stockfish/stockfish_api.h"
#include "game_mode/game_mode.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <new>

StockfishProvider::StockfishProvider(int level, char playerColor, ILogger* logger)
    : EngineProvider(logger), playerColor_(playerColor) {
  // Clamp to valid range and resolve depth + timeout from the level table.
  level_ = (level < 1) ? DEFAULT_LEVEL : (level > LEVEL_COUNT) ? DEFAULT_LEVEL : level;
  int depth = LEVELS[level_ - 1].depth;
  int timeoutMs = 10000 + depth * 3500;  // Scale timeout with depth
  settings_ = StockfishSettings(depth, timeoutMs);
}

bool StockfishProvider::initialize(EngineInitResult& result) {
  logger_.infof("StockfishProvider: depth=%d, timeout=%dms, retries=%d",
                              settings_.depth, settings_.timeoutMs, settings_.maxRetries);
  result.playerColor = playerColor_;
  result.fen = "";  // Starting position
  result.mode = GameModeId::BOT;
  result.difficulty = static_cast<uint8_t>(level_);
  result.canResume = true;
  return true;
}

void StockfishProvider::requestMove(const std::string& fen) {
  auto* ctx = new (std::nothrow) TaskContext();
  if (!ctx) {
    logger_.error("StockfishProvider: failed to allocate task context");
    setImmediateResult(EngineResult{});
    return;
  }
  ctx->fen = fen;
  ctx->depth = settings_.depth;
  ctx->timeoutMs = settings_.timeoutMs;
  ctx->maxRetries = settings_.maxRetries;
  spawnTask(ctx, "sfTask", taskFunction);
}

bool StockfishProvider::checkResult(EngineResult& result) {
  if (!peekResult(result)) return false;
  if (result.type == EngineResult::MOVE)
    currentEvaluation_ = result.evaluation;
  finishTask();
  return true;
}

int StockfishProvider::getEvaluation() {
  return currentEvaluation_;
}

// ---------------------------------------------------------------
// FreeRTOS task — runs the blocking HTTP request off the game loop.
// ---------------------------------------------------------------

void StockfishProvider::taskFunction(void* param) {
  auto* ctx = static_cast<TaskContext*>(param);

  WiFiClientSecure client;
  client.setInsecure();

  String path = StockfishAPI::buildRequestURL(String(ctx->fen.c_str()), ctx->depth);
  ctx->logger.infof("Stockfish request: %s%s", STOCKFISH_API_URL, path.c_str());

  String response;
  bool gotResponse = false;

  for (int attempt = 1; attempt <= ctx->maxRetries; attempt++) {
    if (ctx->cancel.load()) break;

    if (attempt > 1) {
      ctx->logger.infof("Stockfish: attempt %d/%d", attempt, ctx->maxRetries);
      delay(500);
    }

    if (!client.connect(STOCKFISH_API_URL, STOCKFISH_API_PORT)) {
      ctx->logger.error("Stockfish: connection failed");
      continue;
    }

    client.println("GET " + path + " HTTP/1.1");
    client.println("Host: " STOCKFISH_API_URL);
    client.println("Connection: close");
    client.println();

    unsigned long startTime = millis();
    while (client.connected() && (millis() - startTime < (unsigned long)ctx->timeoutMs)) {
      if (ctx->cancel.load()) break;
      if (client.available()) {
        response = client.readString();
        gotResponse = true;
        break;
      }
      delay(10);
    }
    client.stop();

    if (gotResponse) break;
    ctx->logger.error("Stockfish: timeout or empty response");
  }

  if (ctx->cancel.load()) {
    ctx->ready.store(true);
    vTaskDelete(nullptr);
    return;
  }

  if (!gotResponse || response.length() == 0) {
    ctx->logger.error("Stockfish: all attempts failed");
    ctx->ready.store(true);
    vTaskDelete(nullptr);
    return;
  }

  // Parse the response
  StockfishResponse sfResp;
  if (StockfishAPI::parseResponse(response, sfResp)) {
    ctx->result.type = EngineResult::MOVE;
    ctx->result.move = std::string(sfResp.bestMove.c_str());

    if (sfResp.hasMate)
      ctx->result.evaluation = sfResp.mateInMoves > 0 ? 10000 : -10000;
    else
      ctx->result.evaluation = static_cast<int>(sfResp.evaluation * 100.0f);

    ctx->logger.infof("Stockfish: bestMove=%s, eval=%d cp",
                                         sfResp.bestMove.c_str(), ctx->result.evaluation);
  } else {
    ctx->logger.errorf("Stockfish: parse failed: %s", sfResp.errorMessage.c_str());
  }

  ctx->ready.store(true);
  vTaskDelete(nullptr);
}
