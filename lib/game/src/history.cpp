#include "history.h"

#include <cstring>
#include <vector>

#include "position.h"

namespace LibreChess {

// ---------------------------------------------------------------------------
// Header validation — detects corrupt or incompatible live game files.
//
// Silently rejects headers with impossible field values. Guards against
// files written by older firmware whose binary layout no longer matches.
// ---------------------------------------------------------------------------

/// Maximum move entries (including FEN markers) in a single game recording.
static constexpr uint16_t MAX_RECORDING_ENTRIES = 700;

/// Returns true if every GameHeader field is within its valid domain.
static bool isValidHeader(const GameHeader& h) {
  if (static_cast<uint8_t>(h.result) > 9) return false;

  uint8_t w = h.winnerColor;
  if (w != 'w' && w != 'b' && w != 'd' && w != '?') return false;

  uint8_t p = h.playerColor;
  if (p != 'w' && p != 'b' && p != '?') return false;

  if (h.moveCount > MAX_RECORDING_ENTRIES) return false;
  if (h.fenEntryCnt > h.moveCount) return false;

  return true;
}

// --- Compact 2-byte move encoding ---

// Promotion piece mapping: code 0 = none, 1 = queen, 2 = rook, 3 = bishop, 4 = knight.
static constexpr char PROMO_PIECES[] = {' ', 'q', 'r', 'b', 'n'};
static constexpr int PROMO_COUNT = sizeof(PROMO_PIECES);

static uint8_t promoCharToCode(char p) {
  char lower = tolower(p);
  for (int i = 1; i < PROMO_COUNT; i++)
    if (PROMO_PIECES[i] == lower) return i;
  return 0;
}

static char promoCodeToChar(uint8_t code) {
  return (code < PROMO_COUNT) ? PROMO_PIECES[code] : ' ';
}

// Reads a 2-byte persisted move entry without assuming the byte vector is
// aligned for uint16_t loads. ESP32 can fault on unaligned typed accesses.
static bool readEncodedMove(const std::vector<uint8_t>& data, size_t index,
                            uint16_t& encoded) {
  const size_t offset = index * sizeof(uint16_t);
  if (offset + sizeof(uint16_t) > data.size()) return false;
  memcpy(&encoded, data.data() + offset, sizeof(encoded));
  return true;
}

// LERF ↔ wire-format conversion.
// Wire format stores row-major index where row 0 = rank 8 (i.e., inverted
// rank). XOR-56 flips the rank bits, mapping between LERF and wire.
static constexpr uint8_t toWire(Square sq) { return static_cast<uint8_t>(sq ^ 56); }
static constexpr Square fromWire(uint8_t w) { return static_cast<Square>(w ^ 56); }

uint16_t History::encodeMove(Square from, Square to, char promotion) {
  uint8_t wFrom = toWire(from);
  uint8_t wTo = toWire(to);
  uint8_t promo = promoCharToCode(promotion);
  return (uint16_t)((wFrom << 10) | (wTo << 4) | promo);
}

void History::decodeMove(uint16_t encoded, Square& from, Square& to, char& promotion) {
  uint8_t wFrom = (encoded >> 10) & 0x3F;
  uint8_t wTo = (encoded >> 4) & 0x3F;
  uint8_t promo = encoded & 0x0F;
  from = fromWire(wFrom);
  to = fromWire(wTo);
  promotion = promoCodeToChar(promo);
}

// ---------------------------------------------------------------------------

History::History(IGameStorage* storage, ILogger* logger)
    : moveCount_(0),
      currentIndex_(-1),
      storage_(storage),
      logger_(logger),
      recordingActive_(false),
      headerInitialized_(false) {
  memset(&header_, 0, sizeof(header_));
}

void History::clear() {
  moveCount_ = 0;
  currentIndex_ = -1;
}

// ---------------------------------------------------------------------------
// Move log with undo/redo
// ---------------------------------------------------------------------------

void History::addMove(const MoveEntry& entry) {
  if (currentIndex_ + 1 >= MAX_MOVES) return;

  // Branch point: if cursor is not at the end, wipe future moves
  if (currentIndex_ < moveCount_ - 1) {
    int newCount = currentIndex_ + 1;
    // Truncate recording to match
    if (recordingActive_ && storage_) {
      storage_->truncateMoveData(newCount * 2);
      header_.moveCount = static_cast<uint16_t>(newCount);
      storage_->updateHeader(header_);
    }
    moveCount_ = newCount;
  }

  moves_[moveCount_] = entry;
  moveCount_++;
  currentIndex_ = moveCount_ - 1;

  // Persist to storage if recording is active
  if (recordingActive_ && storage_)
    persistMove(entry);
}

const MoveEntry* History::undoMove() {
  if (currentIndex_ < 0) return nullptr;
  return &moves_[currentIndex_--];
}

const MoveEntry* History::redoMove() {
  if (currentIndex_ >= moveCount_ - 1) return nullptr;
  return &moves_[++currentIndex_];
}

const MoveEntry& History::getMove(int index) const {
  return moves_[index];
}

const MoveEntry& History::lastMove() const {
  static const MoveEntry empty{};
  if (currentIndex_ < 0) return empty;
  return moves_[currentIndex_];
}

// ---------------------------------------------------------------------------
// Recording (persistent storage)
// ---------------------------------------------------------------------------

void History::setHeader(const GameHeader& header) {
  if (!storage_) return;

  // If already recording, discard previous game
  if (recordingActive_)
    storage_->discardGame();

  clear();  // Ensure no stale moves survive into the new recording
  header_ = header;
  storage_->beginGame(header_);
  recordingActive_ = true;
  headerInitialized_ = true;

  logger_.info("History: new recording started");
}

void History::snapshotPosition(const std::string& fen) {
  if (!storage_ || !recordingActive_) return;

  // Write FEN_MARKER to the move stream
  uint16_t marker = FEN_MARKER;
  storage_->appendMoveData(reinterpret_cast<const uint8_t*>(&marker), 2);
  header_.moveCount++;

  // Write FEN to the FEN table — storage returns the byte offset
  header_.lastFenOffset = static_cast<uint16_t>(storage_->appendFenEntry(fen));
  header_.fenEntryCnt++;

  storage_->updateHeader(header_);
}

void History::save(GameResult result, char winnerColor) {
  if (!storage_ || !recordingActive_) return;

  recordingActive_ = false;
  headerInitialized_ = false;
  header_.result = result;
  header_.winnerColor = winnerColor;

  storage_->finalizeGame(header_);
  storage_->enforceStorageLimits();

  logger_.infof("History: recording saved (result=%d, moves=%d)", static_cast<int>(result), header_.moveCount);
}

void History::discard() {
  recordingActive_ = false;
  headerInitialized_ = false;
  if (storage_) storage_->discardGame();
}

// ---------------------------------------------------------------------------
// State queries (persistent storage)
// ---------------------------------------------------------------------------

bool History::hasActiveGame() {
  if (!storage_) return false;
  return storage_->hasActiveGame();
}

bool History::getActiveGameInfo(uint8_t& playerColor, uint8_t* meta) {
  if (!storage_) return false;

  GameHeader hdr;
  if (!storage_->readHeader(hdr)) return false;
  if (!isValidHeader(hdr)) return false;

  playerColor = hdr.playerColor;
  if (meta)
    memcpy(meta, hdr.meta, GAME_META_SIZE);
  return true;
}

// ---------------------------------------------------------------------------
// Replay
// ---------------------------------------------------------------------------

bool History::replayInto(Position& board) {
  if (!storage_) return false;

  // Read header from live file
  GameHeader hdr;
  if (!storage_->readHeader(hdr)) return false;
  if (!isValidHeader(hdr)) {
    logger_.error("History: invalid header in live game, discarding");
    storage_->discardGame();
    return false;
  }
  if (hdr.fenEntryCnt == 0) {
    logger_.error("History: no FEN in live game, cannot resume");
    return false;
  }

  // Read all 2-byte move entries
  std::vector<uint8_t> moveData;
  if (!storage_->readMoveData(moveData)) return false;

  size_t entryCount = moveData.size() / 2;

  // Read last FEN
  std::string lastFen;
  if (!storage_->readFenAt(hdr.lastFenOffset, lastFen) || lastFen.empty()) {
    logger_.error("History: failed to read last FEN");
    return false;
  }

  // Find last FEN marker in move stream (scan backwards)
  int lastFenIdx = -1;
  for (int i = static_cast<int>(entryCount) - 1; i >= 0; i--) {
    uint16_t encoded = 0;
    if (readEncodedMove(moveData, static_cast<size_t>(i), encoded) &&
        encoded == FEN_MARKER) {
      lastFenIdx = i;
      break;
    }
  }

  if (lastFenIdx < 0) {
    logger_.error("History: FEN marker not found in moves");
    return false;
  }

  logger_.infof("History: resuming from FEN: %s", lastFen.c_str());

  // Load FEN into board, then replay subsequent moves
  if (!board.loadFEN(lastFen)) {
    logger_.error("History: invalid FEN in recording");
    return false;
  }

  // Store the replay FEN for callers (e.g. Game::resumeGame)
  replayFen_ = lastFen;

  // Clear and repopulate in-memory move log during replay
  clear();

  int replayed = 0;
  for (int i = lastFenIdx + 1; i < static_cast<int>(entryCount); i++) {
    uint16_t encoded = 0;
    if (!readEncodedMove(moveData, static_cast<size_t>(i), encoded)) break;
    if (encoded == FEN_MARKER) continue;  // skip intermediate FEN markers
    Square from, to;
    char promotion;
    decodeMove(encoded, from, to, promotion);

    // Capture pre-move state for the MoveEntry
    Piece piece = board.getSquare(from);
    Piece targetPiece = board.getSquare(to);
    PositionState prevState = board.positionState();

    MoveResult moveResult = board.makeMove(from, to, promotion);
    if (!moveResult.valid()) {
      logger_.errorf("History: invalid move at entry %d during replay", i);
      return false;
    }

    // Build MoveEntry and add to in-memory log (but don't persist — it's already on disk)
    MoveEntry entry = MoveEntry::build(from, to, piece, targetPiece, moveResult, prevState);

    // Add to in-memory log only (bypass persistMove by direct insertion)
    if (moveCount_ < MAX_MOVES) {
      moves_[moveCount_++] = entry;
      currentIndex_ = moveCount_ - 1;
    }

    replayed++;
  }

  // Restore header for continued recording
  header_ = hdr;
  recordingActive_ = true;
  headerInitialized_ = true;

  logger_.infof("History: replayed %d moves from last FEN marker, game resumed", replayed);

  return true;
}

// ---------------------------------------------------------------------------
// Internal: persist a single move to storage
// ---------------------------------------------------------------------------

void History::persistMove(const MoveEntry& entry) {
  char promo = entry.isPromotion() ? piece::pieceToChar(entry.promotion) : ' ';
  uint16_t encoded = encodeMove(entry.from, entry.to, promo);
  storage_->appendMoveData(reinterpret_cast<const uint8_t*>(&encoded), 2);
  header_.moveCount++;
  storage_->updateHeader(header_);
}

}  // namespace LibreChess
