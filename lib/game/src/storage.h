#ifndef LIBRECHESS_STORAGE_H
#define LIBRECHESS_STORAGE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

// Abstract persistence interface for game recording.
// Concrete implementations live in the firmware layer (e.g. LittleFSStorage).
// History depends only on this interface — no filesystem coupling in core.
namespace LibreChess {

class IGameStorage {
 public:
  virtual ~IGameStorage() = default;

  // Create the storage directory structure.
  virtual void initialize() = 0;

  // Create live game files and write the initial header.
  virtual void beginGame(const GameHeader& header) = 0;

  // Append raw encoded bytes to the live move stream.
  virtual void appendMoveData(const uint8_t* data, size_t length) = 0;

  // Truncate the live move stream to exactly byteOffset bytes after the header.
  // Used when undo + branch discards future moves from the recording.
  virtual void truncateMoveData(size_t byteOffset) = 0;

  // Rewrite the header at offset 0 of the live move file.
  virtual void updateHeader(const GameHeader& header) = 0;

  // Append a FEN string to the live FEN table file.
  // Returns the byte offset where this entry was written.
  virtual size_t appendFenEntry(const std::string& fen) = 0;

  // Finalize the live game: update header, merge FEN table, rename to
  // permanent file.
  virtual void finalizeGame(const GameHeader& header) = 0;

  // Remove live game files.
  virtual void discardGame() = 0;

  // Check whether live game files exist.
  virtual bool hasActiveGame() = 0;

  // Read the header from the live move file.
  virtual bool readHeader(GameHeader& header) = 0;

  // Read all move data bytes (after the header) from the live move file.
  virtual bool readMoveData(std::vector<uint8_t>& data) = 0;

  // Read a FEN string at the given byte offset in the live FEN table file.
  virtual bool readFenAt(size_t offset, std::string& fen) = 0;

  // Delete a completed game by id.
  virtual bool deleteGame(int id) = 0;

  // Delete oldest games until count/size limits are satisfied.
  virtual void enforceStorageLimits() = 0;
};

}  // namespace LibreChess

#endif  // CORE_STORAGE_H
