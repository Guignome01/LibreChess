#ifndef LITTLEFS_STORAGE_H
#define LITTLEFS_STORAGE_H

#include <Arduino.h>
#include <vector>

#include "storage.h"
#include "logger.h"

using namespace LibreChess;

// Concrete IGameStorage implementation backed by LittleFS on ESP32.
// Manages binary game files under /games/ with crash-recovery semantics.
class LittleFSStorage : public IGameStorage {
 public:
  explicit LittleFSStorage(ILogger* logger = nullptr);

  // --- IGameStorage interface ---
  void initialize() override;
  void beginGame(const GameHeader& header) override;
  void appendMoveData(const uint8_t* data, size_t length) override;
  void truncateMoveData(size_t byteOffset) override;
  void updateHeader(const GameHeader& header) override;
  size_t appendFenEntry(const std::string& fen) override;
  void finalizeGame(const GameHeader& header) override;
  void discardGame() override;
  bool hasActiveGame() override;
  bool readHeader(GameHeader& header) override;
  bool readMoveData(std::vector<uint8_t>& data) override;
  bool readFenAt(size_t offset, std::string& fen) override;
  bool deleteGame(int id) override;
  void enforceStorageLimits() override;

  // --- Concrete-only methods (not on IGameStorage) ---

  // JSON array of game metadata — uses ArduinoJson, firmware-specific.
  String getGameListJSON();

  // Build path string for a game id.
  static String gamePath(int id);

  // POSIX stat()-based existence check (avoids noisy VFS log output).
  static bool quietExists(const char* path);

 private:
  Log logger_;

  static constexpr const char* GAMES_DIR = "/games";
  static constexpr const char* LIVE_MOVES_PATH = "/games/live.bin";
  static constexpr const char* LIVE_FEN_PATH = "/games/live_fen.bin";

  // Collect sorted list of existing game ids.
  std::vector<int> listGameIds();

  // Enforce storage limits given an already-fetched list of game ids.
  // Modifies ids in-place (removes pruned entries).
  void enforceStorageLimitsInternal(std::vector<int>& ids);

  // Obtain a Unix timestamp (returns 0 if NTP has not synced).
  static uint32_t getTimestamp();
};

#endif  // LITTLEFS_STORAGE_H
