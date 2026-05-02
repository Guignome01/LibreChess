#include "lichess_api.h"
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <atomic>

String LichessAPI::makeHttpRequest(const String& method, const String& path, const String& body) {
  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(LICHESS_API_HOST, LICHESS_API_PORT)) {
    logger_.error("Lichess API: Connection failed");
    return "";
  }

  // Build HTTP request
  String request = method + " " + path + " HTTP/1.1\r\n";
  request += "Host: " LICHESS_API_HOST "\r\n";
  request += "Authorization: Bearer " + config_.apiToken + "\r\n";
  request += "Accept: application/json\r\n";

  if (body.length() > 0) {
    request += "Content-Type: application/x-www-form-urlencoded\r\n";
    request += "Content-Length: " + String(body.length()) + "\r\n";
  }

  request += "Connection: close\r\n\r\n";

  if (body.length() > 0) {
    request += body;
  }

  client.print(request);

  // Wait for response
  unsigned long timeout = millis() + 10000;
  while (client.connected() && !client.available()) {
    if (millis() > timeout) {
      logger_.error("Lichess API: Request timeout");
      client.stop();
      return "";
    }
    delay(10);
  }

  // Read response
  String response = "";
  bool headersDone = false;
  int httpStatus = 0;

  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (!headersDone) {
      // Extract HTTP status code from first header line
      if (httpStatus == 0 && line.startsWith("HTTP/")) {
        int spaceIdx = line.indexOf(' ');
        if (spaceIdx != -1)
          httpStatus = line.substring(spaceIdx + 1).toInt();
      }
      if (line == "\r" || line.length() == 0) {
        headersDone = true;
      }
    } else {
      response += line + "\n";
    }
  }

  client.stop();

  if (httpStatus >= 400) {
    logger_.errorf("Lichess API: HTTP %d on %s", httpStatus, path.c_str());
    return "";
  }

  return response;
}

bool LichessAPI::verifyToken(String& username) {
  String response = makeHttpRequest("GET", "/api/account");
  if (response.length() == 0) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    logger_.error("Lichess API: JSON parse error in verifyToken");
    return false;
  }

  if (!doc["username"].isNull()) {
    username = doc["username"].as<String>();
    logger_.infof("Lichess API: Verified token for user: %s", username.c_str());
    return true;
  }

  return false;
}

bool LichessAPI::pollForGameEvent(LichessEvent& event) {
  String response = makeHttpRequest("GET", "/api/account/playing");
  if (response.length() == 0) return false;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) return false;

  JsonArray games = doc["nowPlaying"].as<JsonArray>();
  if (games.size() > 0) {
    // Get the first active game
    JsonObject game = games[0];
    event.type = LichessEventType::GAME_START;
    event.gameId = game["gameId"].as<String>();
    event.fen = game["fen"].as<String>();

    String color = game["color"].as<String>();
    event.myColor = (color == "white") ? 'w' : 'b';

    logger_.infof("Lichess: Found active game: %s", event.gameId.c_str());
    return true;
  }

  return false;
}

bool LichessAPI::pollGameStream(const String& gameId, LichessGameState& state) {
  WiFiClientSecure client;
  if (!connectGameStream(client, gameId)) return false;

  std::atomic<bool> cancel{false};
  bool found = readStreamEvent(client, state, cancel, 15000);
  client.stop();
  return found;
}

// Parse a space-separated UCI moves string and update state fields.
static void applyMovesList(const String& moves, LichessGameState& state) {
  int moveCount = 0;
  if (moves.length() > 0) {
    moveCount = 1;
    for (size_t i = 0; i < moves.length(); i++)
      if (moves[i] == ' ') moveCount++;
    int lastSpace = moves.lastIndexOf(' ');
    state.lastMove = (lastSpace >= 0) ? moves.substring(lastSpace + 1) : moves;
  } else {
    state.lastMove = "";
  }
  state.moveCount = moveCount;
  state.isMyTurn = ((moveCount % 2 == 0) && state.myColor == 'w') ||
                   ((moveCount % 2 == 1) && state.myColor == 'b');
}

// Check whether the game has ended and populate state accordingly.
static void checkGameEndStatus(JsonObject obj, LichessGameState& state) {
  String status = obj["status"].as<String>();
  state.status = status;
  if (status == "mate" || status == "resign" || status == "stalemate" || status == "timeout" || status == "draw" || status == "outoftime" || status == "aborted") {
    state.gameEnded = true;
    if (!obj["winner"].isNull())
      state.winner = obj["winner"].as<String>();
  }
}

// ---------------------------------------------------------------

bool LichessAPI::parseGameFullEvent(const String& json, LichessGameState& state) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    logger_.error("Lichess: JSON parse error in parseGameFullEvent");
    return false;
  }

  // Check if this is a "gameFull" event
  if (doc["type"].isNull() || doc["type"].as<String>() != "gameFull") {
    // Try to parse anyway if it has the right structure
    if (doc["id"].isNull() && doc["state"].isNull()) {
      return false;
    }
  }

  state.gameId = doc["id"].as<String>();
  state.gameStarted = true;
  state.gameEnded = false;

  // myColor is already set by the caller from the initial game event.
  // parseGameFullEvent does not override it — the event-level color
  // (from /api/account/playing) is authoritative.

  // Get game state
  if (!doc["state"].isNull()) {
    JsonObject stateObj = doc["state"];
    String moves = stateObj["moves"].as<String>();
    applyMovesList(moves, state);

    // Get FEN if available
    if (!stateObj["fen"].isNull()) {
      state.fen = stateObj["fen"].as<String>();
    }

    checkGameEndStatus(stateObj, state);
  }

  // Get initial FEN if provided
  if (!doc["initialFen"].isNull() && doc["initialFen"].as<String>() != "startpos") {
    state.fen = doc["initialFen"].as<String>();
  }

  return true;
}

bool LichessAPI::parseGameStateEvent(const String& json, LichessGameState& state) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    return false;
  }

  // Check if this is a "gameState" event
  String type = doc["type"].as<String>();
  if (type != "gameState") {
    // Could be chatLine or other events
    return false;
  }

  String moves = doc["moves"].as<String>();
  applyMovesList(moves, state);

  checkGameEndStatus(doc.as<JsonObject>(), state);

  return true;
}

bool LichessAPI::makeMove(const String& gameId, const String& move) {
  String path = "/api/board/game/" + gameId + "/move/" + move;
  String response = makeHttpRequest("POST", path);

  // Check for success
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    // Sometimes the response is just "ok" or empty on success
    return response.indexOf("\"ok\":true") >= 0;
  }

  if (!doc["ok"].isNull() && doc["ok"].as<bool>()) {
    logger_.infof("Lichess: Move sent successfully: %s", move.c_str());
    return true;
  }

  logger_.errorf("Lichess: Move failed: %s", response.c_str());
  return false;
}

bool LichessAPI::resignGame(const String& gameId) {
  String path = "/api/board/game/" + gameId + "/resign";
  String response = makeHttpRequest("POST", path);
  return response.indexOf("\"ok\":true") >= 0;
}

// ---------------------------------------------------------------
// Persistent stream helpers
// ---------------------------------------------------------------

// Return true if `line` looks like a chunked-encoding size (hex digits only, ≤8 chars).
static bool isChunkSizeLine(const String& line) {
  if (line.length() == 0 || line.length() > 8) return false;
  for (size_t i = 0; i < line.length(); i++) {
    char c = line[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
      return false;
  }
  return true;
}

bool LichessAPI::connectGameStream(WiFiClientSecure& client, const String& gameId) {
  client.setInsecure();

  if (!client.connect(LICHESS_API_HOST, LICHESS_API_PORT)) {
    logger_.error("Lichess stream: connection failed");
    return false;
  }

  // Request with keep-alive — Lichess streams use chunked transfer encoding
  String request = "GET /api/board/game/stream/" + gameId + " HTTP/1.1\r\n";
  request += "Host: " LICHESS_API_HOST "\r\n";
  request += "Authorization: Bearer " + config_.apiToken + "\r\n";
  request += "Accept: application/x-ndjson\r\n";
  request += "Connection: keep-alive\r\n\r\n";

  client.print(request);

  // Skip HTTP response headers
  unsigned long timeout = millis() + 15000;
  while (client.connected()) {
    if (millis() > timeout) {
      logger_.error("Lichess stream: header timeout");
      client.stop();
      return false;
    }
    if (client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) break;  // Empty line = end of headers
    }
    delay(1);
  }

  if (!client.connected()) {
    logger_.error("Lichess stream: disconnected during headers");
    return false;
  }

  logger_.info("Lichess stream: connected, headers consumed");
  return true;
}

bool LichessAPI::readStreamEvent(WiFiClientSecure& client, LichessGameState& state,
                                 const std::atomic<bool>& cancel, unsigned long timeoutMs) {
  unsigned long deadline = millis() + timeoutMs;

  while (client.connected() || client.available()) {
    if (cancel.load()) return false;
    if (millis() > deadline) return false;

    if (!client.available()) {
      delay(10);
      continue;
    }

    String line = client.readStringUntil('\n');
    line.trim();

    // Skip empty heartbeat lines
    if (line.length() == 0) {
      deadline = millis() + timeoutMs;  // Reset timeout on heartbeat
      continue;
    }

    // Skip chunked-encoding size lines
    if (isChunkSizeLine(line)) continue;

    // Must start with '{' to be valid JSON
    if (line[0] != '{') continue;

    logger_.infof("Lichess stream event: %.200s", line.c_str());

    // Try gameFull first, then gameState
    if (parseGameFullEvent(line, state)) return true;
    if (parseGameStateEvent(line, state)) return true;

    logger_.info("Lichess stream: unrecognized event, skipping");
  }

  // Connection dropped
  return false;
}
