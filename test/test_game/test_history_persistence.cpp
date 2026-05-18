#include <unity.h>

#include "../test_helpers.h"
#include <position.h>
#include <game.h>
#include <history.h>
#include <observer.h>
#include <storage.h>
#include <logger.h>
#include <types.h>

#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// MockLogger — captures log messages for verification
// ---------------------------------------------------------------------------

class MockLogger : public ILogger {
 public:
  std::vector<std::string> infos;
  std::vector<std::string> errors;
  void info(const char* msg) override { infos.push_back(msg); }
  void error(const char* msg) override { errors.push_back(msg); }
};

// ---------------------------------------------------------------------------
// MockGameStorage — in-memory storage for testing
// ---------------------------------------------------------------------------

class MockGameStorage : public IGameStorage {
 public:
  bool initialized = false;
  bool gameActive = false;
  bool gameFinalized = false;
  bool gameDiscarded = false;
  GameHeader storedHeader;
  std::vector<uint8_t> moveData;
  std::vector<std::pair<size_t, std::string>> fenEntries;  // (offset, fen)
  int headerUpdateCount = 0;
  int deleteCount = 0;
  int truncateCount = 0;
  size_t lastTruncateOffset = 0;

  void initialize() override {
    initialized = true;
  }
  void beginGame(const GameHeader& header) override {
    storedHeader = header;
    gameActive = true;
    gameFinalized = false;
    gameDiscarded = false;
    moveData.clear();
    fenEntries.clear();
    headerUpdateCount = 0;
    truncateCount = 0;
  }
  void appendMoveData(const uint8_t* data, size_t len) override {
    for (size_t i = 0; i < len; i++)
      moveData.push_back(data[i]);
  }
  void truncateMoveData(size_t byteOffset) override {
    truncateCount++;
    lastTruncateOffset = byteOffset;
    if (byteOffset < moveData.size())
      moveData.resize(byteOffset);
  }
  void updateHeader(const GameHeader& header) override {
    storedHeader = header;
    headerUpdateCount++;
  }
  size_t appendFenEntry(const std::string& fen) override {
    size_t offset = moveData.size();
    fenEntries.push_back({offset, fen});
    return offset;
  }
  void finalizeGame(const GameHeader& header) override {
    storedHeader = header;
    gameFinalized = true;
    gameActive = false;
  }
  void discardGame() override {
    gameDiscarded = true;
    gameActive = false;
  }
  bool hasActiveGame() override { return gameActive; }
  bool readHeader(GameHeader& header) override {
    if (!gameActive && !gameFinalized) return false;
    header = storedHeader;
    return true;
  }
  bool readMoveData(std::vector<uint8_t>& out) override {
    out = moveData;
    return true;
  }
  bool readFenAt(size_t offset, std::string& fen) override {
    for (auto& entry : fenEntries) {
      if (entry.first == offset) {
        fen = entry.second;
        return true;
      }
    }
    return false;
  }
  bool deleteGame(int id) override {
    deleteCount++;
    return true;
  }
  void enforceStorageLimits() override {}
};

// ---------------------------------------------------------------------------
// MockGameObserver — captures notifications
// ---------------------------------------------------------------------------

class MockGameObserver : public IGameObserver {
 public:
  int callCount = 0;
  std::string lastFen;
  int lastEval = 0;

  void onBoardStateChanged(const std::string& fen, int evaluation) override {
    callCount++;
    lastFen = fen;
    lastEval = evaluation;
  }
};

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

static MockLogger logger;
static MockGameStorage storage;
static History history(&storage, &logger);

// Test meta byte convention: meta[0]=mode, meta[1]=engineId, meta[2]=difficulty
static constexpr uint8_t MODE_PLAYER  = 1;
static constexpr uint8_t MODE_BOT     = 2;
static constexpr uint8_t MODE_LICHESS = 3;

// Helper: build a GameHeader for testing
static GameHeader makeTestHeader(char playerColor = '?',
                                 const uint8_t* meta = nullptr) {
  GameHeader h;
  memset(&h, 0, sizeof(h));
  h.result = GameResult::IN_PROGRESS;
  h.winnerColor = '?';
  h.playerColor = playerColor;
  if (meta) memcpy(h.meta, meta, GAME_META_SIZE);
  return h;
}

static void setupRecorder() {
  logger = MockLogger();
  storage = MockGameStorage();
  history = History(&storage, &logger);
}

// ---------------------------------------------------------------------------
// History recording tests
// ---------------------------------------------------------------------------

void test_recorder_set_header(void) {
  setupRecorder();
  const uint8_t meta[] = { MODE_PLAYER, 0, 0 };
  history.setHeader(makeTestHeader('?', meta));
  TEST_ASSERT_TRUE(history.isRecording());
  TEST_ASSERT_TRUE(storage.gameActive);
  TEST_ASSERT_EQUAL_UINT8(MODE_PLAYER, storage.storedHeader.meta[0]);
}

void test_recorder_add_move_persists(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());
  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));  // e2e4
  TEST_ASSERT_EQUAL(2, (int)storage.moveData.size());  // 2-byte encoded move
  // Header is flushed after every move so any reboot point can resume the
  // latest side to move.
  TEST_ASSERT_EQUAL(1, storage.headerUpdateCount);
  TEST_ASSERT_EQUAL_UINT16(1, storage.storedHeader.moveCount);
}

void test_recorder_snapshot_position(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());
  std::string fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
  history.snapshotPosition(fen);
  TEST_ASSERT_EQUAL(1, (int)storage.fenEntries.size());
  TEST_ASSERT_EQUAL_STRING(fen.c_str(), storage.fenEntries[0].second.c_str());
  TEST_ASSERT_EQUAL_UINT8(1, storage.storedHeader.fenEntryCnt);
}

void test_recorder_save(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());
  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  history.save(GameResult::CHECKMATE, 'w');
  TEST_ASSERT_FALSE(history.isRecording());
  TEST_ASSERT_TRUE(storage.gameFinalized);
  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, storage.storedHeader.result);
  TEST_ASSERT_EQUAL_CHAR('w', storage.storedHeader.winnerColor);
}

void test_recorder_discard(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());
  history.discard();
  TEST_ASSERT_FALSE(history.isRecording());
  TEST_ASSERT_TRUE(storage.gameDiscarded);
}

void test_recorder_not_recording_noop(void) {
  setupRecorder();
  // addMove without setHeader should not persist
  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  TEST_ASSERT_EQUAL(0, (int)storage.moveData.size());
  // snapshotPosition without setHeader should not persist
  history.snapshotPosition("some fen");
  TEST_ASSERT_EQUAL(0, (int)storage.fenEntries.size());
  // But the move IS in the in-memory log
  TEST_ASSERT_EQUAL(1, history.moveCount());
}

void test_recorder_has_active_game(void) {
  setupRecorder();
  TEST_ASSERT_FALSE(history.hasActiveGame());
  const uint8_t meta[] = { MODE_BOT, 0, 5 };
  history.setHeader(makeTestHeader('w', meta));
  TEST_ASSERT_TRUE(history.hasActiveGame());
}

void test_recorder_get_active_game_info(void) {
  setupRecorder();
  storage.gameActive = true;
  storage.storedHeader.result = GameResult::IN_PROGRESS;
  storage.storedHeader.winnerColor = '?';
  storage.storedHeader.playerColor = 'b';
  storage.storedHeader.meta[0] = MODE_BOT;
  storage.storedHeader.meta[1] = 0;
  storage.storedHeader.meta[2] = 3;

  History histWithLive(&storage, &logger);
  uint8_t color;
  uint8_t metaBuf[GAME_META_SIZE];
  TEST_ASSERT_TRUE(histWithLive.getActiveGameInfo(color, metaBuf));
  TEST_ASSERT_EQUAL_CHAR('b', (char)color);
  TEST_ASSERT_EQUAL_UINT8(MODE_BOT, metaBuf[0]);
  TEST_ASSERT_EQUAL_UINT8(3, metaBuf[2]);
}

void test_recorder_replay_into_board(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());

  // Record initial FEN
  std::string initialFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  history.snapshotPosition(initialFen);
  // Record e2e4
  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  // Record d7d5
  history.addMove(makeEntry(1, 3, 3, 3, Piece::B_PAWN));

  // Now replay into a fresh Position
  Position board;
  board.newGame();
  bool ok = history.replayInto(board);
  TEST_ASSERT_TRUE(ok);

  // Board should have e4 and d5 played
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, board.getSquare(squareOf(4, 4)));
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, board.getSquare(squareOf(3, 3)));
  TEST_ASSERT_ENUM_EQ(Color::WHITE, board.sideToMove());
}

void test_recorder_flushes_header_every_move(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());
  int initial = storage.headerUpdateCount;

  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));  // e2e4 (1st half-move)
  TEST_ASSERT_EQUAL(initial + 1, storage.headerUpdateCount);
  TEST_ASSERT_EQUAL_UINT16(1, storage.storedHeader.moveCount);

  history.addMove(makeEntry(1, 4, 3, 4, Piece::B_PAWN));  // e7e5 (2nd half-move)
  TEST_ASSERT_EQUAL(initial + 2, storage.headerUpdateCount);
  TEST_ASSERT_EQUAL_UINT16(2, storage.storedHeader.moveCount);

  history.addMove(makeEntry(6, 3, 4, 3, Piece::W_PAWN));  // d2d4 (3rd half-move)
  TEST_ASSERT_EQUAL(initial + 3, storage.headerUpdateCount);
  TEST_ASSERT_EQUAL_UINT16(3, storage.storedHeader.moveCount);

  history.addMove(makeEntry(1, 3, 3, 3, Piece::B_PAWN));  // d7d5 (4th half-move)
  TEST_ASSERT_EQUAL(initial + 4, storage.headerUpdateCount);
  TEST_ASSERT_EQUAL_UINT16(4, storage.storedHeader.moveCount);
}

void test_recorder_snapshot_always_flushes_header(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());
  int initial = storage.headerUpdateCount;

  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  TEST_ASSERT_EQUAL(initial + 1, storage.headerUpdateCount);

  history.snapshotPosition("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
  TEST_ASSERT_EQUAL(initial + 2, storage.headerUpdateCount);  // FEN always flushes
}

void test_recorder_replay_rejects_invalid_move(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());

  // Record initial FEN
  std::string initialFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  history.snapshotPosition(initialFen);

  // Record valid e2e4
  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));

  // Inject an illegal move directly into storage (d2d4 — but it's black's turn)
  uint16_t illegal = History::encodeMove(squareOf(6, 3), squareOf(4, 3), ' ');
  storage.moveData.push_back(illegal & 0xFF);
  storage.moveData.push_back((illegal >> 8) & 0xFF);

  Position board;
  board.newGame();
  bool ok = history.replayInto(board);
  TEST_ASSERT_FALSE(ok);  // Replay should fail on the illegal move
}

void test_recorder_set_header_lichess_mode(void) {
  setupRecorder();
  const uint8_t meta[] = { MODE_LICHESS, 0, 0 };
  history.setHeader(makeTestHeader('w', meta));
  TEST_ASSERT_TRUE(history.isRecording());
  TEST_ASSERT_EQUAL_UINT8(MODE_LICHESS, storage.storedHeader.meta[0]);
}

void test_recorder_set_header_while_active(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());
  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));  // record a move
  TEST_ASSERT_TRUE(history.isRecording());

  // Set header again — previous game should be discarded
  const uint8_t meta[] = { MODE_BOT, 0, 5 };
  history.setHeader(makeTestHeader('w', meta));
  TEST_ASSERT_TRUE(history.isRecording());
  TEST_ASSERT_EQUAL_UINT8(MODE_BOT, storage.storedHeader.meta[0]);
  TEST_ASSERT_EQUAL_UINT8(5, storage.storedHeader.meta[2]);
  // Move data should be cleared (new game)
  TEST_ASSERT_EQUAL(0, (int)storage.moveData.size());
}

void test_recorder_save_not_recording(void) {
  setupRecorder();
  // save when not recording should be a safe no-op
  history.save(GameResult::CHECKMATE, 'w');
  TEST_ASSERT_FALSE(history.isRecording());
  TEST_ASSERT_FALSE(storage.gameFinalized);
}

void test_recorder_discard_not_recording(void) {
  setupRecorder();
  // discard when not recording should not crash
  history.discard();
  TEST_ASSERT_FALSE(history.isRecording());
}

void test_recorder_replay_no_fen_entry(void) {
  setupRecorder();
  storage.gameActive = true;
  storage.storedHeader.fenEntryCnt = 0;

  Position board;
  board.newGame();
  bool ok = history.replayInto(board);
  TEST_ASSERT_FALSE(ok);
}

void test_recorder_replay_null_storage(void) {
  // History with null storage — replayInto should return false
  History nullHist(nullptr, &logger);
  Position board;
  board.newGame();
  bool ok = nullHist.replayInto(board);
  TEST_ASSERT_FALSE(ok);
}

void test_recorder_replay_empty_after_fen(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());

  // Record only an initial FEN and no moves
  std::string initialFen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
  history.snapshotPosition(initialFen);

  // Replay — should load FEN with no moves to replay
  Position board;
  board.newGame();
  bool ok = history.replayInto(board);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, board.sideToMove());
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, board.getSquare(squareOf(4, 4)));  // e4 has white pawn
}

void test_recorder_replay_with_promotion(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());

  // Record a position where white can promote
  std::string fen = "8/4P3/8/8/8/8/8/4K2k w - - 0 1";
  history.snapshotPosition(fen);
  history.addMove(makeEntry(1, 4, 0, 4, Piece::W_PAWN, Piece::NONE, Piece::B_QUEEN));  // e7-e8=Q

  // Replay and verify promotion
  Position board;
  board.newGame();
  bool ok = history.replayInto(board);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, board.getSquare(squareOf(0, 4)));  // Queen on e8
}

void test_recorder_branch_truncates_storage(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());
  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));  // move 0: 2 bytes
  history.addMove(makeEntry(1, 4, 3, 4, Piece::B_PAWN));  // move 1: 4 bytes total

  // Undo move 1 and branch
  history.undoMove();
  history.addMove(makeEntry(1, 3, 3, 3, Piece::B_PAWN));  // replaces move 1

  TEST_ASSERT_EQUAL(1, storage.truncateCount);
  TEST_ASSERT_EQUAL(2u, storage.lastTruncateOffset);  // truncate to 1 move * 2 bytes
}

void test_recorder_branch_at_start_truncates_all(void) {
  setupRecorder();
  history.setHeader(makeTestHeader());
  history.addMove(makeEntry(6, 4, 4, 4, Piece::W_PAWN));
  history.addMove(makeEntry(1, 4, 3, 4, Piece::B_PAWN));

  // Undo both moves
  history.undoMove();
  history.undoMove();
  // Branch: add a totally new first move
  history.addMove(makeEntry(6, 3, 4, 3, Piece::W_PAWN));  // d2d4

  TEST_ASSERT_EQUAL(1, storage.truncateCount);
  TEST_ASSERT_EQUAL(0u, storage.lastTruncateOffset);
  TEST_ASSERT_EQUAL(1, history.moveCount());
}

// ---------------------------------------------------------------------------
// Game recording integration tests
// ---------------------------------------------------------------------------

static MockGameObserver observer;
static Game* game = nullptr;

static void setupGame() {
  logger = MockLogger();
  storage = MockGameStorage();
  observer = MockGameObserver();
  delete game;
  game = new Game(&storage, &observer, &logger);
}

static void teardownGame() {
  delete game;
  game = nullptr;
}

void test_game_new_game(void) {
  setupGame();
  game->newGame();
  TEST_ASSERT_ENUM_EQ(Color::WHITE, game->sideToMove());
  TEST_ASSERT_FALSE(game->isGameOver());
  // Observer should have been notified
  TEST_ASSERT_TRUE(observer.callCount > 0);
  teardownGame();
}

void test_game_make_move(void) {
  setupGame();
  game->startNewGame();
  observer.callCount = 0;

  MoveResult r = game->makeMove(6, 4, 4, 4);  // e2e4
  // startNewGame records initial FEN (FEN_MARKER 2 bytes) then addMove encodes (2 bytes) = 4
  TEST_ASSERT_EQUAL(4, (int)storage.moveData.size());
  TEST_ASSERT_ENUM_EQ(Color::BLACK, game->sideToMove());

  // Observer notified
  TEST_ASSERT_TRUE(observer.callCount > 0);

  // snapshotPosition flushed the header with moveCount=1 (FEN_MARKER).
  // persistMove flushes every move, so storedHeader is now at 2.
  TEST_ASSERT_EQUAL_UINT16(2, storage.storedHeader.moveCount);
  teardownGame();
}

void test_game_make_move_auto_finish(void) {
  setupGame();
  game->startNewGame();

  // Scholar's mate sequence
  game->makeMove(6, 4, 4, 4);  // e2-e4
  game->makeMove(1, 4, 3, 4);  // e7-e5
  game->makeMove(7, 5, 4, 2);  // Bf1-c4
  game->makeMove(1, 0, 2, 0);  // a7-a6
  game->makeMove(7, 3, 3, 7);  // Qd1-h5
  game->makeMove(1, 1, 2, 1);  // b7-b6
  MoveResult r = game->makeMove(3, 7, 1, 5);  // Qh5xf7 checkmate

  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, r.gameResult);
  TEST_ASSERT_TRUE(game->isGameOver());

  // Recording should have been auto-finalized
  TEST_ASSERT_TRUE(storage.gameFinalized);
  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, storage.storedHeader.result);
  teardownGame();
}

void test_game_end_game(void) {
  setupGame();
  game->startNewGame();

  game->endGame(GameResult::RESIGNATION, 'b');
  TEST_ASSERT_TRUE(game->isGameOver());

  // Recording finalized
  TEST_ASSERT_TRUE(storage.gameFinalized);
  TEST_ASSERT_ENUM_EQ(GameResult::RESIGNATION, storage.storedHeader.result);
  TEST_ASSERT_EQUAL_CHAR('b', storage.storedHeader.winnerColor);
  teardownGame();
}

void test_game_load_fen(void) {
  setupGame();
  game->startNewGame();
  observer.callCount = 0;

  std::string fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
  bool ok = game->loadFEN(fen);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, game->sideToMove());

  // Observer notified
  TEST_ASSERT_TRUE(observer.callCount > 0);

  // FEN recorded (1 from startNewGame + 1 from loadFEN)
  TEST_ASSERT_EQUAL(2, (int)storage.fenEntries.size());
  teardownGame();
}

void test_game_no_recorder(void) {
  // Game with nullptr storage — mutations still work, no recording
  observer = MockGameObserver();
  Game noRec(nullptr, &observer);
  noRec.newGame();
  MoveResult r = noRec.makeMove(6, 4, 4, 4);
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(observer.callCount > 0);
}

void test_game_no_observer(void) {
  // Game with nullptr observer — mutations still work, no notification
  logger = MockLogger();
  storage = MockGameStorage();
  Game noObs(&storage, nullptr, &logger);
  noObs.startNewGame();
  MoveResult r = noObs.makeMove(6, 4, 4, 4);
  TEST_ASSERT_TRUE(r.valid());
  // startNewGame FEN marker (2 bytes) + addMove (2 bytes) = 4
  TEST_ASSERT_EQUAL(4, (int)storage.moveData.size());
}

void test_game_discard_recording(void) {
  setupGame();
  game->startNewGame();
  game->makeMove(6, 4, 4, 4);

  game->discardRecording();
  TEST_ASSERT_TRUE(storage.gameDiscarded);
  teardownGame();
}

void test_game_pass_throughs(void) {
  setupGame();
  game->newGame();

  // Verify pass-through methods return correct values
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, game->getSquare(7, 0));
  TEST_ASSERT_ENUM_EQ(Color::WHITE, game->sideToMove());
  TEST_ASSERT_FALSE(game->isGameOver());
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, game->gameResult());

  std::string fen = game->getFen();
  TEST_ASSERT_EQUAL_STRING(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      fen.c_str());
  teardownGame();
}

void test_game_end_game_idempotent(void) {
  setupGame();
  game->startNewGame();

  game->endGame(GameResult::RESIGNATION, 'b');
  TEST_ASSERT_TRUE(game->isGameOver());

  observer.callCount = 0;

  // Second endGame should be a no-op (guard prevents overwrite)
  game->endGame(GameResult::TIMEOUT, 'w');
  TEST_ASSERT_EQUAL(0, observer.callCount);
  TEST_ASSERT_ENUM_EQ(GameResult::RESIGNATION, game->gameResult());
  TEST_ASSERT_EQUAL_CHAR('b', game->winnerColor());
  teardownGame();
}

void test_game_start_new_game(void) {
  setupGame();
  const uint8_t meta[] = { MODE_BOT, 0, 5 };
  game->startNewGame('w', meta);

  TEST_ASSERT_ENUM_EQ(Color::WHITE, game->sideToMove());
  TEST_ASSERT_FALSE(game->isGameOver());

  // Should have started recording and initialized board
  TEST_ASSERT_TRUE(storage.gameActive);
  TEST_ASSERT_EQUAL_UINT8(MODE_BOT, storage.storedHeader.meta[0]);
  TEST_ASSERT_EQUAL_CHAR('w', storage.storedHeader.playerColor);
  TEST_ASSERT_EQUAL_UINT8(5, storage.storedHeader.meta[2]);

  // Initial FEN should have been recorded
  TEST_ASSERT_EQUAL(1, (int)storage.fenEntries.size());
  teardownGame();
}

void test_game_resume_game(void) {
  setupGame();
  game->startNewGame();
  game->makeMove(6, 4, 4, 4);  // e2-e4
  game->makeMove(1, 4, 3, 4);  // e7-e5

  // Now create a new game and resume from storage
  observer = MockGameObserver();
  Game* game2 = new Game(&storage, &observer, &logger);
  bool ok = game2->resumeGame();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_ENUM_EQ(Color::WHITE, game2->sideToMove());
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, game2->getSquare(4, 4));  // e4
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, game2->getSquare(3, 4));  // e5
  TEST_ASSERT_TRUE(observer.callCount > 0);  // Observer notified
  delete game2;
  teardownGame();
}

void test_game_resume_after_single_move_preserves_black_turn(void) {
  setupGame();
  game->startNewGame();
  game->makeMove(6, 4, 4, 4);  // e2-e4

  // A reboot after white's first move must resume with black to play.
  observer = MockGameObserver();
  Game* game2 = new Game(&storage, &observer, &logger);
  bool ok = game2->resumeGame();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, game2->sideToMove());
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, game2->getSquare(4, 4));  // e4
  TEST_ASSERT_ENUM_EQ(Piece::NONE, game2->getSquare(6, 4));    // e2
  TEST_ASSERT_TRUE(observer.callCount > 0);  // Observer notified
  delete game2;
  teardownGame();
}

void test_game_resume_after_single_move_ignores_partial_trailing_byte(void) {
  setupGame();
  game->startNewGame();
  game->makeMove(6, 4, 4, 4);  // e2-e4

  storage.moveData.push_back(0x7F);  // Simulate an interrupted 2-byte append.

  observer = MockGameObserver();
  Game* game2 = new Game(&storage, &observer, &logger);
  bool ok = game2->resumeGame();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, game2->sideToMove());
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, game2->getSquare(4, 4));  // e4
  delete game2;
  teardownGame();
}

void test_game_resume_after_odd_halfmove_preserves_black_turn(void) {
  setupGame();
  game->startNewGame();
  game->makeMove(6, 4, 4, 4);  // e2-e4
  game->makeMove(1, 4, 3, 4);  // e7-e5
  game->makeMove(6, 3, 4, 3);  // d2-d4
  TEST_ASSERT_EQUAL_UINT16(4, storage.storedHeader.moveCount);  // FEN marker + 3 moves

  observer = MockGameObserver();
  Game* game2 = new Game(&storage, &observer, &logger);
  bool ok = game2->resumeGame();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, game2->sideToMove());
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, game2->getSquare(4, 3));  // d4
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, game2->getSquare(3, 4));  // e5
  delete game2;
  teardownGame();
}

void test_game_resume_invalidates_cached_fen(void) {
  setupGame();
  game->startNewGame();
  game->makeMove(6, 4, 4, 4);  // e2-e4

  observer = MockGameObserver();
  Game* game2 = new Game(&storage, &observer, &logger);
  game2->newGame();
  std::string initialFen = game2->getFen();
  TEST_ASSERT_TRUE(initialFen.find(" w ") != std::string::npos);

  bool ok = game2->resumeGame();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, game2->sideToMove());
  TEST_ASSERT_TRUE(observer.lastFen.find(" b ") != std::string::npos);
  TEST_ASSERT_TRUE(observer.lastFen.find("4P3") != std::string::npos);
  delete game2;
  teardownGame();
}

void test_game_resume_finished_game(void) {
  setupGame();
  game->startNewGame();

  // Play Scholar's mate to completion
  game->makeMove(6, 4, 4, 4);  // e2-e4
  game->makeMove(1, 4, 3, 4);  // e7-e5
  game->makeMove(7, 5, 4, 2);  // Bf1-c4
  game->makeMove(1, 0, 2, 0);  // a7-a6
  game->makeMove(7, 3, 3, 7);  // Qd1-h5
  game->makeMove(1, 1, 2, 1);  // b7-b6
  game->makeMove(3, 7, 1, 5);  // Qh5xf7#
  TEST_ASSERT_TRUE(game->isGameOver());
  TEST_ASSERT_TRUE(storage.gameFinalized);

  // Resume from storage — game-over state should be restored
  observer = MockGameObserver();
  Game* game2 = new Game(&storage, &observer, &logger);
  bool ok = game2->resumeGame();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(game2->isGameOver());
  TEST_ASSERT_EQUAL(GameResult::CHECKMATE, game2->gameResult());
  TEST_ASSERT_EQUAL('w', game2->winnerColor());
  delete game2;
  teardownGame();
}

void test_game_make_move_records_promotion(void) {
  setupGame();
  game->startNewGame();
  game->loadFEN("8/4P3/8/8/8/8/8/4K2k w - - 0 1");

  MoveResult r = game->makeMove(1, 4, 0, 4);  // e7-e8=Q (auto-queen)
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isPromotion());
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, r.promotedTo);

  // Verify the correct promotion type was recorded (compact encoding stores lowercase)
  // moveData: FEN_MARKER(2) + FEN_MARKER(2, from loadFEN) + move(2) = 6 bytes
  TEST_ASSERT_TRUE(storage.moveData.size() >= 6);
  uint16_t lastEntry;
  memcpy(&lastEntry, &storage.moveData[storage.moveData.size() - 2], 2);
  Square decFrom, decTo;
  char promo;
  History::decodeMove(lastEntry, decFrom, decTo, promo);
  TEST_ASSERT_EQUAL_CHAR('q', promo);  // compact encoding normalizes to lowercase
  teardownGame();
}

void test_game_undo_redo(void) {
  setupGame();
  game->startNewGame();
  game->makeMove(6, 4, 4, 4);  // e2-e4
  game->makeMove(1, 4, 3, 4);  // e7-e5

  TEST_ASSERT_TRUE(game->canUndo());
  TEST_ASSERT_FALSE(game->canRedo());

  // Undo e7-e5
  TEST_ASSERT_TRUE(game->undoMove());
  TEST_ASSERT_ENUM_EQ(Color::BLACK, game->sideToMove());  // black to move again
  TEST_ASSERT_ENUM_EQ(Piece::NONE, game->getSquare(3, 4));  // e5 empty
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, game->getSquare(1, 4));  // pawn back on e7

  TEST_ASSERT_TRUE(game->canUndo());
  TEST_ASSERT_TRUE(game->canRedo());

  // Redo e7-e5
  TEST_ASSERT_TRUE(game->redoMove());
  TEST_ASSERT_ENUM_EQ(Color::WHITE, game->sideToMove());
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, game->getSquare(3, 4));  // e5 has pawn again

  TEST_ASSERT_FALSE(game->canRedo());
  teardownGame();
}

void test_game_undo_at_start(void) {
  setupGame();
  game->startNewGame();
  TEST_ASSERT_FALSE(game->canUndo());
  TEST_ASSERT_FALSE(game->undoMove());
  teardownGame();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Compact move encoding (relocated from test_codec)
// ---------------------------------------------------------------------------

void test_encodeMove_rook_promotion(void) {
  Square from = squareOf(1, 4), to = squareOf(0, 4);
  uint16_t encoded = History::encodeMove(from, to, 'r');
  Square decFrom, decTo;
  char promo;
  History::decodeMove(encoded, decFrom, decTo, promo);
  TEST_ASSERT_EQUAL_INT(from, decFrom);
  TEST_ASSERT_EQUAL_INT(to, decTo);
  TEST_ASSERT_EQUAL_CHAR('r', promo);
}

void test_encodeMove_bishop_promotion(void) {
  Square from = squareOf(1, 4), to = squareOf(0, 4);
  uint16_t encoded = History::encodeMove(from, to, 'b');
  Square decFrom, decTo;
  char promo;
  History::decodeMove(encoded, decFrom, decTo, promo);
  TEST_ASSERT_EQUAL_INT(from, decFrom);
  TEST_ASSERT_EQUAL_INT(to, decTo);
  TEST_ASSERT_EQUAL_CHAR('b', promo);
}

// ---------------------------------------------------------------------------
// On-disk format stability (relocated from test_codec)
// ---------------------------------------------------------------------------

void test_game_result_pinned_values(void) {
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(GameResult::IN_PROGRESS));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(GameResult::CHECKMATE));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(GameResult::STALEMATE));
  TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(GameResult::DRAW_50));
  TEST_ASSERT_EQUAL_UINT8(4, static_cast<uint8_t>(GameResult::DRAW_3FOLD));
  TEST_ASSERT_EQUAL_UINT8(5, static_cast<uint8_t>(GameResult::RESIGNATION));
  TEST_ASSERT_EQUAL_UINT8(6, static_cast<uint8_t>(GameResult::DRAW_INSUFFICIENT));
  TEST_ASSERT_EQUAL_UINT8(7, static_cast<uint8_t>(GameResult::DRAW_AGREEMENT));
  TEST_ASSERT_EQUAL_UINT8(8, static_cast<uint8_t>(GameResult::TIMEOUT));
  TEST_ASSERT_EQUAL_UINT8(9, static_cast<uint8_t>(GameResult::ABORTED));
}

void test_fen_marker_no_collision(void) {
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, FEN_MARKER);
  uint16_t maxEncoded = History::encodeMove(squareOf(7, 7), squareOf(7, 7), 'n');
  TEST_ASSERT_TRUE(maxEncoded < FEN_MARKER);
}

void test_game_header_size(void) {
  TEST_ASSERT_EQUAL(16, (int)sizeof(GameHeader));
}

void register_history_persistence_tests() {
  needsDefaultKings = false;

  // History recording
  RUN_TEST(test_recorder_set_header);
  RUN_TEST(test_recorder_add_move_persists);
  RUN_TEST(test_recorder_snapshot_position);
  RUN_TEST(test_recorder_save);
  RUN_TEST(test_recorder_discard);
  RUN_TEST(test_recorder_not_recording_noop);
  RUN_TEST(test_recorder_has_active_game);
  RUN_TEST(test_recorder_get_active_game_info);
  RUN_TEST(test_recorder_replay_into_board);
  RUN_TEST(test_recorder_flushes_header_every_move);
  RUN_TEST(test_recorder_snapshot_always_flushes_header);
  RUN_TEST(test_recorder_replay_rejects_invalid_move);
  RUN_TEST(test_recorder_set_header_lichess_mode);
  RUN_TEST(test_recorder_set_header_while_active);
  RUN_TEST(test_recorder_save_not_recording);
  RUN_TEST(test_recorder_discard_not_recording);
  RUN_TEST(test_recorder_replay_no_fen_entry);
  RUN_TEST(test_recorder_replay_null_storage);
  RUN_TEST(test_recorder_replay_empty_after_fen);
  RUN_TEST(test_recorder_replay_with_promotion);
  RUN_TEST(test_recorder_branch_truncates_storage);
  RUN_TEST(test_recorder_branch_at_start_truncates_all);

  // Game recording integration
  RUN_TEST(test_game_new_game);
  RUN_TEST(test_game_make_move);
  RUN_TEST(test_game_make_move_auto_finish);
  RUN_TEST(test_game_end_game);
  RUN_TEST(test_game_load_fen);
  RUN_TEST(test_game_no_recorder);
  RUN_TEST(test_game_no_observer);
  RUN_TEST(test_game_discard_recording);
  RUN_TEST(test_game_pass_throughs);
  RUN_TEST(test_game_end_game_idempotent);
  RUN_TEST(test_game_start_new_game);
  RUN_TEST(test_game_resume_game);
  RUN_TEST(test_game_resume_after_single_move_preserves_black_turn);
  RUN_TEST(test_game_resume_after_single_move_ignores_partial_trailing_byte);
  RUN_TEST(test_game_resume_after_odd_halfmove_preserves_black_turn);
  RUN_TEST(test_game_resume_invalidates_cached_fen);
  RUN_TEST(test_game_resume_finished_game);
  RUN_TEST(test_game_make_move_records_promotion);
  RUN_TEST(test_game_undo_redo);
  RUN_TEST(test_game_undo_at_start);

  // Compact move encoding
  RUN_TEST(test_encodeMove_rook_promotion);
  RUN_TEST(test_encodeMove_bishop_promotion);

  // On-disk format stability
  RUN_TEST(test_game_result_pinned_values);
  RUN_TEST(test_fen_marker_no_collision);
  RUN_TEST(test_game_header_size);
}
