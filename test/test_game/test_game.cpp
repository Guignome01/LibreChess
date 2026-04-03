#include <unity.h>

#include "../test_helpers.h"
#include <game.h>
#include <evaluation.h>
#include <observer.h>
#include <types.h>

static Game game;

// Reset Game before every test
static void setUpGame(void) {
  game = Game();
  game.newGame();
}

// ---------------------------------------------------------------------------
// Threefold repetition (detected via Position::isGameOver)
// ---------------------------------------------------------------------------

void test_game_threefold_repetition(void) {
  setUpGame();
  // Position where Ke1 and Ke8 shuffle back and forth (pawns provide sufficient material)
  game.loadFEN("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1");
  // Move 1: Ke1-d1, Ke8-d8
  game.makeMove(7, 4, 7, 3); // Ke1-d1
  game.makeMove(0, 4, 0, 3); // Ke8-d8
  // Move 2: Kd1-e1, Kd8-e8 (back to original — occurrence 2)
  game.makeMove(7, 3, 7, 4); // Kd1-e1
  game.makeMove(0, 3, 0, 4); // Kd8-e8
  // Move 3: Ke1-d1, Ke8-d8
  game.makeMove(7, 4, 7, 3); // Ke1-d1
  game.makeMove(0, 4, 0, 3); // Ke8-d8
  // Move 4: Kd1-e1, Kd8-e8 (back to original — occurrence 3)
  game.makeMove(7, 3, 7, 4); // Kd1-e1
  MoveResult r = game.makeMove(0, 3, 0, 4); // Kd8-e8 — third repetition
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_3FOLD, r.gameResult);
  TEST_ASSERT_TRUE(game.isGameOver());
}

void test_game_threefold_different_castling_rights(void) {
  setUpGame();
  // Same as threefold test but with castling rights.
  // King move on first half-move loses castling → initial position hash
  // differs from subsequent "same board" hashes, so 8 half-moves is NOT
  // enough for threefold (only 2 occurrences of castling-less position).
  game.loadFEN("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
  game.makeMove(7, 4, 7, 3); // Ke1-d1 (loses K castling)
  game.makeMove(0, 4, 0, 3); // Ke8-d8
  game.makeMove(7, 3, 7, 4); // Kd1-e1
  game.makeMove(0, 3, 0, 4); // Kd8-e8 — board same as start, but castling=0
  game.makeMove(7, 4, 7, 3); // Ke1-d1
  game.makeMove(0, 4, 0, 3); // Ke8-d8
  game.makeMove(7, 3, 7, 4); // Kd1-e1
  MoveResult r = game.makeMove(0, 3, 0, 4); // Kd8-e8 — occurrence 2 (not 3)
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_FALSE(game.isGameOver());
}

// ---------------------------------------------------------------------------
// Observer notification / batching
// ---------------------------------------------------------------------------

// Local mock observer
class TestObserver : public IGameObserver {
 public:
  int callCount = 0;
  void onBoardStateChanged(const std::string&, int) override { callCount++; }
};

void test_game_observer_fires_on_move(void) {
  TestObserver obs;
  Game g(nullptr, &obs);
  g.newGame();
  obs.callCount = 0;
  g.makeMove(6, 4, 4, 4); // e2e4
  TEST_ASSERT_EQUAL_INT(1, obs.callCount);
}

void test_game_observer_fires_on_new_game(void) {
  TestObserver obs;
  Game g(nullptr, &obs);
  g.newGame();
  obs.callCount = 0;
  g.newGame();
  TEST_ASSERT_EQUAL_INT(1, obs.callCount);
}

void test_game_batch_suppresses_observer(void) {
  TestObserver obs;
  Game g(nullptr, &obs);
  g.newGame();
  obs.callCount = 0;

  g.beginBatch();
  g.makeMove(6, 4, 4, 4);  // e2e4
  g.makeMove(1, 4, 3, 4);  // e5
  TEST_ASSERT_EQUAL_INT(0, obs.callCount);
  g.endBatch();
  TEST_ASSERT_EQUAL_INT(1, obs.callCount);  // single coalesced notification
}

void test_game_no_observer_no_crash(void) {
  setUpGame();
  // No observer set — ensure no crash
  game.makeMove(6, 4, 4, 4); // should not crash
  TEST_ASSERT_TRUE(true);
}

void test_game_nested_batch(void) {
  TestObserver obs;
  Game g(nullptr, &obs);
  g.newGame();
  obs.callCount = 0;
  g.beginBatch();
  g.beginBatch();  // depth 2
  g.makeMove(6, 4, 4, 4);
  g.endBatch();    // depth 1 — observer still suppressed
  TEST_ASSERT_EQUAL_INT(0, obs.callCount);
  g.endBatch();    // depth 0 — fires coalesced notification
  TEST_ASSERT_EQUAL_INT(1, obs.callCount);
}

void test_game_end_batch_without_begin(void) {
  TestObserver obs;
  Game g(nullptr, &obs);
  g.newGame();
  obs.callCount = 0;
  // endBatch with no beginBatch should not crash or fire observer
  g.endBatch();
  TEST_ASSERT_EQUAL_INT(0, obs.callCount);
}

// ---------------------------------------------------------------------------
// History integration (Game owns history, records moves via makeMove)
// ---------------------------------------------------------------------------

void test_game_history_records_moves(void) {
  setUpGame();
  TEST_ASSERT_TRUE(game.history().empty());
  game.makeMove(6, 4, 4, 4);  // e4
  TEST_ASSERT_EQUAL(1, game.history().moveCount());
  game.makeMove(1, 4, 3, 4);  // e5
  TEST_ASSERT_EQUAL(2, game.history().moveCount());
}

void test_game_history_correct_fields(void) {
  setUpGame();
  game.makeMove(6, 4, 4, 4);  // e4

  const MoveEntry& m = game.history().getMove(0);
  TEST_ASSERT_EQUAL(6, m.fromRow);
  TEST_ASSERT_EQUAL(4, m.fromCol);
  TEST_ASSERT_EQUAL(4, m.toRow);
  TEST_ASSERT_EQUAL(4, m.toCol);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, m.piece);
  TEST_ASSERT_ENUM_EQ(Piece::NONE, m.captured);
  TEST_ASSERT_FALSE(m.isCapture);
  TEST_ASSERT_FALSE(m.isEnPassant);
  TEST_ASSERT_FALSE(m.isCastling);
  TEST_ASSERT_FALSE(m.isPromotion);
}

void test_game_history_capture_recorded(void) {
  setUpGame();
  // Set up a simple capture: 1. e4 d5 2. exd5
  game.makeMove(6, 4, 4, 4);  // e4
  game.makeMove(1, 3, 3, 3);  // d5
  game.makeMove(4, 4, 3, 3);  // exd5 (capture)

  const MoveEntry& m = game.history().getMove(2);
  TEST_ASSERT_TRUE(m.isCapture);
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, m.captured);  // captured black pawn
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, m.piece);     // white pawn captured
}

void test_game_history_en_passant_recorded(void) {
  setUpGame();
  // Set up en passant: 1. e4 a6 2. e5 d5 3. exd6 (en passant)
  game.makeMove(6, 4, 4, 4);  // e4
  game.makeMove(1, 0, 2, 0);  // a6
  game.makeMove(4, 4, 3, 4);  // e5
  game.makeMove(1, 3, 3, 3);  // d5
  game.makeMove(3, 4, 2, 3);  // exd6 (en passant)

  const MoveEntry& m = game.history().getMove(4);
  TEST_ASSERT_TRUE(m.isEnPassant);
  TEST_ASSERT_TRUE(m.isCapture);
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, m.captured);  // captured black pawn
  TEST_ASSERT_EQUAL(3, m.epCapturedRow);
}

void test_game_history_castling_recorded(void) {
  setUpGame();
  // Direct setup: empty between king and rook
  game.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  game.makeMove(7, 4, 7, 6);  // O-O (white kingside)

  const MoveEntry& m = game.history().lastMove();
  TEST_ASSERT_TRUE(m.isCastling);
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, m.piece);
  TEST_ASSERT_FALSE(m.isCapture);
}

void test_game_history_promotion_recorded(void) {
  setUpGame();
  // White pawn on 7th rank promotes
  game.loadFEN("8/P4k2/8/8/8/8/5K2/8 w - - 0 1");
  game.makeMove(1, 0, 0, 0, 'q');  // a8=Q

  const MoveEntry& m = game.history().lastMove();
  TEST_ASSERT_TRUE(m.isPromotion);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, m.promotion);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, m.piece);
}

void test_game_history_check_recorded(void) {
  setUpGame();
  // Set up a position where a move gives check
  game.loadFEN("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  game.makeMove(6, 4, 1, 4);  // Re8+ (check)

  const MoveEntry& m = game.history().lastMove();
  TEST_ASSERT_TRUE(m.isCheck);
}

void test_game_history_new_game_clears(void) {
  setUpGame();
  game.makeMove(6, 4, 4, 4);
  TEST_ASSERT_EQUAL(1, game.history().moveCount());

  game.newGame();
  TEST_ASSERT_EQUAL(0, game.history().moveCount());
  TEST_ASSERT_TRUE(game.history().empty());
}

void test_game_history_load_fen_clears(void) {
  setUpGame();
  game.makeMove(6, 4, 4, 4);
  TEST_ASSERT_EQUAL(1, game.history().moveCount());

  game.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  TEST_ASSERT_EQUAL(0, game.history().moveCount());
}

void test_game_history_prevstate_saved(void) {
  setUpGame();
  // Before e4: castling=KQkq (0x0F), ep=none, halfmove=0, fullmove=1
  PositionState before = game.positionState();
  game.makeMove(6, 4, 4, 4);  // e4

  const MoveEntry& m = game.history().getMove(0);
  TEST_ASSERT_EQUAL(before.castlingRights, m.prevState.castlingRights);
  TEST_ASSERT_EQUAL(before.halfmoveClock, m.prevState.halfmoveClock);
  TEST_ASSERT_EQUAL(before.fullmoveClock, m.prevState.fullmoveClock);
}

void test_game_history_invalid_move_not_recorded(void) {
  setUpGame();
  // Try an illegal move
  MoveResult r = game.makeMove(6, 4, 3, 4);  // e2-e5 (illegal: 3 squares)
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_EQUAL(0, game.history().moveCount());
}

void test_game_history_last_move_after_sequence(void) {
  setUpGame();
  game.makeMove(6, 4, 4, 4);  // 1. e4
  game.makeMove(1, 4, 3, 4);  // 1... e5
  game.makeMove(7, 6, 5, 5);  // 2. Nf3

  TEST_ASSERT_EQUAL(3, game.history().moveCount());
  const MoveEntry& last = game.history().lastMove();
  TEST_ASSERT_ENUM_EQ(Piece::W_KNIGHT, last.piece);
  TEST_ASSERT_EQUAL(7, last.fromRow);
  TEST_ASSERT_EQUAL(6, last.fromCol);
  TEST_ASSERT_EQUAL(5, last.toRow);
  TEST_ASSERT_EQUAL(5, last.toCol);
}

void test_game_batch_first_last_single_observer(void) {
  TestObserver obs;
  Game g(nullptr, &obs);
  g.newGame();
  g.makeMove(6, 4, 4, 4);  // e2e4
  g.makeMove(1, 4, 3, 4);  // e5
  g.makeMove(7, 6, 5, 5);  // Nf3
  obs.callCount = 0;

  // "first" pattern: undo all inside a batch → one observer call
  g.beginBatch();
  while (g.canUndo()) g.undoMove();
  g.endBatch();
  TEST_ASSERT_EQUAL_INT(1, obs.callCount);
  TEST_ASSERT_EQUAL(0, g.currentMoveIndex());

  obs.callCount = 0;

  // "last" pattern: redo all inside a batch → one observer call
  g.beginBatch();
  while (g.canRedo()) g.redoMove();
  g.endBatch();
  TEST_ASSERT_EQUAL_INT(1, obs.callCount);
  TEST_ASSERT_EQUAL(3, g.currentMoveIndex());
}

// ---------------------------------------------------------------------------
// getHistory (SAN/LAN formats exercise board-replay orchestration logic)
// ---------------------------------------------------------------------------

void test_game_get_history_san_format(void) {
  setUpGame();
  game.makeMove(6, 4, 4, 4);  // 1. e4
  game.makeMove(1, 4, 3, 4);  // 1... e5
  game.makeMove(7, 6, 5, 5);  // 2. Nf3

  std::string moves[10];
  int count = game.getHistory(moves, 10, MoveFormat::SAN);
  TEST_ASSERT_EQUAL(3, count);
  TEST_ASSERT_EQUAL_STRING("e4", moves[0].c_str());
  TEST_ASSERT_EQUAL_STRING("e5", moves[1].c_str());
  TEST_ASSERT_EQUAL_STRING("Nf3", moves[2].c_str());
}

void test_game_get_history_lan_format(void) {
  setUpGame();
  game.makeMove(6, 4, 4, 4);  // 1. e4
  game.makeMove(1, 4, 3, 4);  // 1... e5
  game.makeMove(7, 6, 5, 5);  // 2. Nf3

  std::string moves[10];
  int count = game.getHistory(moves, 10, MoveFormat::LAN);
  TEST_ASSERT_EQUAL(3, count);
  TEST_ASSERT_EQUAL_STRING("e2-e4", moves[0].c_str());
  TEST_ASSERT_EQUAL_STRING("e7-e5", moves[1].c_str());
  TEST_ASSERT_EQUAL_STRING("Ng1-f3", moves[2].c_str());
}

// ---------------------------------------------------------------------------
// Lifecycle (gameOver / endGame owned by Game)
// ---------------------------------------------------------------------------

void test_game_end_game_resignation(void) {
  setUpGame();
  game.endGame(GameResult::RESIGNATION, 'b');
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::RESIGNATION, game.gameResult());
  TEST_ASSERT_EQUAL('b', game.winnerColor());
}

void test_game_end_game_timeout(void) {
  setUpGame();
  game.endGame(GameResult::TIMEOUT, 'w');
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::TIMEOUT, game.gameResult());
  TEST_ASSERT_EQUAL('w', game.winnerColor());
}

void test_game_end_game_aborted(void) {
  setUpGame();
  game.endGame(GameResult::ABORTED, ' ');
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::ABORTED, game.gameResult());
  TEST_ASSERT_EQUAL(' ', game.winnerColor());
}

void test_game_end_game_draw_agreement(void) {
  setUpGame();
  game.endGame(GameResult::DRAW_AGREEMENT, ' ');
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::DRAW_AGREEMENT, game.gameResult());
  TEST_ASSERT_EQUAL(' ', game.winnerColor());
}

void test_game_end_game_double_call(void) {
  setUpGame();
  game.endGame(GameResult::RESIGNATION, 'w');
  game.endGame(GameResult::TIMEOUT, 'b');
  // First call wins
  TEST_ASSERT_EQUAL(GameResult::RESIGNATION, game.gameResult());
  TEST_ASSERT_EQUAL('w', game.winnerColor());
}

void test_game_move_after_game_over_rejected(void) {
  setUpGame();
  game.endGame(GameResult::RESIGNATION, 'b');
  MoveResult r = game.makeMove(6, 4, 4, 4);  // e2e4
  TEST_ASSERT_FALSE(r.valid);
}

void test_game_end_game_preserves_fen(void) {
  setUpGame();
  game.makeMove(6, 4, 4, 4);  // e2e4
  std::string fenBefore = game.getFen();
  game.endGame(GameResult::RESIGNATION, 'b');
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), game.getFen().c_str());
}

void test_game_load_fen_resets_game_over(void) {
  setUpGame();
  game.endGame(GameResult::RESIGNATION, 'b');
  TEST_ASSERT_TRUE(game.isGameOver());
  game.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  TEST_ASSERT_FALSE(game.isGameOver());
}

void test_game_new_game_resets_game_over(void) {
  setUpGame();
  game.endGame(GameResult::RESIGNATION, 'w');
  TEST_ASSERT_TRUE(game.isGameOver());
  game.newGame();
  TEST_ASSERT_FALSE(game.isGameOver());
}

void test_game_checkmate_sets_game_over(void) {
  setUpGame();
  // Scholar's mate
  game.makeMove(6, 4, 4, 4);  // e4
  game.makeMove(1, 4, 3, 4);  // e5
  game.makeMove(7, 5, 4, 2);  // Bc4
  game.makeMove(1, 0, 2, 0);  // a6
  game.makeMove(7, 3, 3, 7);  // Qh5
  game.makeMove(1, 1, 2, 1);  // b6
  MoveResult r = game.makeMove(3, 7, 1, 5);  // Qxf7#
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::CHECKMATE, game.gameResult());
  TEST_ASSERT_EQUAL('w', game.winnerColor());
}

void test_game_stalemate_sets_game_over(void) {
  setUpGame();
  game.loadFEN("k7/8/2K5/8/8/8/8/1Q6 w - - 0 1");
  MoveResult r = game.makeMove(7, 1, 2, 1);  // Qb1-b6 stalemates black
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::STALEMATE, game.gameResult());
  TEST_ASSERT_TRUE(game.isDraw());
}

void test_game_insufficient_material_sets_game_over(void) {
  setUpGame();
  game.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  MoveResult r = game.makeMove(7, 4, 7, 3);  // Ke1-d1
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::DRAW_INSUFFICIENT, game.gameResult());
  TEST_ASSERT_TRUE(game.isDraw());
}

void test_game_fifty_move_sets_game_over(void) {
  setUpGame();
  game.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 99 50");
  MoveResult r = game.makeMove(7, 0, 7, 1);  // Ra1-b1 (clock hits 100)
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::DRAW_50, game.gameResult());
  TEST_ASSERT_TRUE(game.isDraw());
}

void test_game_undo_clears_game_over(void) {
  setUpGame();
  // Scholar's mate
  game.makeMove(6, 4, 4, 4);
  game.makeMove(1, 4, 3, 4);
  game.makeMove(7, 5, 4, 2);
  game.makeMove(1, 0, 2, 0);
  game.makeMove(7, 3, 3, 7);
  game.makeMove(1, 1, 2, 1);
  game.makeMove(3, 7, 1, 5);  // Qxf7#
  TEST_ASSERT_TRUE(game.isGameOver());
  game.undoMove();
  TEST_ASSERT_FALSE(game.isGameOver());
}

void test_game_redo_restores_game_over(void) {
  setUpGame();
  // Scholar's mate
  game.makeMove(6, 4, 4, 4);
  game.makeMove(1, 4, 3, 4);
  game.makeMove(7, 5, 4, 2);
  game.makeMove(1, 0, 2, 0);
  game.makeMove(7, 3, 3, 7);
  game.makeMove(1, 1, 2, 1);
  game.makeMove(3, 7, 1, 5);  // Qxf7#
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::CHECKMATE, game.gameResult());

  game.undoMove();
  TEST_ASSERT_FALSE(game.isGameOver());

  game.redoMove();
  TEST_ASSERT_TRUE(game.isGameOver());
  TEST_ASSERT_EQUAL(GameResult::CHECKMATE, game.gameResult());
  TEST_ASSERT_EQUAL('w', game.winnerColor());
}

void test_game_san_checkmate_suffix(void) {
  setUpGame();
  // Scholar's mate
  game.makeMove(6, 4, 4, 4);  // e4
  game.makeMove(1, 4, 3, 4);  // e5
  game.makeMove(7, 5, 4, 2);  // Bc4
  game.makeMove(1, 0, 2, 0);  // a6
  game.makeMove(7, 3, 3, 7);  // Qh5
  game.makeMove(1, 1, 2, 1);  // b6
  game.makeMove(3, 7, 1, 5);  // Qxf7#

  std::string moves[10];
  int count = game.getHistory(moves, 10, MoveFormat::SAN);
  TEST_ASSERT_EQUAL(7, count);
  TEST_ASSERT_EQUAL_STRING("Qxf7#", moves[6].c_str());
}

void test_game_san_check_suffix(void) {
  setUpGame();
  game.loadFEN("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  game.makeMove(6, 4, 1, 4);  // Re8+

  std::string moves[10];
  int count = game.getHistory(moves, 10, MoveFormat::SAN);
  TEST_ASSERT_EQUAL(1, count);
  TEST_ASSERT_EQUAL_STRING("Re7+", moves[0].c_str());
}

// ---------------------------------------------------------------------------
// makeMove(string) — string-based overload
// ---------------------------------------------------------------------------

void test_game_makeMove_string_coordinate(void) {
  setUpGame();
  MoveResult r = game.makeMove("e2e4");
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, game.getSquare(4, 4));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, game.currentTurn());
}

void test_game_makeMove_string_invalid(void) {
  setUpGame();
  MoveResult r = game.makeMove("xxxx");
  TEST_ASSERT_FALSE(r.valid);
}

void test_game_makeMove_string_empty(void) {
  setUpGame();
  MoveResult r = game.makeMove("");
  TEST_ASSERT_FALSE(r.valid);
}

void test_game_makeMove_string_promotion(void) {
  setUpGame();
  game.loadFEN("8/4P3/8/8/8/8/4p3/4K2k w - - 0 1");
  MoveResult r = game.makeMove("e7e8q");
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isPromotion);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, game.getSquare(0, 4));
}

// ---------------------------------------------------------------------------
// board() / history() accessors
// ---------------------------------------------------------------------------

void test_game_board_accessor(void) {
  setUpGame();
  const Position& b = game.board();
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, b.getSquare(7, 4));
  TEST_ASSERT_ENUM_EQ(Color::WHITE, b.currentTurn());
}

void test_game_history_accessor(void) {
  setUpGame();
  game.makeMove(6, 4, 4, 4);
  game.makeMove(1, 4, 3, 4);
  const History& h = game.history();
  TEST_ASSERT_EQUAL(2, h.moveCount());
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, h.getMove(0).piece);
}

// ---------------------------------------------------------------------------
// FEN / evaluation caching (Game layer)
// ---------------------------------------------------------------------------

void test_game_fen_cache_consistent(void) {
  setUpGame();
  std::string fen1 = game.getFen();
  std::string fen2 = game.getFen();
  TEST_ASSERT_EQUAL_STRING(fen1.c_str(), fen2.c_str());

  game.makeMove(6, 4, 4, 4);  // e2e4
  std::string fen3 = game.getFen();
  TEST_ASSERT_TRUE(fen3 != fen1);
}

void test_game_eval_cache_consistent(void) {
  setUpGame();
  int eval1 = game.getEvaluation();
  int eval2 = game.getEvaluation();
  TEST_ASSERT_EQUAL_INT(eval1, eval2);

  // Load asymmetric position (white missing queen) and verify eval updates
  game.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1");
  int eval3 = game.getEvaluation();
  TEST_ASSERT_TRUE(eval3 < eval1);
}

void test_game_eval_updates_after_capture(void) {
  setUpGame();
  game.loadFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
  int evalBefore = game.getEvaluation();
  game.makeMove(4, 4, 3, 3);  // exd5
  int evalAfter = game.getEvaluation();
  TEST_ASSERT_TRUE(evalAfter > evalBefore);
}

void test_game_fen_updates_after_move(void) {
  setUpGame();
  std::string fenBefore = game.getFen();
  game.makeMove(6, 4, 4, 4);  // e2e4
  std::string fenAfter = game.getFen();
  TEST_ASSERT_FALSE(fenBefore == fenAfter);
  TEST_ASSERT_TRUE(fenAfter.find(" b ") != std::string::npos);
}

void test_game_cache_invalidated_on_undo(void) {
  setUpGame();
  std::string fenBefore = game.getFen();
  game.makeMove(6, 4, 4, 4);  // e2e4
  TEST_ASSERT_TRUE(game.getFen() != fenBefore);
  game.undoMove();
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), game.getFen().c_str());
}

void test_game_cache_invalidated_on_load_fen(void) {
  setUpGame();
  game.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  std::string fen = game.getFen();
  TEST_ASSERT_TRUE(fen.find("4k3") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_game_tests() {
  needsDefaultKings = false;

  // Threefold repetition
  RUN_TEST(test_game_threefold_repetition);
  RUN_TEST(test_game_threefold_different_castling_rights);

  // Observer notification / batching
  RUN_TEST(test_game_observer_fires_on_move);
  RUN_TEST(test_game_observer_fires_on_new_game);
  RUN_TEST(test_game_batch_suppresses_observer);
  RUN_TEST(test_game_no_observer_no_crash);
  RUN_TEST(test_game_nested_batch);
  RUN_TEST(test_game_end_batch_without_begin);
  RUN_TEST(test_game_batch_first_last_single_observer);

  // History integration
  RUN_TEST(test_game_history_records_moves);
  RUN_TEST(test_game_history_correct_fields);
  RUN_TEST(test_game_history_capture_recorded);
  RUN_TEST(test_game_history_en_passant_recorded);
  RUN_TEST(test_game_history_castling_recorded);
  RUN_TEST(test_game_history_promotion_recorded);
  RUN_TEST(test_game_history_check_recorded);
  RUN_TEST(test_game_history_new_game_clears);
  RUN_TEST(test_game_history_load_fen_clears);
  RUN_TEST(test_game_history_prevstate_saved);
  RUN_TEST(test_game_history_invalid_move_not_recorded);
  RUN_TEST(test_game_history_last_move_after_sequence);

  // Navigation orchestrator (SAN/LAN formats test board-replay logic)
  RUN_TEST(test_game_get_history_san_format);
  RUN_TEST(test_game_get_history_lan_format);

  // Lifecycle (gameOver / endGame)
  RUN_TEST(test_game_end_game_resignation);
  RUN_TEST(test_game_end_game_timeout);
  RUN_TEST(test_game_end_game_aborted);
  RUN_TEST(test_game_end_game_draw_agreement);
  RUN_TEST(test_game_end_game_double_call);
  RUN_TEST(test_game_move_after_game_over_rejected);
  RUN_TEST(test_game_end_game_preserves_fen);
  RUN_TEST(test_game_load_fen_resets_game_over);
  RUN_TEST(test_game_new_game_resets_game_over);
  RUN_TEST(test_game_checkmate_sets_game_over);
  RUN_TEST(test_game_stalemate_sets_game_over);
  RUN_TEST(test_game_insufficient_material_sets_game_over);
  RUN_TEST(test_game_fifty_move_sets_game_over);
  RUN_TEST(test_game_undo_clears_game_over);
  RUN_TEST(test_game_redo_restores_game_over);
  RUN_TEST(test_game_san_checkmate_suffix);
  RUN_TEST(test_game_san_check_suffix);

  // makeMove(string)
  RUN_TEST(test_game_makeMove_string_coordinate);
  RUN_TEST(test_game_makeMove_string_invalid);
  RUN_TEST(test_game_makeMove_string_empty);
  RUN_TEST(test_game_makeMove_string_promotion);

  // board() / history() accessors
  RUN_TEST(test_game_board_accessor);
  RUN_TEST(test_game_history_accessor);

  // FEN / evaluation caching
  RUN_TEST(test_game_fen_cache_consistent);
  RUN_TEST(test_game_eval_cache_consistent);
  RUN_TEST(test_game_eval_updates_after_capture);
  RUN_TEST(test_game_fen_updates_after_move);
  RUN_TEST(test_game_cache_invalidated_on_undo);
  RUN_TEST(test_game_cache_invalidated_on_load_fen);
}
