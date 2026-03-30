#include "wifi_manager_esp32.h"
#include "game.h"
#include "engine/lichess/lichess_config.h"
#include "system_utils.h"
#include "littlefs_storage.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

static const char* INITIAL_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// --- Response helpers ---

static void sendJsonOk(AsyncWebServerRequest* request, const char* key = nullptr, const char* value = nullptr) {
  JsonDocument doc;
  doc["ok"] = true;
  if (key && value) {
    doc[key] = value;
  }
  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);
}

static void sendJsonError(AsyncWebServerRequest* request, int code, const char* msg) {
  JsonDocument doc;
  doc["error"] = msg;
  String body;
  serializeJson(doc, body);
  request->send(code, "application/json", body);
}

static AsyncWebServerResponse* beginJsonOk(AsyncWebServerRequest* request) {
  return request->beginResponse(200, "application/json", "{\"ok\":true}");
}

// Singleton instance for WiFi event callback
WiFiManagerESP32* WiFiManagerESP32::instance = nullptr;

// ===========================
// WiFiManagerESP32
// ===========================

WiFiManagerESP32::WiFiManagerESP32(BoardDriver* bd, LittleFSStorage* st) : boardDriver(bd), storage_(st), server(AP_PORT), gameMode("0"), lichessToken(""), botPlayerColor('w'), currentFen(INITIAL_FEN), hasPendingEdit(false), boardEvaluation(0.0f) {}

void WiFiManagerESP32::begin() {
  Serial.println("=== Starting LibreChess WiFi Manager (ESP32) ===");
  instance = this;

  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - credentials not loaded");
  } else {
    loadNetworks();
    loadOtaPassword();

    // Load Lichess token
    prefs.begin("lichess", false);
    if (prefs.isKey("token")) {
      lichessToken = prefs.getString("token", "");
    }
    prefs.end();
    if (lichessToken.length() > 0) {
      Serial.println("Lichess API token loaded from NVS");
    }
  }

  // Start AP — always active initially
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("ERROR: Failed to create Access Point!");
    return;
  }
  apActive = true;
  wifiState = WiFiState::AP_ONLY;

  // Register WiFi event handler for state machine (observer pattern)
  WiFi.onEvent(onWiFiEvent);

  // Create stabilization timer (one-shot, not started yet)
  apStabilizationTimer = xTimerCreate("apStab", pdMS_TO_TICKS(AP_STABILIZATION_MS), pdFALSE, this, apStabilizationCallback);

  // Attempt connection to saved networks
  bool connected = false;
  for (uint8_t i = 0; i < networkCount && !connected; i++) {
    wifiState = WiFiState::CONNECTING;
    connected = connectToNetwork(i);
  }
  if (!connected) {
    wifiState = WiFiState::AP_ONLY;
  }

  // Start mDNS responder — enables http://librechess.local access
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", AP_PORT);
    Serial.println("mDNS started: http://" MDNS_HOSTNAME ".local");
  } else {
    Serial.println("mDNS failed to start");
  }

  Serial.println("==== WiFi Connection Information ====");
  Serial.println("Access Point:");
  Serial.println("  SSID: " AP_SSID);
  Serial.println("  Password: " AP_PASSWORD);
  Serial.println("  URL: http://" + WiFi.softAPIP().toString());
  Serial.println("  URL: http://" MDNS_HOSTNAME ".local");
  if (connected) {
    Serial.println("Connected to WiFi:");
    Serial.println("  SSID: " + savedNetworks[connectedNetworkIndex].ssid);
    Serial.println("  URL: http://" + WiFi.localIP().toString());
    Serial.println("  URL: http://" MDNS_HOSTNAME ".local");
  } else {
    Serial.println("Not connected to any WiFi network");
    Serial.println("Configure WiFi from the web interface (Stockfish needs internet)");
  }
  Serial.println("=====================================");

  // --- Set up web server routes ---

  // Health check endpoint (used by OTA reboot polling)
  server.on("/health", HTTP_GET, [](AsyncWebServerRequest* request) { sendJsonOk(request); });

  // OTA endpoints
  server.on("/ota/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    sendJsonOk(request, "hasPassword", this->otaPasswordHash.isEmpty() ? "false" : "true");
  });
  server.on("/ota/verify", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (this->otaPasswordHash.isEmpty()) {
      sendJsonOk(request);
      return;
    }
    if (!request->hasArg("password") || !this->verifyOtaPassword(request->arg("password"))) {
      sendJsonError(request, 401, "Incorrect OTA password");
      return;
    }
    sendJsonOk(request);
  });
  server.on("/ota/password", HTTP_POST, [this](AsyncWebServerRequest* request) { this->handleOtaPassword(request); });

  // Board endpoints
  server.on("/board-update", HTTP_GET, [this](AsyncWebServerRequest* request) { request->send(200, "application/json", this->getBoardUpdateJSON()); });
  server.on("/board-update", HTTP_POST, [this](AsyncWebServerRequest* request) { this->handleBoardEditSuccess(request); });

  // WiFi network management endpoints
  server.on("/wifi/networks", HTTP_GET, [this](AsyncWebServerRequest* request) { request->send(200, "application/json", this->getNetworksJSON()); });
  server.on("/wifi/networks", HTTP_POST, [this](AsyncWebServerRequest* request) { this->handleAddNetwork(request); });
  server.on("/wifi/networks", HTTP_DELETE, [this](AsyncWebServerRequest* request) { this->handleDeleteNetwork(request); });
  server.on("/wifi/connect", HTTP_POST, [this](AsyncWebServerRequest* request) { this->handleConnectNetwork(request); });
  server.on("/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) { this->handleWiFiScan(request); });

  // Game and settings endpoints
  server.on("/gameselect", HTTP_POST, [this](AsyncWebServerRequest* request) { this->handleGameSelection(request); });
  server.on("/lichess", HTTP_GET, [this](AsyncWebServerRequest* request) { request->send(200, "application/json", this->getLichessInfoJSON()); });
  server.on("/lichess", HTTP_POST, [this](AsyncWebServerRequest* request) { this->handleSaveLichessToken(request); });
  server.on("/board-settings", HTTP_GET, [this](AsyncWebServerRequest* request) { request->send(200, "application/json", this->getBoardSettingsJSON()); });
  server.on("/board-settings", HTTP_POST, [this](AsyncWebServerRequest* request) { this->handleBoardSettings(request); });
  server.on("/board-calibrate", HTTP_POST, [this](AsyncWebServerRequest* request) { this->handleBoardCalibration(request); });
  server.on("/ota", HTTP_POST,
    [this](AsyncWebServerRequest* request) { this->handleOtaResult(request); },
    [this](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
      this->handleOtaUpload(request, filename, index, data, len, final);
    });
  server.on("/games", HTTP_GET, [this](AsyncWebServerRequest* request) { this->handleGamesRequest(request); });
  server.on("/games", HTTP_DELETE, [this](AsyncWebServerRequest* request) { this->handleDeleteGame(request); });
  server.on("/resign", HTTP_POST, [this](AsyncWebServerRequest* request) {
    this->hasPendingResign = true;
    sendJsonOk(request);
  });
  server.on("/nav", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (this->navigationBlocked_) {
      sendJsonError(request, 409, "Navigation blocked");
      return;
    }
    if (!request->hasArg("action")) {
      sendJsonError(request, 400, "Missing action parameter");
      return;
    }
    String action = request->arg("action");
    uint8_t nav = 0;
    if (action == "undo") nav = static_cast<uint8_t>(NavAction::UNDO);
    else if (action == "redo") nav = static_cast<uint8_t>(NavAction::REDO);
    else if (action == "first") nav = static_cast<uint8_t>(NavAction::FIRST);
    else if (action == "last") nav = static_cast<uint8_t>(NavAction::LAST);
    else {
      sendJsonError(request, 400, "Invalid action");
      return;
    }
    this->pendingNavAction_ = nav;
    sendJsonOk(request);
  });

  // Static file serving
  server.serveStatic("/sounds/", LittleFS, "/sounds/").setTryGzipFirst(false);
  server.serveStatic("/pieces/", LittleFS, "/pieces/").setCacheControl("max-age=31536000, immutable");
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Security response headers + 404 handler
  server.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not Found"); });
  DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
  DefaultHeaders::Instance().addHeader("X-Frame-Options", "DENY");

  server.begin();
  Serial.println("Web server started on port 80");
}

// ===========================
// WiFi State Machine
// ===========================

void WiFiManagerESP32::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (!instance) return;
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      instance->handleWiFiConnected();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      instance->handleWiFiDisconnected();
      break;
    default:
      break;
  }
}

void WiFiManagerESP32::handleWiFiConnected() {
  Serial.println("WiFi STA connected — IP: " + WiFi.localIP().toString());

  // Restart mDNS so it binds to the new STA interface
  MDNS.end();
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", AP_PORT);
  }

  if (wifiState == WiFiState::CONNECTING || wifiState == WiFiState::RECONNECTING) {
    // Start stabilization timer before disabling AP
    wifiState = WiFiState::CONNECTED;
    reconnectInterval = RECONNECT_INITIAL_MS; // Reset backoff
    if (apStabilizationTimer) {
      xTimerStart(apStabilizationTimer, 0);
    }
  }
}

void WiFiManagerESP32::handleWiFiDisconnected() {
  // Ignore disconnect events when we're not in a connected/connecting state
  if (wifiState == WiFiState::AP_ONLY) return;

  Serial.println("WiFi STA disconnected");

  // Cancel stabilization timer if it was running (prevents disabling AP during unstable connection)
  if (apStabilizationTimer) {
    xTimerStop(apStabilizationTimer, 0);
  }

  wifiState = WiFiState::RECONNECTING;
  connectedNetworkIndex = -1;
  lastReconnectAttempt = 0; // Force immediate first attempt in update()
  reconnectNetworkIndex = 0;

  enableAP();
}

void WiFiManagerESP32::apStabilizationCallback(TimerHandle_t timer) {
  WiFiManagerESP32* self = static_cast<WiFiManagerESP32*>(pvTimerGetTimerID(timer));
  if (self && WiFi.status() == WL_CONNECTED) {
    self->disableAP();
    Serial.println("AP disabled after stabilization (STA connection stable)");
  }
}

void WiFiManagerESP32::disableAP() {
  if (apActive) {
    WiFi.softAPdisconnect(true);
    apActive = false;
  }
}

void WiFiManagerESP32::enableAP() {
  if (!apActive) {
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    apActive = true;
    Serial.println("AP re-enabled — SSID: " AP_SSID " IP: " + WiFi.softAPIP().toString());
  }
}

void WiFiManagerESP32::update() {
  // Only handle reconnection logic when in RECONNECTING state
  if (wifiState != WiFiState::RECONNECTING || networkCount == 0) return;

  unsigned long now = millis();
  if (now - lastReconnectAttempt < reconnectInterval) return;
  lastReconnectAttempt = now;

  Serial.printf("Reconnection attempt — trying network %d/%d (backoff %lums)\n", reconnectNetworkIndex + 1, networkCount, reconnectInterval);

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(savedNetworks[reconnectNetworkIndex].ssid.c_str(), savedNetworks[reconnectNetworkIndex].password.c_str());

  // Move to next network for the next attempt
  reconnectNetworkIndex = (reconnectNetworkIndex + 1) % networkCount;

  // Exponential backoff: 5s → 10s → 20s → 40s → 60s cap
  reconnectInterval = min(reconnectInterval * 2, RECONNECT_MAX_MS);
}

// ===========================
// Known-Networks Registry
// ===========================

void WiFiManagerESP32::loadNetworks() {
  prefs.begin("wifiNets", true);
  networkCount = prefs.getUChar("count", 0);
  if (networkCount > MAX_SAVED_NETWORKS) networkCount = MAX_SAVED_NETWORKS;

  for (uint8_t i = 0; i < networkCount; i++) {
    savedNetworks[i].ssid = prefs.getString(("ssid" + String(i)).c_str(), "");
    savedNetworks[i].password = prefs.getString(("pass" + String(i)).c_str(), "");
  }
  prefs.end();

  Serial.printf("Loaded %d saved network(s)\n", networkCount);
}

void WiFiManagerESP32::saveNetworks() {
  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - networks not saved");
    return;
  }
  prefs.begin("wifiNets", false);
  prefs.clear(); // Clear all keys first to handle deletions
  prefs.putUChar("count", networkCount);
  for (uint8_t i = 0; i < networkCount; i++) {
    prefs.putString(("ssid" + String(i)).c_str(), savedNetworks[i].ssid);
    prefs.putString(("pass" + String(i)).c_str(), savedNetworks[i].password);
  }
  prefs.end();
}

bool WiFiManagerESP32::connectToNetwork(uint8_t index) {
  if (index >= networkCount) return false;

  const SavedNetwork& net = savedNetworks[index];
  if (net.ssid.isEmpty()) return false;

  Serial.printf("Connecting to '%s'...\n", net.ssid.c_str());
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(net.ssid.c_str(), net.password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    boardDriver->showConnectingAnimation();
    attempts++;
    Serial.printf("  Attempt %d/10 — status: %d\n", attempts, WiFi.status());
  }

  if (WiFi.status() == WL_CONNECTED) {
    connectedNetworkIndex = index;
    Serial.printf("Connected to '%s'\n", net.ssid.c_str());
    return true;
  }

  Serial.printf("Failed to connect to '%s'\n", net.ssid.c_str());
  return false;
}

// ===========================
// OTA Password
// ===========================

void WiFiManagerESP32::loadOtaPassword() {
  prefs.begin("ota", true);
  otaPasswordHash = prefs.getString("passHash", "");
  otaPasswordSalt = prefs.getString("salt", "");
  prefs.end();

  if (!otaPasswordHash.isEmpty())
    Serial.println("OTA password configured");
}

String WiFiManagerESP32::hashPassword(const String& password, const String& salt) const {
  String salted = salt + password;
  uint8_t hash[32];
  mbedtls_sha256((const uint8_t*)salted.c_str(), salted.length(), hash, 0); // 0 = SHA-256 (not SHA-224)

  // Convert to hex string
  char hex[65];
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02x", hash[i]);
  }
  hex[64] = '\0';
  return String(hex);
}

bool WiFiManagerESP32::verifyOtaPassword(const String& password) const {
  return hashPassword(password, otaPasswordSalt) == otaPasswordHash;
}

String WiFiManagerESP32::generateRandomHex(size_t bytes) const {
  String hex;
  hex.reserve(bytes * 2);
  for (size_t i = 0; i < bytes; i++) {
    char buf[3];
    sprintf(buf, "%02x", (uint8_t)esp_random());
    hex += buf;
  }
  return hex;
}

void WiFiManagerESP32::handleOtaPassword(AsyncWebServerRequest* request) {

  bool hasExisting = !otaPasswordHash.isEmpty();

  // Changing or removing requires the current password
  if (hasExisting) {
    if (!request->hasArg("currentPassword")) {
      sendJsonError(request, 400, "Current password required");
      return;
    }
    if (!verifyOtaPassword(request->arg("currentPassword"))) {
      sendJsonError(request, 401, "Incorrect current password");
      return;
    }
  }

  // Empty newPassword = remove
  if (!request->hasArg("newPassword") || request->arg("newPassword").isEmpty()) {
    if (!hasExisting) {
      sendJsonError(request, 400, "No password to remove");
      return;
    }
    prefs.begin("ota", false);
    prefs.remove("passHash");
    prefs.remove("salt");
    prefs.end();
    otaPasswordHash = "";
    otaPasswordSalt = "";
    sendJsonOk(request);
    Serial.println("OTA password removed");
    return;
  }

  // Set or change
  String newPassword = request->arg("newPassword");
  if (request->hasArg("confirmPassword") && newPassword != request->arg("confirmPassword")) {
    sendJsonError(request, 400, "Passwords do not match");
    return;
  }
  if (newPassword.length() < 4) {
    sendJsonError(request, 400, "Password must be at least 4 characters");
    return;
  }

  String salt = generateRandomHex(16);
  String hash = hashPassword(newPassword, salt);
  prefs.begin("ota", false);
  prefs.putString("passHash", hash);
  prefs.putString("salt", salt);
  prefs.end();
  otaPasswordHash = hash;
  otaPasswordSalt = salt;

  sendJsonOk(request);
  Serial.println("OTA password " + String(hasExisting ? "changed" : "set"));
}

// ===========================
// WiFi Network Endpoints
// ===========================

String WiFiManagerESP32::getNetworksJSON() {
  JsonDocument doc;
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (uint8_t i = 0; i < networkCount; i++) {
    JsonObject net = arr.add<JsonObject>();
    net["index"] = i;
    net["ssid"] = savedNetworks[i].ssid;
    net["connected"] = (i == connectedNetworkIndex && WiFi.status() == WL_CONNECTED);
    if (i == connectedNetworkIndex && WiFi.status() == WL_CONNECTED) {
      net["ip"] = WiFi.localIP().toString();
      net["rssi"] = WiFi.RSSI();
    }
    // Never expose passwords
  }
  doc["apActive"] = apActive;
  doc["apIp"] = WiFi.softAPIP().toString();
  doc["hostname"] = MDNS_HOSTNAME ".local";
  doc["maxNetworks"] = MAX_SAVED_NETWORKS;
  doc["wifiState"] = static_cast<int>(wifiState);
  String output;
  serializeJson(doc, output);
  return output;
}

String WiFiManagerESP32::getScanResultsJSON() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true); // Start async scan
    return "{\"scanning\":true}";
  }
  if (n == WIFI_SCAN_RUNNING) {
    return "{\"scanning\":true}";
  }

  JsonDocument doc;
  doc["scanning"] = false;
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n && i < 20; i++) { // Cap at 20 results
    JsonObject net = arr.add<JsonObject>();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["encryption"] = WiFi.encryptionType(i);
  }
  WiFi.scanDelete();
  String output;
  serializeJson(doc, output);
  return output;
}

void WiFiManagerESP32::handleAddNetwork(AsyncWebServerRequest* request) {
  if (!request->hasArg("ssid") || !request->hasArg("password")) {
    sendJsonError(request, 400, "Missing ssid or password");
    return;
  }

  String ssid = request->arg("ssid");
  String password = request->arg("password");

  if (ssid.length() < 1) {
    sendJsonError(request, 400, "SSID too short");
    return;
  }
  if (password.length() < 5) {
    sendJsonError(request, 400, "Password must be at least 5 characters");
    return;
  }

  // Check if SSID already exists — update password instead of adding duplicate
  for (uint8_t i = 0; i < networkCount; i++) {
    if (savedNetworks[i].ssid == ssid) {
      savedNetworks[i].password = password;
      saveNetworks();
      sendJsonOk(request, "action", "updated");
      Serial.printf("WiFi: updated password for '%s'\n", ssid.c_str());
      return;
    }
  }

  if (networkCount >= MAX_SAVED_NETWORKS) {
    sendJsonError(request, 409, "Maximum networks reached. Delete one first.");
    return;
  }

  savedNetworks[networkCount].ssid = ssid;
  savedNetworks[networkCount].password = password;
  networkCount++;
  saveNetworks();

  sendJsonOk(request, "action", "added");
  Serial.printf("WiFi: added network '%s' (%d/%d)\n", ssid.c_str(), networkCount, MAX_SAVED_NETWORKS);
}

void WiFiManagerESP32::handleDeleteNetwork(AsyncWebServerRequest* request) {
  if (!request->hasArg("index")) {
    sendJsonError(request, 400, "Missing index");
    return;
  }

  int index = request->arg("index").toInt();
  if (index < 0 || index >= networkCount) {
    sendJsonError(request, 400, "Invalid index");
    return;
  }

  String removedSsid = savedNetworks[index].ssid;

  // If deleting the connected network, disconnect
  if (index == connectedNetworkIndex) {
    WiFi.disconnect();
    connectedNetworkIndex = -1;
  } else if (connectedNetworkIndex > index) {
    connectedNetworkIndex--; // Adjust index after shift
  }

  // Shift remaining networks down
  for (int i = index; i < networkCount - 1; i++) {
    savedNetworks[i] = savedNetworks[i + 1];
  }
  savedNetworks[networkCount - 1] = SavedNetwork(); // Clear last slot
  networkCount--;
  saveNetworks();

  sendJsonOk(request);
  Serial.printf("WiFi: deleted network '%s'\n", removedSsid.c_str());
}

void WiFiManagerESP32::handleConnectNetwork(AsyncWebServerRequest* request) {
  if (!request->hasArg("index")) {
    sendJsonError(request, 400, "Missing index");
    return;
  }

  int index = request->arg("index").toInt();
  if (index < 0 || index >= networkCount) {
    sendJsonError(request, 400, "Invalid index");
    return;
  }

  // Respond immediately — connection proceeds on a separate task to avoid
  // blocking the async_tcp task that serves other HTTP requests
  sendJsonOk(request, "message", "Connecting...");

  wifiState = WiFiState::CONNECTING;
  xTaskCreate([](void* param) {
    uint8_t idx = (uint8_t)(uintptr_t)param;
    if (instance->connectToNetwork(idx)) {
      instance->wifiState = WiFiState::CONNECTED;
      if (instance->apStabilizationTimer) {
        xTimerStart(instance->apStabilizationTimer, 0);
      }
    } else {
      instance->wifiState = WiFiState::AP_ONLY;
    }
    vTaskDelete(nullptr);
  }, "WiFiConn", 4096, (void*)(uintptr_t)index, 1, nullptr);
}

void WiFiManagerESP32::handleWiFiScan(AsyncWebServerRequest* request) {
  request->send(200, "application/json", getScanResultsJSON());
}

// ===========================
// Board & Game Endpoints
// ===========================

String WiFiManagerESP32::getBoardUpdateJSON() {
  JsonDocument doc;
  doc["fen"] = currentFen;
  doc["evaluation"] = serialized(String(boardEvaluation, 2));
  doc["moveIndex"] = cachedMoveIndex_;
  doc["moveCount"] = cachedMoveCount_;
  doc["canUndo"] = cachedCanUndo_;
  doc["canRedo"] = cachedCanRedo_;
  String output;
  serializeJson(doc, output);
  return output;
}

void WiFiManagerESP32::handleBoardEditSuccess(AsyncWebServerRequest* request) {
  if (request->hasArg("fen")) {
    pendingFenEdit = request->arg("fen");
    hasPendingEdit = true;
    Serial.println("Board edit received (FEN): " + pendingFenEdit);
    sendJsonOk(request);
  } else {
    Serial.println("Board edit failed: no FEN parameter");
    sendJsonError(request, 400, "Missing FEN parameter");
  }
}

void WiFiManagerESP32::handleGameSelection(AsyncWebServerRequest* request) {
  int mode = 0;
  if (request->hasArg("gamemode"))
    mode = request->arg("gamemode").toInt();
  gameMode = String(mode);
  // If bot game mode, also handle bot config
  if (mode == 2) {
    if (request->hasArg("difficulty") && request->hasArg("playerColor")) {
      int diffLevel = request->arg("difficulty").toInt();
      if (diffLevel < 1 || diffLevel > 8) diffLevel = 4;
      botPlayerColor = (request->arg("playerColor") == "white") ? 'w' : 'b';
      botDifficultyLevel_ = diffLevel;
      botEngine = request->hasArg("engine") ? request->arg("engine") : "stockfish";
      Serial.printf("Bot configuration received: Engine=%s, Level=%d, Player is %s\n",
                     botEngine.c_str(), botDifficultyLevel_,
                     botPlayerColor == 'w' ? "White" : "Black");
    } else {
      sendJsonError(request, 400, "Missing bot parameters");
      return;
    }
  }
  // If Lichess mode, verify token exists
  if (mode == 3) {
    if (lichessToken.length() == 0) {
      sendJsonError(request, 400, "No Lichess API token configured");
      return;
    }
    Serial.println("Lichess mode selected via web");
  }
  Serial.println("Game mode selected via web: " + gameMode);
  sendJsonOk(request);
}

String WiFiManagerESP32::getLichessInfoJSON() {
  // Don't expose the actual token, just whether it exists and a masked version
  String maskedToken = lichessToken.length() > 4
    ? "****" + lichessToken.substring(lichessToken.length() - 4)
    : "";

  JsonDocument doc;
  doc["hasToken"] = (lichessToken.length() > 0);
  doc["maskedToken"] = maskedToken;
  String output;
  serializeJson(doc, output);
  return output;
}

void WiFiManagerESP32::handleSaveLichessToken(AsyncWebServerRequest* request) {
  if (!request->hasArg("token")) {
    sendJsonError(request, 400, "Missing token parameter");
    return;
  }

  String newToken = request->arg("token");
  newToken.trim();

  if (newToken.length() < 10) {
    sendJsonError(request, 400, "Token too short");
    return;
  }

  if (!SystemUtils::ensureNvsInitialized()) {
    sendJsonError(request, 500, "NVS init failed");
    return;
  }

  prefs.begin("lichess", false);
  prefs.putString("token", newToken);
  prefs.end();

  lichessToken = newToken;
  Serial.println("Lichess API token saved to NVS");

  sendJsonOk(request);
}

String WiFiManagerESP32::getBoardSettingsJSON() {
  JsonDocument doc;
  doc["brightness"] = boardDriver->getBrightness();
  doc["dimMultiplier"] = boardDriver->getDimMultiplier();
  String output;
  serializeJson(doc, output);
  return output;
}

void WiFiManagerESP32::handleBoardSettings(AsyncWebServerRequest* request) {
  bool changed = false;

  if (request->hasArg("brightness")) {
    int brightness = request->arg("brightness").toInt();
    if (brightness >= 0 && brightness <= 255) {
      boardDriver->setBrightness((uint8_t)brightness);
      changed = true;
    }
  }

  if (request->hasArg("dimMultiplier")) {
    int dimMult = request->arg("dimMultiplier").toInt();
    if (dimMult >= 0 && dimMult <= 100) {
      boardDriver->setDimMultiplier((uint8_t)dimMult);
      changed = true;
    }
  }

  if (changed) {
    boardDriver->saveLedSettings();
    Serial.println("Board settings updated via web interface");
    sendJsonOk(request);
  } else {
    sendJsonError(request, 400, "No valid settings provided");
  }
}

void WiFiManagerESP32::handleBoardCalibration(AsyncWebServerRequest* request) {
  boardDriver->triggerCalibration();
  sendJsonOk(request);
}

void WiFiManagerESP32::handleOtaResult(AsyncWebServerRequest* request) {
  bool success = !otaHasError;
  AsyncWebServerResponse* response;
  if (success) {
    response = beginJsonOk(request);
  } else {
    JsonDocument doc;
    doc["error"] = otaErrorMessage.isEmpty() ? "Update failed" : otaErrorMessage.c_str();
    String body;
    serializeJson(doc, body);
    response = request->beginResponse(500, "application/json", body);
  }
  response->addHeader("Connection", "close");
  request->send(response);
  otaHasError = false;
  otaErrorMessage = "";
  if (success) {
    Serial.println("OTA update successful, scheduling reboot...");
    xTaskCreate([](void*) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      ESP.restart();
    }, "OtaReboot", 2048, nullptr, 1, nullptr);
  }
}

void WiFiManagerESP32::handleOtaUpload(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    // Reset error state for new upload attempt
    otaHasError = false;
    otaErrorMessage = "";

    // Validate OTA password if one is configured
    if (!otaPasswordHash.isEmpty()) {
      if (!request->hasHeader("X-OTA-Password")) {
        Serial.println("OTA rejected: password required");
        otaHasError = true;
        otaErrorMessage = "OTA password required";
        return;
      }
      if (!verifyOtaPassword(request->header("X-OTA-Password"))) {
        Serial.println("OTA rejected: incorrect password");
        otaHasError = true;
        otaErrorMessage = "Incorrect OTA password";
        return;
      }
    }
    if (otaHasError) {
      Serial.printf("OTA skipping '%s' due to previous error\n", filename.c_str());
      return;
    }
    if (!filename.endsWith(".bin")) {
      Serial.printf("OTA rejected: invalid file type '%s'\n", filename.c_str());
      otaHasError = true;
      return;
    }
    // Validate ESP32 magic byte (0xE9) for firmware binaries
    bool isFilesystem = filename.indexOf("littlefs") >= 0 || filename.indexOf("spiffs") >= 0;
    if (!isFilesystem && len > 0 && data[0] != 0xE9) {
      Serial.printf("OTA rejected: invalid firmware magic byte (0x%02X)\n", data[0]);
      otaHasError = true;
      return;
    }
    int command = isFilesystem ? U_SPIFFS : U_FLASH;
    Serial.printf("OTA %s update starting: %s\n", isFilesystem ? "filesystem" : "firmware", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
      Update.printError(Serial);
      otaHasError = true;
      return;
    }
  }

  if (otaHasError || Update.hasError())
    return;

  if (Update.write(data, len) != len) {
    Update.printError(Serial);
    Update.abort();
    otaHasError = true;
    return;
  }

  if (final) {
    if (Update.end(true)) {
      Serial.printf("OTA update complete: %u bytes\n", index + len);
    } else {
      Update.printError(Serial);
      otaHasError = true;
    }
  }
}

LichessConfig WiFiManagerESP32::getLichessConfig() {
  LichessConfig config;
  config.apiToken = lichessToken;
  return config;
}

// TODO: cached fields are written here (main loop task) and read by
// getBoardUpdateJSON() (async_tcp task) without synchronization. Add a portMUX_TYPE
// spinlock around both the write and read sites to eliminate the data race.
void WiFiManagerESP32::onBoardStateChanged(const std::string& fen, int evaluation) {
  currentFen = fen;
  boardEvaluation = evaluation / 100.0f;

  if (game_) {
    cachedMoveIndex_ = game_->currentMoveIndex();
    cachedCanUndo_ = game_->canUndo();
    cachedCanRedo_ = game_->canRedo();
    cachedMoveCount_ = game_->moveCount();
  }
}

bool WiFiManagerESP32::getPendingBoardEdit(String& fenOut) {
  if (hasPendingEdit) {
    fenOut = pendingFenEdit;
    return true;
  }
  return false;
}

void WiFiManagerESP32::clearPendingEdit() {
  currentFen = std::string(pendingFenEdit.c_str());
  hasPendingEdit = false;
}

void WiFiManagerESP32::handleGamesRequest(AsyncWebServerRequest* request) {
  if (request->hasArg("id")) {
    String idStr = request->arg("id");

    // GET /games?id=live1 — return live moves file directly
    if (idStr == "live1") {
      if (!LittleFSStorage::quietExists("/games/live.bin")) {
        sendJsonError(request, 404, "No live game");
        return;
      }
      AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/games/live.bin", "application/octet-stream", true);
      request->send(response);
      return;
    }

    // GET /games?id=live2 — return live FEN table file directly
    if (idStr == "live2") {
      if (!LittleFSStorage::quietExists("/games/live_fen.bin")) {
        sendJsonError(request, 404, "No live FEN table");
        return;
      }
      AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/games/live_fen.bin", "application/octet-stream", true);
      request->send(response);
      return;
    }

    // GET /games?id=N — return binary of game N
    int id = idStr.toInt();
    if (id <= 0) {
      sendJsonError(request, 400, "Invalid game id");
      return;
    }

    String path = LittleFSStorage::gamePath(id);
    if (!LittleFSStorage::quietExists(path.c_str())) {
      sendJsonError(request, 404, "Game not found");
      return;
    }
    AsyncWebServerResponse* response = request->beginResponse(LittleFS, path, "application/octet-stream", true);
    request->send(response);
  } else {
    // GET /games — return JSON list of all saved games
    request->send(200, "application/json", storage_->getGameListJSON());
  }
}

void WiFiManagerESP32::handleDeleteGame(AsyncWebServerRequest* request) {
  if (!request->hasArg("id")) {
    sendJsonError(request, 400, "Missing id parameter");
    return;
  }

  int id = request->arg("id").toInt();
  if (id <= 0) {
    sendJsonError(request, 400, "Invalid game id");
    return;
  }

  if (storage_->deleteGame(id))
    sendJsonOk(request);
  else
    sendJsonError(request, 404, "Game not found");
}
