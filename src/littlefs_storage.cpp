#include "littlefs_storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <algorithm>
#include <sys/stat.h>
#include <time.h>

LittleFSStorage::LittleFSStorage(ILogger* logger) : logger_(logger) {}

// ---------------------------------------------------------------------------
// IGameStorage interface
// ---------------------------------------------------------------------------

void LittleFSStorage::initialize() {
  if (!quietExists(GAMES_DIR))
    LittleFS.mkdir(GAMES_DIR);
}

void LittleFSStorage::beginGame(const GameHeader& header) {
  // Discard any leftover live files
  discardGame();

  // Write initial header with a fresh timestamp
  GameHeader hdr = header;
  hdr.timestamp = getTimestamp();

  File f = LittleFS.open(LIVE_MOVES_PATH, "w");
  if (f) {
    f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    f.close();
  }

  // Create empty FEN table file
  File ft = LittleFS.open(LIVE_FEN_PATH, "w");
  if (ft) ft.close();
}

void LittleFSStorage::appendMoveData(const uint8_t* data, size_t length) {
  File f = LittleFS.open(LIVE_MOVES_PATH, "a");
  if (f) {
    f.write(data, length);
    f.close();
  }
}

void LittleFSStorage::truncateMoveData(size_t byteOffset) {
  File f = LittleFS.open(LIVE_MOVES_PATH, "r");
  if (!f || f.size() < sizeof(GameHeader)) return;

  size_t keepSize = sizeof(GameHeader) + byteOffset;
  if (keepSize >= f.size()) {
    f.close();
    return;  // Nothing to truncate
  }

  // Read the portion we want to keep
  std::vector<uint8_t> buf(keepSize);
  f.read(buf.data(), keepSize);
  f.close();

  // Rewrite the file with only the kept portion
  File fw = LittleFS.open(LIVE_MOVES_PATH, "w");
  if (fw) {
    fw.write(buf.data(), keepSize);
    fw.close();
  }
}

void LittleFSStorage::updateHeader(const GameHeader& header) {
  File f = LittleFS.open(LIVE_MOVES_PATH, "r+");
  if (f) {
    f.seek(0);
    f.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    f.close();
  }
}

size_t LittleFSStorage::appendFenEntry(const std::string& fen) {
  File ft = LittleFS.open(LIVE_FEN_PATH, "a");
  if (!ft) return 0;

  size_t offset = ft.size();
  uint8_t len = static_cast<uint8_t>(std::min(fen.length(), static_cast<size_t>(255)));
  ft.write(len);
  ft.write(reinterpret_cast<const uint8_t*>(fen.c_str()), len);
  ft.close();
  return offset;
}

void LittleFSStorage::finalizeGame(const GameHeader& header) {
  // Update header with final result and refreshed timestamp
  GameHeader hdr = header;
  uint32_t ts = getTimestamp();
  if (ts > 0) hdr.timestamp = ts;
  updateHeader(hdr);

  // Read FEN table
  std::vector<uint8_t> fenData;
  if (quietExists(LIVE_FEN_PATH)) {
    File ft = LittleFS.open(LIVE_FEN_PATH, "r");
    if (ft) {
      fenData.resize(ft.size());
      ft.read(fenData.data(), fenData.size());
      ft.close();
    }
  }

  // Append FEN table to live moves file
  if (!fenData.empty()) {
    File fm = LittleFS.open(LIVE_MOVES_PATH, "a");
    if (fm) {
      fm.write(fenData.data(), fenData.size());
      fm.close();
    }
  }

  // Single directory scan for both cleanup and next-ID determination
  auto ids = listGameIds();
  enforceStorageLimitsInternal(ids);

  // Rename to completed game file
  int id = ids.empty() ? 1 : ids.back() + 1;
  String dest = gamePath(id);
  LittleFS.rename(LIVE_MOVES_PATH, dest.c_str());
  discardGame();  // Remove remaining live files (FEN table)

  logger_.infof("LittleFSStorage: game saved as %s (%d moves) (%d FEN entries)",
                   dest.c_str(), hdr.moveCount, hdr.fenEntryCnt);
}

void LittleFSStorage::discardGame() {
  if (quietExists(LIVE_MOVES_PATH)) LittleFS.remove(LIVE_MOVES_PATH);
  if (quietExists(LIVE_FEN_PATH)) LittleFS.remove(LIVE_FEN_PATH);
}

bool LittleFSStorage::hasActiveGame() {
  return quietExists(LIVE_MOVES_PATH);
}

bool LittleFSStorage::readHeader(GameHeader& header) {
  File f = LittleFS.open(LIVE_MOVES_PATH, "r");
  if (!f || f.size() < sizeof(GameHeader)) return false;
  f.read(reinterpret_cast<uint8_t*>(&header), sizeof(header));
  f.close();
  return true;
}

bool LittleFSStorage::readMoveData(std::vector<uint8_t>& data) {
  File f = LittleFS.open(LIVE_MOVES_PATH, "r");
  if (!f || f.size() < sizeof(GameHeader)) return false;

  // Derive move data size from file — more robust than header's moveCount
  // which may lag by one move due to turn-based header flushing.
  f.seek(sizeof(GameHeader));
  size_t dataSize = f.size() - sizeof(GameHeader);
  data.resize(dataSize);
  size_t bytesRead = f.read(data.data(), dataSize);
  f.close();

  if (bytesRead != dataSize) {
    data.clear();
    return false;
  }
  return true;
}

bool LittleFSStorage::readFenAt(size_t offset, std::string& fen) {
  File ft = LittleFS.open(LIVE_FEN_PATH, "r");
  if (!ft) return false;

  ft.seek(offset);
  uint8_t len = ft.read();
  if (len == 0) {
    ft.close();
    return false;
  }

  char buf[256];  // Max FEN length is 255 (stored as uint8_t)
  ft.read(reinterpret_cast<uint8_t*>(buf), len);
  buf[len] = '\0';
  fen = buf;
  ft.close();
  return true;
}

bool LittleFSStorage::deleteGame(int id) {
  String path = gamePath(id);
  if (!quietExists(path.c_str())) return false;
  return LittleFS.remove(path);
}

void LittleFSStorage::enforceStorageLimits() {
  auto ids = listGameIds();
  enforceStorageLimitsInternal(ids);
}

void LittleFSStorage::enforceStorageLimitsInternal(std::vector<int>& ids) {
  // 1. Enforce MAX_GAMES
  while (static_cast<int>(ids.size()) > MAX_GAMES) {
    LittleFS.remove(gamePath(ids.front()));
    ids.erase(ids.begin());
    logger_.info("LittleFSStorage: deleted oldest game (max game limit)");
  }

  // 2. Enforce MAX_USAGE_PERCENT
  while (!ids.empty()) {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    if (total == 0 || static_cast<float>(used) / static_cast<float>(total) <= MAX_USAGE_PERCENT)
      break;
    LittleFS.remove(gamePath(ids.front()));
    ids.erase(ids.begin());
    logger_.info("LittleFSStorage: deleted oldest game (storage limit)");
  }
}

// ---------------------------------------------------------------------------
// Concrete-only methods
// ---------------------------------------------------------------------------

String LittleFSStorage::getGameListJSON() {
  auto ids = listGameIds();
  JsonDocument doc;
  JsonArray arr = doc["games"].to<JsonArray>();

  for (int id : ids) {
    File f = LittleFS.open(gamePath(id), "r");
    if (!f || f.size() < sizeof(GameHeader)) continue;

    GameHeader hdr;
    f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
    f.close();

    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = id;
    obj["mode"] = hdr.meta[0];
    obj["engineId"] = hdr.meta[1];
    obj["result"] = static_cast<uint8_t>(hdr.result);
    obj["winner"] = String(static_cast<char>(hdr.winnerColor));
    obj["playerColor"] = hdr.playerColor ? String(static_cast<char>(hdr.playerColor)) : String("?");
    obj["difficulty"] = hdr.meta[2];
    obj["moveCount"] = hdr.moveCount;
    obj["timestamp"] = hdr.timestamp;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

String LittleFSStorage::gamePath(int id) {
  char buf[24];
  snprintf(buf, sizeof(buf), "/games/game_%02d.bin", id);
  return String(buf);
}

bool LittleFSStorage::quietExists(const char* path) {
  struct stat st;
  String fullPath = "/littlefs" + String(path);
  return (stat(fullPath.c_str(), &st) == 0);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::vector<int> LittleFSStorage::listGameIds() {
  std::vector<int> ids;
  File dir = LittleFS.open(GAMES_DIR);
  if (!dir || !dir.isDirectory()) return ids;

  File f = dir.openNextFile();
  while (f) {
    String name = f.name();
    if (name.startsWith("game_") && name.endsWith(".bin")) {
      int id = name.substring(5, name.length() - 4).toInt();
      if (id > 0) ids.push_back(id);
    }
    f = dir.openNextFile();
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

// Threshold: 2026-02-14T12:32:48Z — any timestamp before this indicates
// NTP has not yet synced (ESP32 starts counting from epoch 0 at boot).
static constexpr uint32_t NTP_SYNC_THRESHOLD = 1771008768UL;

uint32_t LittleFSStorage::getTimestamp() {
  time_t now = time(nullptr);
  return (now > NTP_SYNC_THRESHOLD) ? static_cast<uint32_t>(now) : 0;
}
