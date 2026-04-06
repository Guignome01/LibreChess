#include <unity.h>

#include "../test_helpers.h"
#include <position.h>
#include <evaluation.h>
#include <zobrist.h>
#include <move.h>
#include <bitboard.h>
#include <types.h>

static Position pos;

// Reset Position before every test
static void setUpPosition(void) {
  pos = Position();
  pos.newGame();
}

// ---------------------------------------------------------------------------
// New game / initial state
// ---------------------------------------------------------------------------

void test_position_new_game_board(void) {
  setUpPosition();
  // Verify each square matches INITIAL_BOARD
  for (Square sq = 0; sq < 64; sq++)
    TEST_ASSERT_ENUM_EQ(Position::INITIAL_BOARD[sq], pos.getSquare(sq));
}

void test_position_new_game_turn(void) {
  setUpPosition();
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_new_game_not_over(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.isCheckmate());
  TEST_ASSERT_FALSE(pos.isDraw());
}

void test_position_new_game_fen(void) {
  setUpPosition();
  TEST_ASSERT_EQUAL_STRING(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      pos.getFen().c_str());
}

void test_position_initial_evaluation_zero(void) {
  setUpPosition();
  TEST_ASSERT_EQUAL_INT(0, eval::evaluatePosition(pos.bitboards()));
}

// ---------------------------------------------------------------------------
// Basic moves
// ---------------------------------------------------------------------------

void test_position_e2e4(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2e4
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_FALSE(r.isCapture());
  TEST_ASSERT_FALSE(r.isCastling());
  TEST_ASSERT_FALSE(r.isEnPassant());
  TEST_ASSERT_FALSE(r.isPromotion());
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pos.currentTurn());
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(6, 4))); // e2 empty
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(4, 4))); // e4 has pawn
}

void test_position_illegal_move_rejected(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(squareOf(6, 4), squareOf( 3, 4)); // e2e5 — not legal
  TEST_ASSERT_FALSE(r.valid());
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn()); // turn unchanged
}

void test_position_wrong_turn_rejected(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(squareOf(1, 4), squareOf( 3, 4)); // e7e5 — black's pawn, but it's white's turn
  TEST_ASSERT_FALSE(r.valid());
}

void test_position_empty_square_rejected(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(squareOf(4, 4), squareOf( 3, 4)); // e4e5 — empty square
  TEST_ASSERT_FALSE(r.valid());
}

void test_position_out_of_bounds_rejected(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(-1, squareOf(0, 0)); // invalid Square
  TEST_ASSERT_FALSE(r.valid());
  r = pos.makeMove(squareOf(0, 0), 64); // invalid Square
  TEST_ASSERT_FALSE(r.valid());
}

void test_position_move_after_game_over_rejected(void) {
  setUpPosition();
  // Reach checkmate via scholar's mate, then try to move
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e4
  pos.makeMove(squareOf(1, 4), squareOf( 3, 4)); // e5
  pos.makeMove(squareOf(7, 5), squareOf( 4, 2)); // Bc4
  pos.makeMove(squareOf(1, 0), squareOf( 2, 0)); // a6
  pos.makeMove(squareOf(7, 3), squareOf( 3, 7)); // Qh5
  pos.makeMove(squareOf(1, 1), squareOf( 2, 1)); // b6
  MoveResult r = pos.makeMove(squareOf(3, 7), squareOf( 1, 5)); // Qxf7#
  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, r.gameResult);
  // Board no longer guards; this tests that the position is in checkmate
  TEST_ASSERT_TRUE(pos.isCheckmate());
}

// ---------------------------------------------------------------------------
// getSquare
// ---------------------------------------------------------------------------

void test_position_getSquare_returns_piece(void) {
  setUpPosition();
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(squareOf(7, 0))); // a1
  TEST_ASSERT_ENUM_EQ(Piece::B_KING, pos.getSquare(squareOf(0, 4))); // e8
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(4, 4))); // e4 empty
}

// ---------------------------------------------------------------------------
// invalidMoveResult
// ---------------------------------------------------------------------------

void test_position_invalidMoveResult_fields(void) {
  MoveResult r = invalidMoveResult();
  TEST_ASSERT_FALSE(r.valid());
  TEST_ASSERT_FALSE(r.isCapture());
  TEST_ASSERT_FALSE(r.isEnPassant());
  TEST_ASSERT_EQUAL_INT(SQ_NONE, r.epCapturedSq);
  TEST_ASSERT_FALSE(r.isCastling());
  TEST_ASSERT_FALSE(r.isPromotion());
  TEST_ASSERT_ENUM_EQ(Piece::NONE, r.promotedTo);
  TEST_ASSERT_FALSE(r.isCheck());
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_EQUAL_CHAR(' ', r.winnerColor);
}

// ---------------------------------------------------------------------------
// Captures
// ---------------------------------------------------------------------------

void test_position_simple_capture(void) {
  setUpPosition();
  // Set up a position where white can capture
  pos.loadFEN("rnbqkbnr/pppp1ppp/8/4p3/3P4/8/PPP1PPPP/RNBQKBNR w KQkq e6 0 2");
  MoveResult r = pos.makeMove(squareOf(4, 3), squareOf( 3, 4)); // d4xe5
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCapture());
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(3, 4))); // e5 now has white pawn
}

// ---------------------------------------------------------------------------
// En passant
// ---------------------------------------------------------------------------

void test_position_en_passant_white(void) {
  setUpPosition();
  // White pawn on e5, black just played d7d5
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
  MoveResult r = pos.makeMove(squareOf(3, 4), squareOf( 2, 3)); // e5xd6 en passant
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isEnPassant());
  TEST_ASSERT_TRUE(r.isCapture());
  TEST_ASSERT_EQUAL_INT(squareOf(3, 3), r.epCapturedSq); // captured pawn was on d5 (row 3, col 3)
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(3, 3))); // d5 cleared
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(2, 3))); // d6 has white pawn
}

void test_position_en_passant_black(void) {
  setUpPosition();
  // Black pawn on d4, white just played e2e4
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3");
  MoveResult r = pos.makeMove(squareOf(4, 3), squareOf( 5, 4)); // d4xe3 en passant
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isEnPassant());
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(4, 4))); // e4 cleared
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, pos.getSquare(squareOf(5, 4))); // e3 has black pawn
}

void test_position_ep_target_set_after_double_push(void) {
  setUpPosition();
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2-e4 (double push)
  const PositionState& st = pos.positionState();
  TEST_ASSERT_EQUAL_INT(squareOf(5, 4), st.epSquare); // EP target = e3
}

void test_position_ep_target_cleared_after_other_move(void) {
  setUpPosition();
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2-e4 sets EP target
  TEST_ASSERT_TRUE(pos.positionState().epSquare != SQ_NONE); // EP target exists
  pos.makeMove(squareOf(1, 0), squareOf( 2, 0)); // a7-a6 (not a double push)
  TEST_ASSERT_EQUAL_INT(SQ_NONE, pos.positionState().epSquare); // EP target cleared
}

// ---------------------------------------------------------------------------
// Castling
// ---------------------------------------------------------------------------

void test_position_white_kingside_castle(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 6)); // e1g1
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCastling());
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.getSquare(squareOf(7, 6))); // king on g1
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(squareOf(7, 5))); // rook on f1
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(7, 4))); // e1 empty
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(7, 7))); // h1 empty
}

void test_position_white_queenside_castle(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 2)); // e1c1
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCastling());
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.getSquare(squareOf(7, 2))); // king on c1
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(squareOf(7, 3))); // rook on d1
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(7, 0))); // a1 empty
}

void test_position_black_kingside_castle(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1");
  MoveResult r = pos.makeMove(squareOf(0, 4), squareOf( 0, 6)); // e8g8
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCastling());
  TEST_ASSERT_ENUM_EQ(Piece::B_KING, pos.getSquare(squareOf(0, 6))); // king on g8
  TEST_ASSERT_ENUM_EQ(Piece::B_ROOK, pos.getSquare(squareOf(0, 5))); // rook on f8
}

void test_position_castling_revokes_rights(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  pos.makeMove(squareOf(7, 4), squareOf( 7, 6)); // white king-side castle
  // White castling rights revoked
  uint8_t rights = pos.getCastlingRights();
  TEST_ASSERT_EQUAL_UINT8(0, rights & 0x03); // K and Q bits cleared
  TEST_ASSERT_EQUAL_UINT8(0x0C, rights & 0x0C); // k and q bits still set
}

void test_position_rook_move_revokes_right(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  pos.makeMove(squareOf(7, 7), squareOf( 7, 6)); // Rh1-g1 (not castling, just a rook move)
  uint8_t rights = pos.getCastlingRights();
  TEST_ASSERT_EQUAL_UINT8(0, rights & 0x01); // K right revoked
  TEST_ASSERT_EQUAL_UINT8(0x02, rights & 0x02); // Q right still present
}

void test_position_black_queenside_castle(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1");
  MoveResult r = pos.makeMove(squareOf(0, 4), squareOf( 0, 2)); // e8c8
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCastling());
  TEST_ASSERT_ENUM_EQ(Piece::B_KING, pos.getSquare(squareOf(0, 2))); // king on c8
  TEST_ASSERT_ENUM_EQ(Piece::B_ROOK, pos.getSquare(squareOf(0, 3))); // rook on d8
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(0, 0))); // a8 empty
}

void test_position_rook_captured_revokes_castling(void) {
  setUpPosition();
  // Black rook at h2 can capture white rook at h1; white has K castling right
  pos.loadFEN("4k3/8/8/8/8/8/7r/4K2R b K - 0 1");
  MoveResult r = pos.makeMove(squareOf(6, 7), squareOf( 7, 7)); // rh2 x Rh1
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCapture());
  TEST_ASSERT_EQUAL_UINT8(0, pos.getCastlingRights()); // K right revoked
}

// ---------------------------------------------------------------------------
// Promotion
// ---------------------------------------------------------------------------

void test_position_auto_queen_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/4P3/8/8/8/8/8/4K2k w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(1, 4), squareOf( 0, 4)); // e7e8 — auto-queen
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isPromotion());
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, r.promotedTo);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(squareOf(0, 4)));
}

void test_position_knight_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/4P3/8/8/8/8/8/4K2k w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(1, 4), squareOf( 0, 4), 'n'); // e7e8n
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isPromotion());
  TEST_ASSERT_ENUM_EQ(Piece::W_KNIGHT, r.promotedTo); // uppercase for white
  TEST_ASSERT_ENUM_EQ(Piece::W_KNIGHT, pos.getSquare(squareOf(0, 4)));
}

void test_position_black_promotion(void) {
  setUpPosition();
  pos.loadFEN("4K2k/8/8/8/8/8/4p3/8 b - - 0 1");
  MoveResult r = pos.makeMove(squareOf(6, 4), squareOf( 7, 4)); // e2e1 — auto-queen
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isPromotion());
  TEST_ASSERT_ENUM_EQ(Piece::B_QUEEN, r.promotedTo); // lowercase for black
}

void test_position_promotion_with_capture(void) {
  setUpPosition();
  // White pawn on d7 captures black rook on e8 and promotes
  pos.loadFEN("4r2k/3P4/8/8/8/8/8/4K3 w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(1, 3), squareOf( 0, 4)); // d7xe8=Q
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCapture());
  TEST_ASSERT_TRUE(r.isPromotion());
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, r.promotedTo);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(squareOf(0, 4)));
}

// ---------------------------------------------------------------------------
// Check detection
// ---------------------------------------------------------------------------

void test_position_move_gives_check(void) {
  setUpPosition();
  // White rook on a1, black king on e8 — Ra1-a8+ is check along rank 8
  pos.loadFEN("4k3/8/8/8/8/8/4K3/R7 w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 0), squareOf( 0, 0)); // Ra1-a8+
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCheck());
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult); // not checkmate
}

void test_position_move_no_check(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2e4 — not check
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_FALSE(r.isCheck());
}

// ---------------------------------------------------------------------------
// Checkmate
// ---------------------------------------------------------------------------

void test_position_scholars_mate(void) {
  setUpPosition();
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2e4
  pos.makeMove(squareOf(1, 4), squareOf( 3, 4)); // e7e5
  pos.makeMove(squareOf(7, 5), squareOf( 4, 2)); // Bf1-c4
  pos.makeMove(squareOf(1, 0), squareOf( 2, 0)); // a7a6
  pos.makeMove(squareOf(7, 3), squareOf( 3, 7)); // Qd1-h5
  pos.makeMove(squareOf(1, 1), squareOf( 2, 1)); // b7b6
  MoveResult r = pos.makeMove(squareOf(3, 7), squareOf( 1, 5)); // Qh5xf7# — checkmate
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, r.gameResult);
  TEST_ASSERT_EQUAL_CHAR('w', r.winnerColor);
  TEST_ASSERT_TRUE(pos.isCheckmate());
}

void test_position_back_rank_mate(void) {
  setUpPosition();
  // White rook delivers back-rank mate
  pos.loadFEN("6k1/5ppp/8/8/8/8/8/R3K3 w Q - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 0), squareOf( 0, 0)); // Ra1-a8#
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, r.gameResult);
  TEST_ASSERT_EQUAL_CHAR('w', r.winnerColor);
}

// ---------------------------------------------------------------------------
// Stalemate
// ---------------------------------------------------------------------------

void test_position_stalemate(void) {
  setUpPosition();
  // Black king on a8, white king on c6, white queen on b1.
  // Qb1-b6 leaves black with no legal moves and no check → stalemate.
  pos.loadFEN("k7/8/2K5/8/8/8/8/1Q6 w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 1), squareOf( 2, 1)); // Qb1-b6 — stalemates black king
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::STALEMATE, r.gameResult);
  TEST_ASSERT_EQUAL_CHAR('d', r.winnerColor);
  TEST_ASSERT_TRUE(Position::isStalemate(pos.bitboards(), pos.mailbox(),
                                         pos.currentTurn(), pos.positionState()));
}

// ---------------------------------------------------------------------------
// 50-move rule
// ---------------------------------------------------------------------------

void test_position_fifty_move_draw(void) {
  setUpPosition();
  // Load position with halfmove clock at 99
  pos.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 99 50");
  MoveResult r = pos.makeMove(squareOf(7, 0), squareOf( 7, 1)); // Ra1-b1 (non-capture, non-pawn = clock hits 100)
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_50, r.gameResult);
  TEST_ASSERT_EQUAL_CHAR('d', r.winnerColor);
  TEST_ASSERT_TRUE(pos.isFiftyMoves());
  TEST_ASSERT_TRUE(pos.isDraw());
}

// ---------------------------------------------------------------------------
// Insufficient material
// ---------------------------------------------------------------------------

void test_position_insufficient_material_k_vs_k(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  // Any move should trigger DRAW_INSUFFICIENT
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 3)); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_INSUFFICIENT, r.gameResult);
  TEST_ASSERT_TRUE(Position::isInsufficientMaterial(pos.bitboards()));
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_insufficient_material_kb_vs_k(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4KB2 w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 3)); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_INSUFFICIENT, r.gameResult);
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_insufficient_material_kn_vs_k(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K1N1 w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 3)); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_INSUFFICIENT, r.gameResult);
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_insufficient_material_kb_vs_kb_same_color(void) {
  setUpPosition();
  // Both bishops on light squares (c1=dark, d1 would be light, f1=light)
  // c8 is light square (row 0 + col 2 = even), f1 is light square (row 7 + col 5 = even)
  pos.loadFEN("2b1k3/8/8/8/8/8/8/4KB2 w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 3)); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_INSUFFICIENT, r.gameResult);
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_insufficient_material_kb_vs_kb_diff_color(void) {
  setUpPosition();
  // White bishop on f1 (light: 7+5=even), black bishop on c8 is light too.
  // Put black bishop on d8 (dark: 0+3=odd) for different colors.
  pos.loadFEN("3bk3/8/8/8/8/8/8/4KB2 w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 3)); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid());
  // Different color bishops — NOT insufficient
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_FALSE(Position::isInsufficientMaterial(pos.bitboards()));
}

void test_position_sufficient_material_knn(void) {
  setUpPosition();
  // K+N+N vs K — sufficient material (checkmate is possible)
  pos.loadFEN("4k3/8/8/8/8/8/8/2N1KN2 w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 3)); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_FALSE(pos.isCheckmate());
}

void test_position_sufficient_material_kp_vs_k(void) {
  setUpPosition();
  // K+P vs K — sufficient material (pawn can promote)
  pos.loadFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 3)); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
}

// ---------------------------------------------------------------------------
// FEN loading
// ---------------------------------------------------------------------------

void test_position_load_fen_sets_turn(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pos.currentTurn());
}

void test_position_load_fen_resets_game_over(void) {
  setUpPosition();
  // Reach checkmate first
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e4
  pos.makeMove(squareOf(1, 4), squareOf( 3, 4)); // e5
  pos.makeMove(squareOf(7, 5), squareOf( 4, 2)); // Bc4
  pos.makeMove(squareOf(1, 0), squareOf( 2, 0)); // a6
  pos.makeMove(squareOf(7, 3), squareOf( 3, 7)); // Qh5
  pos.makeMove(squareOf(1, 1), squareOf( 2, 1)); // b6
  MoveResult r = pos.makeMove(squareOf(3, 7), squareOf( 1, 5)); // Qxf7#
  TEST_ASSERT_TRUE(pos.isCheckmate());
  // loadFEN resets position to non-terminal
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  TEST_ASSERT_FALSE(pos.isCheckmate());
}

void test_position_load_fen_roundtrip(void) {
  setUpPosition();
  std::string inputFen = "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1";
  pos.loadFEN(inputFen);
  TEST_ASSERT_EQUAL_STRING(inputFen.c_str(), pos.getFen().c_str());
}

void test_position_load_fen_complex(void) {
  setUpPosition();
  std::string fen = "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4";
  pos.loadFEN(fen);
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
  TEST_ASSERT_EQUAL_UINT8(0x0F, pos.getCastlingRights()); // KQkq
  TEST_ASSERT_EQUAL_INT(SQ_NONE, pos.positionState().epSquare);     // no EP
  TEST_ASSERT_EQUAL_INT(4, pos.positionState().halfmoveClock);
  TEST_ASSERT_EQUAL_INT(4, pos.positionState().fullmoveClock);
  TEST_ASSERT_ENUM_EQ(Piece::W_BISHOP, pos.getSquare(squareOf(4, 2))); // Bc4
  TEST_ASSERT_ENUM_EQ(Piece::B_KNIGHT, pos.getSquare(squareOf(2, 2))); // Nc6
  TEST_ASSERT_EQUAL_STRING(fen.c_str(), pos.getFen().c_str());
}

// --- FEN validation ---

void test_position_load_fen_rejects_empty(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.loadFEN(""));
}

void test_position_load_fen_rejects_too_few_ranks(void) {
  setUpPosition();
  // Only 7 ranks (6 slashes)
  TEST_ASSERT_FALSE(pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1"));
}

void test_position_load_fen_rejects_too_many_ranks(void) {
  setUpPosition();
  // 9 ranks (8 slashes)
  TEST_ASSERT_FALSE(pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
}

void test_position_load_fen_rejects_invalid_piece(void) {
  setUpPosition();
  // 'x' is not a valid piece character
  TEST_ASSERT_FALSE(pos.loadFEN("xnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
}

void test_position_load_fen_rejects_rank_overflow(void) {
  setUpPosition();
  // First rank sums to 9
  TEST_ASSERT_FALSE(pos.loadFEN("rnbqkbnrr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
}

void test_position_load_fen_rejects_invalid_turn(void) {
  setUpPosition();
  // 'x' is not a valid turn
  TEST_ASSERT_FALSE(pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1"));
}

void test_position_load_fen_valid_returns_true(void) {
  setUpPosition();
  TEST_ASSERT_TRUE(pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
}

void test_position_load_fen_invalid_preserves_state(void) {
  setUpPosition();
  // Make a move so state differs from initial
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2e4
  std::string fenBefore = pos.getFen();
  // Invalid FEN should leave state unchanged
  TEST_ASSERT_FALSE(pos.loadFEN("bad_fen"));
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
}

void test_position_load_fen_missing_king_returns_false(void) {
  setUpPosition();
  std::string fenBefore = pos.getFen();
  // Structurally valid FEN but no kings — must be rejected.
  TEST_ASSERT_FALSE(pos.loadFEN("8/8/8/8/8/8/8/8 w - - 0 1"));
  // State must be preserved after rejection.
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
}

// ---------------------------------------------------------------------------
// FEN / evaluation cache
// ---------------------------------------------------------------------------

void test_position_fen_cache_consistent(void) {
  setUpPosition();
  std::string fen1 = pos.getFen();
  std::string fen2 = pos.getFen();
  TEST_ASSERT_EQUAL_STRING(fen1.c_str(), fen2.c_str());

  pos.makeMove(squareOf(6, 4), squareOf( 4, 4));  // e2e4
  std::string fen3 = pos.getFen();
  TEST_ASSERT_TRUE(fen3 != fen1);  // FEN changed after move
}

void test_position_eval_cache_consistent(void) {
  setUpPosition();
  int eval1 = eval::evaluatePosition(pos.bitboards());
  int eval2 = eval::evaluatePosition(pos.bitboards());
  TEST_ASSERT_EQUAL_INT(eval1, eval2);

  // Load asymmetric position (white missing queen) and verify eval updates
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1");
  int eval3 = eval::evaluatePosition(pos.bitboards());
  // White has no queen → clearly negative evaluation
  TEST_ASSERT_TRUE(eval3 < eval1);
}

void test_position_end_game_preserves_fen(void) {
  setUpPosition();
  std::string fenBefore = pos.getFen();
  // Making a move changes FEN, but non-terminal positions preserve structure
  TEST_ASSERT_EQUAL_STRING(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      fenBefore.c_str());
}

void test_position_eval_after_capture(void) {
  setUpPosition();
  int initialEval = eval::evaluatePosition(pos.bitboards());
  // White captures black's e-pawn (material advantage for white)
  pos.loadFEN("rnbqkbnr/pppp1ppp/8/4p3/3P4/8/PPP1PPPP/RNBQKBNR w KQkq e6 0 2");
  pos.makeMove(squareOf(4, 3), squareOf( 3, 4)); // d4xe5
  int evalAfter = eval::evaluatePosition(pos.bitboards());
  TEST_ASSERT_TRUE(evalAfter > initialEval); // white gained material
}

// ---------------------------------------------------------------------------
// Position clocks (halfmove / fullmove)
// ---------------------------------------------------------------------------

void test_position_halfmove_clock_increments(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
  pos.makeMove(squareOf(7, 0), squareOf( 7, 1)); // Ra1-b1 (non-capture, non-pawn)
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().halfmoveClock);
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3)); // Ke8-d8
  TEST_ASSERT_EQUAL_INT(2, pos.positionState().halfmoveClock);
}

void test_position_halfmove_clock_resets_on_pawn_move(void) {
  setUpPosition();
  // Start with halfmove=5 and make a pawn move
  pos.loadFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 5 10");
  TEST_ASSERT_EQUAL_INT(5, pos.positionState().halfmoveClock);
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2-e4 (pawn move resets clock)
  TEST_ASSERT_EQUAL_INT(0, pos.positionState().halfmoveClock);
}

void test_position_fullmove_increments_after_black(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().fullmoveClock);
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2-e4 (white moves, fullmove stays 1)
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().fullmoveClock);
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3)); // Ke8-d8 (black moves, fullmove → 2)
  TEST_ASSERT_EQUAL_INT(2, pos.positionState().fullmoveClock);
}

// ---------------------------------------------------------------------------
// inCheck (no-arg, uses current turn)
// ---------------------------------------------------------------------------

void test_position_in_check_true(void) {
  setUpPosition();
  // White king in check from black queen
  pos.loadFEN("4k3/8/8/8/8/8/8/3qK3 w - - 0 1");
  TEST_ASSERT_TRUE(pos.inCheck());
}

void test_position_in_check_false(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.inCheck());
}

// ---------------------------------------------------------------------------
// isCheckmate (no-arg, uses current turn)
// ---------------------------------------------------------------------------

void test_position_is_checkmate_true(void) {
  setUpPosition();
  // Back rank mate: white king on h1, black rook delivers mate on a1
  pos.loadFEN("6k1/8/8/8/8/8/5PPP/r5K1 w - - 0 1");
  TEST_ASSERT_TRUE(pos.isCheckmate());
}

void test_position_is_checkmate_false(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.isCheckmate());
}

// ---------------------------------------------------------------------------
// moveNumber
// ---------------------------------------------------------------------------

void test_position_move_number_initial(void) {
  setUpPosition();
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().fullmoveClock);
}

void test_position_move_number_after_moves(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2-e4 (white, fullmove stays 1)
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().fullmoveClock);
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3)); // Ke8-d8 (black, fullmove → 2)
  TEST_ASSERT_EQUAL_INT(2, pos.positionState().fullmoveClock);
}

void test_position_move_number_from_fen(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 42");
  TEST_ASSERT_EQUAL_INT(42, pos.positionState().fullmoveClock);
}

// ---------------------------------------------------------------------------
// Threefold repetition (Zobrist position tracking in Position)
// ---------------------------------------------------------------------------

void test_position_threefold_repetition(void) {
  setUpPosition();
  // Position where Ke1 and Ke8 shuffle back and forth (pawns provide sufficient material)
  pos.loadFEN("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1");
  // Move 1: Ke1-d1, Ke8-d8
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3));
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3));
  // Move 2: Kd1-e1, Kd8-e8 (back to original — occurrence 2)
  pos.makeMove(squareOf(7, 3), squareOf( 7, 4));
  pos.makeMove(squareOf(0, 3), squareOf( 0, 4));
  // Move 3: Ke1-d1, Ke8-d8
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3));
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3));
  // Move 4: Kd1-e1, Kd8-e8 (back to original — occurrence 3)
  pos.makeMove(squareOf(7, 3), squareOf( 7, 4));
  MoveResult r = pos.makeMove(squareOf(0, 3), squareOf( 0, 4));  // third repetition
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_3FOLD, r.gameResult);
  TEST_ASSERT_TRUE(pos.isRepetition());
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_threefold_different_castling_rights(void) {
  setUpPosition();
  // King move loses castling rights → initial hash differs from subsequent hashes
  pos.loadFEN("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3)); // Ke1-d1 (loses K castling)
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3));
  pos.makeMove(squareOf(7, 3), squareOf( 7, 4));
  pos.makeMove(squareOf(0, 3), squareOf( 0, 4)); // board same as start, but castling=0
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3));
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3));
  pos.makeMove(squareOf(7, 3), squareOf( 7, 4));
  MoveResult r = pos.makeMove(squareOf(0, 3), squareOf( 0, 4)); // occurrence 2 (not 3)
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_FALSE(pos.isRepetition());
}

void test_position_threefold_not_reached(void) {
  setUpPosition();
  // Only 2 occurrences — not enough for threefold
  pos.loadFEN("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1");
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3));
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3));
  pos.makeMove(squareOf(7, 3), squareOf( 7, 4));
  MoveResult r = pos.makeMove(squareOf(0, 3), squareOf( 0, 4)); // occurrence 2
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_FALSE(pos.isRepetition());
}

void test_position_threefold_query(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.isRepetition());
}

void test_position_threefold_with_rook_moves(void) {
  setUpPosition();
  // Repeat rook moves to produce threefold repetition (sufficient material)
  pos.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
  pos.makeMove(squareOf(7, 0), squareOf( 7, 1)); // Ra1-b1
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3)); // Ke8-d8
  pos.makeMove(squareOf(7, 1), squareOf( 7, 0)); // Rb1-a1
  pos.makeMove(squareOf(0, 3), squareOf( 0, 4)); // Kd8-e8
  pos.makeMove(squareOf(7, 0), squareOf( 7, 1));
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3));
  pos.makeMove(squareOf(7, 1), squareOf( 7, 0));
  MoveResult r = pos.makeMove(squareOf(0, 3), squareOf( 0, 4)); // 3rd time
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_3FOLD, r.gameResult);
  TEST_ASSERT_TRUE(pos.isRepetition());
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_position_history_reset_on_pawn_move(void) {
  setUpPosition();
  // Start with some moves to build position history, then a pawn move resets it
  pos.loadFEN("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1");
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3)); // Ke1-d1
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3)); // Ke8-d8
  pos.makeMove(squareOf(7, 3), squareOf( 7, 4)); // Kd1-e1
  pos.makeMove(squareOf(0, 3), squareOf( 0, 4)); // Kd8-e8 (occurrence 2)
  // Pawn move resets halfmove clock, which resets position history
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4)); // e2-e4
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3)); // Ke8-d8
  // Now even if positions repeat, the prior history is gone
  TEST_ASSERT_FALSE(pos.isRepetition());
}

// ---------------------------------------------------------------------------
// reverseMove / applyMoveEntry
// ---------------------------------------------------------------------------

// Helper: build a MoveEntry from scratch
static MoveEntry makeBoardEntry(int fr, int fc, int tr, int tc, Piece piece,
                                Piece captured, const PositionState& prev,
                                Piece promo = Piece::NONE, bool ep = false,
                                Square epSq = SQ_NONE, bool castle = false,
                                bool check = false) {
  MoveEntry e = {};
  e.from = squareOf(fr, fc); e.to = squareOf(tr, tc);
  e.piece = piece; e.captured = captured; e.promotion = promo;
  e.flags = 0;
  if (captured != Piece::NONE) e.flags |= ME_CAPTURE;
  if (ep) e.flags |= ME_EP;
  if (castle) e.flags |= ME_CASTLING;
  if (promo != Piece::NONE) e.flags |= ME_PROMOTION;
  if (check) e.flags |= ME_CHECK;
  e.epCapturedSq = epSq;
  e.prevState = prev;
  return e;
}

void test_position_reverse_move_simple(void) {
  setUpPosition();
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(squareOf(6, 4), squareOf( 4, 4));  // e2-e4
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pos.currentTurn());

  MoveEntry e = makeBoardEntry(6, 4, 4, 4, Piece::W_PAWN, Piece::NONE, before);
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(6, 4)));  // pawn back on e2
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(4, 4)));  // e4 empty
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_reverse_move_capture(void) {
  setUpPosition();
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4));  // e4
  pos.makeMove(squareOf(1, 3), squareOf( 3, 3));  // d5
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(squareOf(4, 4), squareOf( 3, 3));  // exd5 capture
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCapture());

  MoveEntry e = makeBoardEntry(4, 4, 3, 3, Piece::W_PAWN, Piece::B_PAWN, before);
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(4, 4)));  // pawn back on e5
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, pos.getSquare(squareOf(3, 3)));  // black pawn restored on d5
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_reverse_move_en_passant(void) {
  setUpPosition();
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4));  // e4
  pos.makeMove(squareOf(1, 0), squareOf( 2, 0));  // a6
  pos.makeMove(squareOf(4, 4), squareOf( 3, 4));  // e5
  pos.makeMove(squareOf(1, 3), squareOf( 3, 3));  // d5
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(squareOf(3, 4), squareOf( 2, 3));  // exd6 en passant
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isEnPassant());

  MoveEntry e = makeBoardEntry(3, 4, 2, 3, Piece::W_PAWN, Piece::B_PAWN, before, Piece::NONE, true, squareOf(3, 3));
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(3, 4)));  // pawn back on e5
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(2, 3)));  // d6 empty
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, pos.getSquare(squareOf(3, 3)));  // black pawn restored on d5
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_reverse_move_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(squareOf(7, 4), squareOf( 7, 6));  // O-O white kingside
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isCastling());

  MoveEntry e = makeBoardEntry(7, 4, 7, 6, Piece::W_KING, Piece::NONE, before, Piece::NONE, false, -1, true);
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.getSquare(squareOf(7, 4)));  // king back on e1
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(7, 6)));  // g1 empty
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(squareOf(7, 7)));  // rook back on h1
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(7, 5)));  // f1 empty
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
  // Castling rights should be restored
  TEST_ASSERT_EQUAL(0x0F, pos.positionState().castlingRights);
}

void test_position_reverse_move_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/P4k2/8/8/8/8/5K2/8 w - - 0 1");
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(squareOf(1, 0), squareOf( 0, 0), 'q');  // a7-a8=Q
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isPromotion());
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(squareOf(0, 0)));

  MoveEntry e = makeBoardEntry(1, 0, 0, 0, Piece::W_PAWN, Piece::NONE, before, Piece::W_QUEEN);
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(1, 0)));  // pawn back on a7
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(0, 0)));  // a8 empty
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_reverse_move_clears_game_over(void) {
  setUpPosition();
  // Scholar's mate
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4));  // e4
  pos.makeMove(squareOf(1, 4), squareOf( 3, 4));  // e5
  pos.makeMove(squareOf(7, 5), squareOf( 4, 2));  // Bc4
  pos.makeMove(squareOf(1, 0), squareOf( 2, 0));  // a6
  pos.makeMove(squareOf(7, 3), squareOf( 3, 7));  // Qh5
  pos.makeMove(squareOf(1, 1), squareOf( 2, 1));  // b6
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(squareOf(3, 7), squareOf( 1, 5));  // Qxf7#
  TEST_ASSERT_TRUE(pos.isCheckmate());

  MoveEntry e = makeBoardEntry(3, 7, 1, 5, Piece::W_QUEEN, Piece::B_PAWN, before, Piece::NONE, false, -1, false, true);
  pos.reverseMove(e);

  TEST_ASSERT_FALSE(pos.isCheckmate());
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_apply_move_entry(void) {
  setUpPosition();
  PositionState before = pos.positionState();
  MoveEntry e = makeBoardEntry(6, 4, 4, 4, Piece::W_PAWN, Piece::NONE, before);

  MoveResult r = pos.applyMoveEntry(e);
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(4, 4)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(6, 4)));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pos.currentTurn());
}

void test_position_apply_move_entry_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/P4k2/8/8/8/8/5K2/8 w - - 0 1");
  PositionState before = pos.positionState();
  MoveEntry e = makeBoardEntry(1, 0, 0, 0, Piece::W_PAWN, Piece::NONE, before, Piece::W_QUEEN);

  MoveResult r = pos.applyMoveEntry(e);
  TEST_ASSERT_TRUE(r.valid());
  TEST_ASSERT_TRUE(r.isPromotion());
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(squareOf(0, 0)));
}

// ---------------------------------------------------------------------------
// King cache
// ---------------------------------------------------------------------------

void test_position_king_cache_initial(void) {
  setUpPosition();
  TEST_ASSERT_EQUAL_INT(7, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(0, rowOf(pos.kingSq(Color::BLACK)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::BLACK)));
}

void test_position_king_cache_after_king_move(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3));  // Ke1-d1
  TEST_ASSERT_EQUAL_INT(7, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(3, fileOf(pos.kingSq(Color::WHITE)));
  // Black king unchanged
  TEST_ASSERT_EQUAL_INT(0, rowOf(pos.kingSq(Color::BLACK)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::BLACK)));
}

void test_position_king_cache_after_non_king_move(void) {
  setUpPosition();
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4));  // e2-e4 (pawn move)
  // Kings haven't moved
  TEST_ASSERT_EQUAL_INT(7, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(0, rowOf(pos.kingSq(Color::BLACK)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::BLACK)));
}

void test_position_king_cache_after_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  pos.makeMove(squareOf(7, 4), squareOf( 7, 6));  // O-O white kingside
  TEST_ASSERT_EQUAL_INT(7, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(6, fileOf(pos.kingSq(Color::WHITE)));
}

void test_position_king_cache_after_load_fen(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1");
  TEST_ASSERT_EQUAL_INT(7, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(0, rowOf(pos.kingSq(Color::BLACK)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::BLACK)));

  // Now a position with kings off-center
  pos.loadFEN("8/8/8/3k4/8/8/8/1K6 w - - 0 1");
  TEST_ASSERT_EQUAL_INT(7, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(1, fileOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(3, rowOf(pos.kingSq(Color::BLACK)));
  TEST_ASSERT_EQUAL_INT(3, fileOf(pos.kingSq(Color::BLACK)));
}

void test_position_king_cache_after_reverse_move(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  PositionState before = pos.positionState();
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3));  // Ke1-d1
  TEST_ASSERT_EQUAL_INT(3, fileOf(pos.kingSq(Color::WHITE)));

  MoveEntry e = makeBoardEntry(7, 4, 7, 3, Piece::W_KING, Piece::NONE, before);
  pos.reverseMove(e);

  TEST_ASSERT_EQUAL_INT(7, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::WHITE)));
}

void test_position_king_cache_reverse_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  PositionState before = pos.positionState();
  pos.makeMove(squareOf(7, 4), squareOf( 7, 6));  // O-O
  TEST_ASSERT_EQUAL_INT(6, fileOf(pos.kingSq(Color::WHITE)));

  MoveEntry e = makeBoardEntry(7, 4, 7, 6, Piece::W_KING, Piece::NONE, before,
                               Piece::NONE, false, -1, true);
  pos.reverseMove(e);

  TEST_ASSERT_EQUAL_INT(7, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::WHITE)));
}

// ---------------------------------------------------------------------------
// MoveList struct
// ---------------------------------------------------------------------------

void test_movelist_initial_state(void) {
  MoveList moves;
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

void test_movelist_add_and_access(void) {
  MoveList moves;
  // Add Move structs — use squareOf to encode (row, col) into from/to
  moves.add(Move(0, static_cast<uint8_t>(squareOf(3, 5))));
  moves.add(Move(0, static_cast<uint8_t>(squareOf(0, 7))));
  TEST_ASSERT_EQUAL_INT(2, moves.count);
  TEST_ASSERT_EQUAL_INT(3, rowOf(moves.moves[0].to));
  TEST_ASSERT_EQUAL_INT(5, fileOf(moves.moves[0].to));
  TEST_ASSERT_EQUAL_INT(0, rowOf(moves.moves[1].to));
  TEST_ASSERT_EQUAL_INT(7, fileOf(moves.moves[1].to));
}

void test_movelist_clear(void) {
  MoveList moves;
  moves.add(Move(0, static_cast<uint8_t>(squareOf(1, 2))));
  moves.add(Move(0, static_cast<uint8_t>(squareOf(3, 4))));
  TEST_ASSERT_EQUAL_INT(2, moves.count);
  moves.clear();
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

void test_movelist_fills_to_capacity(void) {
  MoveList moves;
  for (int i = 0; i < MAX_MOVES; i++) {
    moves.add(Move(0, static_cast<uint8_t>(squareOf(i % 8, i / 4 % 8))));
  }
  TEST_ASSERT_EQUAL_INT(MAX_MOVES, moves.count);
  // Verify first and last entries
  TEST_ASSERT_EQUAL_INT(0, rowOf(moves.moves[0].to));
  TEST_ASSERT_EQUAL_INT(0, fileOf(moves.moves[0].to));
}

void test_movelist_used_by_get_possible_moves(void) {
  setUpPosition();
  MoveList moves;
  pos.getPossibleMoves(squareOf(6, 4), moves);  // e2 pawn: e3 and e4
  TEST_ASSERT_EQUAL_INT(2, moves.count);
}

// ---------------------------------------------------------------------------
// HashHistory struct
// ---------------------------------------------------------------------------

void test_hashhistory_initial_state(void) {
  HashHistory h;
  TEST_ASSERT_EQUAL_INT(0, h.count);
}

void test_hashhistory_add_and_read(void) {
  HashHistory h;
  h.keys[h.count++] = 0xDEADBEEF;
  h.keys[h.count++] = 0xCAFEBABE;
  TEST_ASSERT_EQUAL_INT(2, h.count);
  TEST_ASSERT_EQUAL_UINT64(0xDEADBEEF, h.keys[0]);
  TEST_ASSERT_EQUAL_UINT64(0xCAFEBABE, h.keys[1]);
}

void test_hashhistory_max_size(void) {
  TEST_ASSERT_EQUAL_INT(128, HashHistory::MAX_SIZE);
}

// ---------------------------------------------------------------------------
// loadFEN edge cases
// ---------------------------------------------------------------------------

void test_position_load_fen_sets_castling_rights(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w Kq - 0 1");
  // K + q = 0x01 | 0x08 = 0x09
  TEST_ASSERT_EQUAL_UINT8(0x09, pos.getCastlingRights());
}

void test_position_load_fen_sets_ep_target(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
  const PositionState& st = pos.positionState();
  TEST_ASSERT_EQUAL_INT(squareOf(5, 4), st.epSquare);  // e3
}

void test_position_load_fen_sets_clocks(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 42 21");
  TEST_ASSERT_EQUAL_INT(42, pos.positionState().halfmoveClock);
  TEST_ASSERT_EQUAL_INT(21, pos.positionState().fullmoveClock);
}

// ---------------------------------------------------------------------------
// Board-level threefold repetition query
// ---------------------------------------------------------------------------

void test_position_isRepetition_query(void) {
  setUpPosition();
  pos.loadFEN("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1");
  // Repeat position 3 times
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3));  // Ke1-d1
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3));  // Ke8-d8
  pos.makeMove(squareOf(7, 3), squareOf( 7, 4));  // Kd1-e1
  pos.makeMove(squareOf(0, 3), squareOf( 0, 4));  // Kd8-e8 — 2nd occurrence
  pos.makeMove(squareOf(7, 4), squareOf( 7, 3));  // Ke1-d1
  pos.makeMove(squareOf(0, 4), squareOf( 0, 3));  // Ke8-d8
  pos.makeMove(squareOf(7, 3), squareOf( 7, 4));  // Kd1-e1
  pos.makeMove(squareOf(0, 3), squareOf( 0, 4));  // Kd8-e8 — 3rd occurrence
  TEST_ASSERT_TRUE(pos.isRepetition());
}

void test_position_isRepetition_false(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.isRepetition());
}

// ---------------------------------------------------------------------------
// reverseMove cache restoration
// ---------------------------------------------------------------------------

void test_position_reverse_move_restores_fen(void) {
  setUpPosition();
  std::string fenBefore = pos.getFen();
  PositionState before = pos.positionState();
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4));  // e2-e4
  TEST_ASSERT_FALSE(fenBefore == pos.getFen());

  MoveEntry e = makeBoardEntry(6, 4, 4, 4, Piece::W_PAWN, Piece::NONE, before);
  pos.reverseMove(e);

  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
}

void test_position_reverse_move_restores_eval(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
  int evalBefore = eval::evaluatePosition(pos.bitboards());
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(squareOf(4, 4), squareOf( 3, 3));  // exd5 capture
  TEST_ASSERT_TRUE(r.valid());
  // Eval should change after capture
  TEST_ASSERT_FALSE(evalBefore == eval::evaluatePosition(pos.bitboards()));

  MoveEntry e = makeBoardEntry(4, 4, 3, 3, Piece::W_PAWN, Piece::B_PAWN, before);
  pos.reverseMove(e);

  TEST_ASSERT_EQUAL_INT(evalBefore, eval::evaluatePosition(pos.bitboards()));
}

// ---------------------------------------------------------------------------
// make / unmake (raw search interface)
// ---------------------------------------------------------------------------

// Helper: verify every square matches between two positions.
static void assertBoardEqual(Position& a, Position& b) {
  for (int row = 0; row < 8; row++)
    for (int col = 0; col < 8; col++)
      TEST_ASSERT_ENUM_EQ(a.getSquare(squareOf(row, col)), b.getSquare(squareOf(row, col)));
}

void test_make_unmake_roundtrip_quiet(void) {
  setUpPosition();
  Position before;
  before.newGame();
  Move m(squareOf(6, 4), squareOf(4, 4));  // e2-e4

  UndoInfo undo = pos.make(m);
  // Board should differ after make
  TEST_ASSERT_FALSE(pos.getFen() == before.getFen());
  pos.unmake(m, undo);
  // Board should be restored
  assertBoardEqual(before, pos);
  TEST_ASSERT_EQUAL_STRING(before.getFen().c_str(), pos.getFen().c_str());
}

void test_make_unmake_roundtrip_capture(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
  std::string fenBefore = pos.getFen();
  uint64_t hashBefore = pos.hash();

  Move m(squareOf(4, 4), squareOf(3, 3), MOVE_CAPTURE);  // exd5
  UndoInfo undo = pos.make(m);

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(3, 3)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(4, 4)));

  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
  TEST_ASSERT_EQUAL_UINT64(hashBefore, pos.hash());
}

void test_make_unmake_roundtrip_ep(void) {
  setUpPosition();
  // White pawn on e5, Black just played d7-d5 → EP target d6
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
  std::string fenBefore = pos.getFen();
  uint64_t hashBefore = pos.hash();

  Move m(squareOf(3, 4), squareOf(2, 3), MOVE_CAPTURE | MOVE_EP);  // exd6 EP
  UndoInfo undo = pos.make(m);

  // Pawn should be on d6, d5 and e5 empty
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(2, 3)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(3, 3)));  // captured pawn removed
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(3, 4)));

  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
  TEST_ASSERT_EQUAL_UINT64(hashBefore, pos.hash());
}

void test_make_unmake_roundtrip_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  std::string fenBefore = pos.getFen();
  uint64_t hashBefore = pos.hash();

  Move m(squareOf(7, 4), squareOf(7, 6), MOVE_CASTLING);  // White O-O
  UndoInfo undo = pos.make(m);

  // King on g1, rook on f1
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.getSquare(squareOf(7, 6)));
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(squareOf(7, 5)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(7, 4)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(7, 7)));

  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
  TEST_ASSERT_EQUAL_UINT64(hashBefore, pos.hash());
}

void test_make_unmake_roundtrip_queenside_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  std::string fenBefore = pos.getFen();

  Move m(squareOf(7, 4), squareOf(7, 2), MOVE_CASTLING);  // White O-O-O
  UndoInfo undo = pos.make(m);

  // King on c1, rook on d1
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.getSquare(squareOf(7, 2)));
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(squareOf(7, 3)));

  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
}

void test_make_unmake_roundtrip_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/P4k2/8/8/8/8/5K2/8 w - - 0 1");
  std::string fenBefore = pos.getFen();
  uint64_t hashBefore = pos.hash();

  uint8_t queenPromoFlags = Move::promoFlags(Move::promoIndexFromType(PieceType::QUEEN));
  Move m(squareOf(1, 0), squareOf(0, 0), queenPromoFlags);  // a7-a8=Q
  UndoInfo undo = pos.make(m);

  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(squareOf(0, 0)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(1, 0)));

  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
  TEST_ASSERT_EQUAL_UINT64(hashBefore, pos.hash());
  // Unmake should restore pawn, not queen
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(squareOf(1, 0)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(squareOf(0, 0)));
}

void test_make_unmake_roundtrip_promotion_capture(void) {
  setUpPosition();
  pos.loadFEN("1n6/P4k2/8/8/8/8/5K2/8 w - - 0 1");
  std::string fenBefore = pos.getFen();
  uint64_t hashBefore = pos.hash();

  uint8_t queenPromo = Move::promoFlags(Move::promoIndexFromType(PieceType::QUEEN));
  Move m(squareOf(1, 0), squareOf(0, 1), MOVE_CAPTURE | queenPromo);  // axb8=Q
  UndoInfo undo = pos.make(m);

  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(squareOf(0, 1)));

  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
  TEST_ASSERT_EQUAL_UINT64(hashBefore, pos.hash());
  // Knight should be restored
  TEST_ASSERT_ENUM_EQ(Piece::B_KNIGHT, pos.getSquare(squareOf(0, 1)));
}

void test_make_hash_matches_compute(void) {
  setUpPosition();
  Move m(squareOf(6, 4), squareOf(4, 4));  // e2-e4
  pos.make(m);

  uint64_t incremental = pos.hash();
  uint64_t computed = zobrist::computeHash(
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState(), false);
  TEST_ASSERT_EQUAL_UINT64(computed, incremental);
}

void test_make_hash_matches_compute_capture(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");

  Move m(squareOf(4, 4), squareOf(3, 3), MOVE_CAPTURE);  // exd5
  pos.make(m);

  uint64_t incremental = pos.hash();
  uint64_t computed = zobrist::computeHash(
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState(), false);
  TEST_ASSERT_EQUAL_UINT64(computed, incremental);
}

void test_make_hash_matches_compute_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");

  Move m(squareOf(7, 4), squareOf(7, 6), MOVE_CASTLING);  // O-O
  pos.make(m);

  uint64_t incremental = pos.hash();
  uint64_t computed = zobrist::computeHash(
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState(), false);
  TEST_ASSERT_EQUAL_UINT64(computed, incremental);
}

void test_make_hash_matches_compute_ep(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");

  Move m(squareOf(3, 4), squareOf(2, 3), MOVE_CAPTURE | MOVE_EP);  // exd6 EP
  pos.make(m);

  uint64_t incremental = pos.hash();
  uint64_t computed = zobrist::computeHash(
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState(), false);
  TEST_ASSERT_EQUAL_UINT64(computed, incremental);
}

void test_make_hash_matches_compute_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/P4k2/8/8/8/8/5K2/8 w - - 0 1");

  uint8_t queenPromo = Move::promoFlags(Move::promoIndexFromType(PieceType::QUEEN));
  Move m(squareOf(1, 0), squareOf(0, 0), queenPromo);  // a7-a8=Q
  pos.make(m);

  uint64_t incremental = pos.hash();
  uint64_t computed = zobrist::computeHash(
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState(), false);
  TEST_ASSERT_EQUAL_UINT64(computed, incremental);
}

void test_make_updates_turn(void) {
  setUpPosition();
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.sideToMove());

  Move m(squareOf(6, 4), squareOf(4, 4));  // e2-e4
  pos.make(m);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pos.sideToMove());
}

void test_make_updates_king_cache(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  TEST_ASSERT_EQUAL_INT(7, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::WHITE)));

  Move m(squareOf(7, 4), squareOf(6, 4));  // Ke1-e2
  pos.make(m);
  TEST_ASSERT_EQUAL_INT(6, rowOf(pos.kingSq(Color::WHITE)));
  TEST_ASSERT_EQUAL_INT(4, fileOf(pos.kingSq(Color::WHITE)));
}

void test_make_sets_ep_after_double_push(void) {
  setUpPosition();
  Move m(squareOf(6, 4), squareOf(4, 4));  // e2-e4
  pos.make(m);
  TEST_ASSERT_EQUAL_INT(squareOf(5, 4), pos.positionState().epSquare);
}

void test_make_clears_ep_after_normal_move(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
  // Black plays Nf6, should clear EP
  Move m(squareOf(0, 6), squareOf(2, 5));  // Ng8-f6
  pos.make(m);
  TEST_ASSERT_EQUAL_INT(SQ_NONE, pos.positionState().epSquare);
}

void test_make_resets_halfmove_on_capture(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 5 10");
  TEST_ASSERT_EQUAL_INT(5, pos.positionState().halfmoveClock);

  Move m(squareOf(4, 4), squareOf(3, 3), MOVE_CAPTURE);  // exd5
  pos.make(m);
  TEST_ASSERT_EQUAL_INT(0, pos.positionState().halfmoveClock);
}

void test_make_increments_fullmove_after_black(void) {
  setUpPosition();
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().fullmoveClock);

  Move e4(squareOf(6, 4), squareOf(4, 4));
  pos.make(e4);
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().fullmoveClock);  // still 1 after White

  Move e5(squareOf(1, 4), squareOf(3, 4));
  pos.make(e5);
  TEST_ASSERT_EQUAL_INT(2, pos.positionState().fullmoveClock);  // increments after Black
}

void test_make_unmake_sequence_multiple_moves(void) {
  setUpPosition();
  std::string fenStart = pos.getFen();
  uint64_t hashStart = pos.hash();

  // 1. e2-e4
  Move m1(squareOf(6, 4), squareOf(4, 4));
  UndoInfo u1 = pos.make(m1);

  // 2. e7-e5
  Move m2(squareOf(1, 4), squareOf(3, 4));
  UndoInfo u2 = pos.make(m2);

  // 3. Ng1-f3
  Move m3(squareOf(7, 6), squareOf(5, 5));
  UndoInfo u3 = pos.make(m3);

  // Unmake in reverse order
  pos.unmake(m3, u3);
  pos.unmake(m2, u2);
  pos.unmake(m1, u1);

  TEST_ASSERT_EQUAL_STRING(fenStart.c_str(), pos.getFen().c_str());
  TEST_ASSERT_EQUAL_UINT64(hashStart, pos.hash());
}

void test_make_castling_revokes_castling_rights(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  TEST_ASSERT_EQUAL_UINT8(0x0F, pos.getCastlingRights());  // all rights

  Move m(squareOf(7, 4), squareOf(7, 6), MOVE_CASTLING);  // O-O
  UndoInfo undo = pos.make(m);

  // White lost both castling rights
  uint8_t rights = pos.getCastlingRights();
  TEST_ASSERT_FALSE(rights & 0x03);  // White K+Q gone

  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_UINT8(0x0F, pos.getCastlingRights());  // restored
}

// ---------------------------------------------------------------------------
// Incremental material tracking
// ---------------------------------------------------------------------------

// Verify material() matches eval::computeMaterial() at startpos.
void test_material_initial_position(void) {
  setUpPosition();
  int expected = eval::computeMaterial(pos.bitboards());
  TEST_ASSERT_EQUAL_INT(expected, pos.material());
  TEST_ASSERT_EQUAL_INT(0, pos.material());  // symmetric = 0
}

// material() stays correct after regular capture.
void test_material_after_capture(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
  Move m(squareOf(4, 4), squareOf(3, 3), MOVE_CAPTURE);  // exd5
  UndoInfo undo = pos.make(m);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
}

// material() stays correct after en passant capture.
void test_material_after_ep_capture(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
  Move m(squareOf(3, 4), squareOf(2, 3), MOVE_EP);  // exd6 e.p.
  UndoInfo undo = pos.make(m);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
}

// material() stays correct after promotion.
void test_material_after_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/P3k3/8/8/8/8/4K3/8 w - - 0 1");
  uint8_t queenPromo = Move::promoFlags(Move::promoIndexFromType(PieceType::QUEEN));
  Move m(squareOf(1, 0), squareOf(0, 0), queenPromo);  // a8=Q
  UndoInfo undo = pos.make(m);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
}

// material() stays correct across a sequence of make/unmake moves.
void test_material_make_unmake_sequence(void) {
  setUpPosition();
  // Play a few moves and verify after each
  Move m1(squareOf(6, 4), squareOf(4, 4), 0);  // e4
  UndoInfo u1 = pos.make(m1);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());

  Move m2(squareOf(1, 3), squareOf(3, 3), 0);  // d5
  UndoInfo u2 = pos.make(m2);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());

  Move m3(squareOf(4, 4), squareOf(3, 3), MOVE_CAPTURE);  // exd5
  UndoInfo u3 = pos.make(m3);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());

  pos.unmake(m3, u3);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
  pos.unmake(m2, u2);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
  pos.unmake(m1, u1);
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
  TEST_ASSERT_EQUAL_INT(0, pos.material());  // back to symmetric
}

// material() preserved across null move make/unmake.
void test_material_null_move(void) {
  setUpPosition();
  pos.loadFEN("r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2");
  int before = pos.material();
  UndoInfo undo = pos.makeNullMove();
  TEST_ASSERT_EQUAL_INT(before, pos.material());
  pos.unmakeNullMove(undo);
  TEST_ASSERT_EQUAL_INT(before, pos.material());
}

// material() recomputed after loadFEN.
void test_material_after_load_fen(void) {
  setUpPosition();
  pos.loadFEN("rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2");
  TEST_ASSERT_EQUAL_INT(eval::computeMaterial(pos.bitboards()), pos.material());
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------


// ===========================================================================
// Tests merged from test_rules.cpp (Position static methods + movegen rules)
// ===========================================================================
// ===========================================================================
// isCheck
// ===========================================================================

static void test_king_not_in_check_initial(void) {
  setupInitialBoard(bb, mailbox);
  TEST_ASSERT_FALSE(Position::isCheck(bb, Color::WHITE));
  TEST_ASSERT_FALSE(Position::isCheck(bb, Color::BLACK));
}

static void test_king_in_check_by_rook(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // rook on same file
  TEST_ASSERT_TRUE(Position::isCheck(bb, Color::WHITE));
}

static void test_king_in_check_by_bishop(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_BISHOP, "h4"); // bishop on diagonal
  TEST_ASSERT_TRUE(Position::isCheck(bb, Color::WHITE));
}

static void test_king_in_check_by_knight(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "f3"); // knight checks
  TEST_ASSERT_TRUE(Position::isCheck(bb, Color::WHITE));
}

static void test_king_in_check_by_pawn(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e4");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5"); // black pawn attacks e4 from d5
  TEST_ASSERT_TRUE(Position::isCheck(bb, Color::WHITE));
}

static void test_king_in_check_by_queen(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_QUEEN, "e8"); // queen on same file
  TEST_ASSERT_TRUE(Position::isCheck(bb, Color::WHITE));
}

static void test_king_not_in_check_blocked(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e2"); // own pawn blocks rook
  TEST_ASSERT_FALSE(Position::isCheck(bb, Color::WHITE));
}

static void test_black_king_in_check(void) {
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_ROOK, "e1"); // white rook attacks
  TEST_ASSERT_TRUE(Position::isCheck(bb, Color::BLACK));
}

// ===========================================================================
// isCheckmate
// ===========================================================================

static void test_back_rank_mate(void) {
  // Classic back-rank mate: Black king on g8, pawns on f7/g7/h7, White rook on e8
  placePiece(bb, mailbox, Piece::B_KING, "g8");
  placePiece(bb, mailbox, Piece::B_PAWN, "f7");
  placePiece(bb, mailbox, Piece::B_PAWN, "g7");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  placePiece(bb, mailbox, Piece::W_ROOK, "e8"); // delivers mate
  PositionState flags{0x00, SQ_NONE, 0, 1};
  TEST_ASSERT_TRUE(Position::isCheckmate(bb, mailbox, Color::BLACK, flags));
}

static void test_scholars_mate(void) {
  // Scholar's mate position: White queen on f7 delivers checkmate
  PositionState state;
  Color turn;
  fen::fenToBoard("r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b - - 0 1", bb, mailbox, turn, &state);
  TEST_ASSERT_TRUE(Position::isCheckmate(bb, mailbox, Color::BLACK, state));
}

static void test_not_checkmate_can_block(void) {
  // King in check but can block
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "d1"); // own rook can block/interpose
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // attacking rook
  PositionState flags{0x00, SQ_NONE, 0, 1};
  TEST_ASSERT_FALSE(Position::isCheckmate(bb, mailbox, Color::WHITE, flags));
}

static void test_not_checkmate_can_escape(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // rook checks
  // King can escape to d1, d2, f1, f2
  PositionState flags{0x00, SQ_NONE, 0, 1};
  TEST_ASSERT_FALSE(Position::isCheckmate(bb, mailbox, Color::WHITE, flags));
}

static void test_not_checkmate_can_capture_attacker(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e2"); // rook checks from e2
  // King can capture the rook (assuming no support)
  PositionState flags{0x00, SQ_NONE, 0, 1};
  TEST_ASSERT_FALSE(Position::isCheckmate(bb, mailbox, Color::WHITE, flags));
}

static void test_smothered_mate(void) {
  // Philidor's smothered mate: Kh8, Rg8, g7/h7 pawns, white Nf7#
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::B_ROOK, "g8"); // own rook blocks g8
  placePiece(bb, mailbox, Piece::B_PAWN, "g7");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "f7"); // knight checks h8, blocks via g8/g7/h7
  PositionState flags{0x00, SQ_NONE, 0, 1};
  TEST_ASSERT_TRUE(Position::isCheck(bb, Color::BLACK));
  TEST_ASSERT_TRUE(Position::isCheckmate(bb, mailbox, Color::BLACK, flags));
}

// ===========================================================================
// isStalemate
// ===========================================================================

static void test_stalemate_king_only(void) {
  // Classic stalemate: Black king on a8, White queen on b6, White king on c6
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_QUEEN, "b6");
  placePiece(bb, mailbox, Piece::W_KING, "c6");
  PositionState flags{0x00, SQ_NONE, 0, 1};
  // Black to move — king has no legal moves, not in check
  TEST_ASSERT_FALSE(Position::isCheck(bb, Color::BLACK));
  TEST_ASSERT_TRUE(Position::isStalemate(bb, mailbox, Color::BLACK, flags));
}

static void test_not_stalemate_has_move(void) {
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_KING, "c6");
  PositionState flags{0x00, SQ_NONE, 0, 1};
  // Black king can move to b8, b7, a7
  TEST_ASSERT_FALSE(Position::isStalemate(bb, mailbox, Color::BLACK, flags));
}

static void test_stalemate_with_blocked_pawns(void) {
  // Black king on a8, black pawn on a7 blocked by white pawn on a6.
  // White king on c7 controls b8, b7, c8, d8, d7.
  // Black has no legal moves: king surrounded, pawn blocked.
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_PAWN, "a7");
  placePiece(bb, mailbox, Piece::W_PAWN, "a6"); // blocks the pawn
  placePiece(bb, mailbox, Piece::W_KING, "c7"); // controls b8, b7, c8, d8, d7
  PositionState flags{0x00, SQ_NONE, 0, 1};
  TEST_ASSERT_FALSE(Position::isCheck(bb, Color::BLACK));
  TEST_ASSERT_TRUE(Position::isStalemate(bb, mailbox, Color::BLACK, flags));
}

// ===========================================================================
// Move legality (can't move into check, pins)
// ===========================================================================

static void test_king_cannot_move_into_check(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "f8"); // rook controls f-file
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("f1", tr, tc);
  PositionState flags{0x00, SQ_NONE, 0, 1};
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, squareOf(r, c), squareOf(tr, tc), flags));
}

static void test_pinned_piece_cannot_move(void) {
  // Bishop pinned to its own king
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_BISHOP, "e2"); // bishop on same file, between king and rook
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // enemy rook pins bishop
  int r, c;
  sq("e2", r, c);
  int tr, tc;
  sq("d3", tr, tc);
  PositionState flags{0x00, SQ_NONE, 0, 1};
  // Moving bishop exposes king to check → illegal
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, squareOf(r, c), squareOf(tr, tc), flags));
}

static void test_pinned_piece_can_move_along_pin(void) {
  // Rook pinned along file — can move along that file
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "e4"); // own rook on same file
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // enemy rook pins
  int r, c;
  sq("e4", r, c);
  int tr, tc;
  sq("e8", tr, tc); // capture the pinning rook
  PositionState flags{0x00, SQ_NONE, 0, 1};
  TEST_ASSERT_TRUE(movegen::isValidMove(bb, mailbox, squareOf(r, c), squareOf(tr, tc), flags));
}

static void test_diagonal_pin(void) {
  // Black pawn on d4 pinned to black king on g7 by white bishop on a1
  placePiece(bb, mailbox, Piece::B_KING, "g7");
  placePiece(bb, mailbox, Piece::W_KING, "a8");
  placePiece(bb, mailbox, Piece::B_PAWN, "d4");
  placePiece(bb, mailbox, Piece::W_BISHOP, "a1"); // pins d4 to g7 along diagonal
  int r, c;
  sq("d4", r, c);
  PositionState flags{0x00, SQ_NONE, 0, 1};
  // Pawn should have 0 legal moves (pinned diagonally, can't move along pin)
  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, squareOf(r, c), flags, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

static void test_discovered_check(void) {
  // White bishop on c1 blocks white rook on a1 from checking black king on h1.
  // Move the bishop away to reveal the rook check.
  placePiece(bb, mailbox, Piece::B_KING, "h1");
  placePiece(bb, mailbox, Piece::W_KING, "a8");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1"); // rook on a1
  placePiece(bb, mailbox, Piece::W_BISHOP, "d1"); // bishop blocks rank 1
  // After bishop moves to e2 (off rank 1), rook gives check along rank 1
  // Verify the bishop CAN move (it would reveal check on opponent's king)
  int r, c;
  sq("d1", r, c);
  int tr, tc;
  sq("e2", tr, tc);
  PositionState flags{0x00, SQ_NONE, 0, 1};
  TEST_ASSERT_TRUE(movegen::isValidMove(bb, mailbox, squareOf(r, c), squareOf(tr, tc), flags));
}

static void test_double_check_only_king_can_move(void) {
  // Black king in double check from white rook and bishop.
  // Only king moves should be legal — no blocks or captures by other pieces.
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "e1"); // rook checks along e-file
  placePiece(bb, mailbox, Piece::W_BISHOP, "b5"); // bishop checks along b5-e8 diagonal
  placePiece(bb, mailbox, Piece::B_KNIGHT, "d6"); // black knight could theoretically block/capture

  PositionState flags{0x00, SQ_NONE, 0, 1};
  // King is in check
  TEST_ASSERT_TRUE(Position::isCheck(bb, Color::BLACK));
  // Knight on d6 cannot resolve double check (even though it attacks both e4 and b5)
  MoveList moves;
  int r, c;
  sq("d6", r, c);
  movegen::getPossibleMoves(bb, mailbox, squareOf(r, c), flags, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

// ===========================================================================
// Pin-aware generation — checkMask and pin detection scenarios
// ===========================================================================

static void test_single_check_slider_can_block(void) {
  // White king e1 in check from black rook e8.
  // White rook on d4 (not pinned) can interpose at e4 but cannot make unrelated moves.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // checks king on e-file
  placePiece(bb, mailbox, Piece::W_ROOK, "d4"); // can block at e4
  PositionState flags{0x00, SQ_NONE, 0, 1};
  // Moving to e4 interposes the check — legal
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, 4, 3, 4, 4, flags));
  // Moving to d5 does not address the check — illegal
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, 4, 3, 3, 3, flags));
}

static void test_knight_check_no_blocking(void) {
  // White king e1 in check from black knight f3.
  // White bishop d4 cannot block (non-colinear) and cannot capture f3 (not on diagonal).
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "f3"); // knight check — cannot be blocked
  placePiece(bb, mailbox, Piece::W_BISHOP, "d4"); // not in position to capture f3
  PositionState flags{0x00, SQ_NONE, 0, 1};
  MoveList moves;
  int r, c;
  sq("d4", r, c);
  movegen::getPossibleMoves(bb, mailbox, squareOf(r, c), flags, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

static void test_two_friendly_shielding_king_not_pinned(void) {
  // White king a1, white rook c1, white knight f1, black rook h1.
  // Two friendlies on rank 1 between king and enemy rook — neither is pinned.
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_ROOK, "c1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "f1");
  placePiece(bb, mailbox, Piece::B_ROOK, "h1"); // blocked by two friendlies
  PositionState flags{0x00, SQ_NONE, 0, 1};
  // Knight is NOT pinned — it has its 4 normal moves (d2, e3, g3, h2)
  MoveList moves;
  int r, c;
  sq("f1", r, c);
  movegen::getPossibleMoves(bb, mailbox, squareOf(r, c), flags, moves);
  TEST_ASSERT_EQUAL_INT(4, moves.count);
}

static void test_ep_horizontal_pin_illegal(void) {
  // White king a5, white pawn d5, black pawn e5 (just moved), black rook h5.
  // After EP capture dxe6, both d5 and e5 are cleared — king on a5 is exposed to rook on h5.
  placePiece(bb, mailbox, Piece::W_KING, "a5");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "d5");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5"); // last moved from e7
  placePiece(bb, mailbox, Piece::B_ROOK, "h5"); // would give check after EP
  int epR, epC;
  sq("e6", epR, epC); // EP target square
  PositionState flags{0x00, squareOf(epR, epC), 0, 1};
  TEST_ASSERT_FALSE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::WHITE, flags));
}

static void test_getPossibleMoves_idempotent(void) {
  // Calling getPossibleMoves twice on the same position must return identical
  // results (verifies no internal state leaks between calls).
  placePiece(bb, mailbox, Piece::W_KING, "e4");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h5"); // limits some king moves
  PositionState flags{0x00, SQ_NONE, 0, 1};
  int r, c;
  sq("e4", r, c);
  MoveList moves1, moves2;
  movegen::getPossibleMoves(bb, mailbox, squareOf(r, c), flags, moves1);
  movegen::getPossibleMoves(bb, mailbox, squareOf(r, c), flags, moves2);
  TEST_ASSERT_EQUAL_INT(moves1.count, moves2.count);
}

// ===========================================================================
// hasLegalEnPassantCapture (direct tests)
// ===========================================================================

static void test_hasLegalEnPassantCapture_true(void) {
  // White pawn on e5, black pawn on d5, EP target d6
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  int epR, epC;
  sq("d6", epR, epC);
  PositionState flags{0x00, squareOf(epR, epC), 0, 1};
  TEST_ASSERT_TRUE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::WHITE, flags));
}

static void test_hasLegalEnPassantCapture_false_no_target(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  PositionState flags{0x00, SQ_NONE, 0, 1}; // no EP target
  TEST_ASSERT_FALSE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::WHITE, flags));
}

// EP capture from a-file pawn (only one adjacent file: b).
static void test_hasLegalEnPassantCapture_a_file(void) {
  placePiece(bb, mailbox, Piece::W_KING, "h1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::W_PAWN, "a5");
  placePiece(bb, mailbox, Piece::B_PAWN, "b5");
  int epR, epC;
  sq("b6", epR, epC);
  PositionState flags{0x00, squareOf(epR, epC), 0, 1};
  TEST_ASSERT_TRUE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::WHITE, flags));
}

// EP capture from h-file pawn (only one adjacent file: g).
static void test_hasLegalEnPassantCapture_h_file(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "h5");
  placePiece(bb, mailbox, Piece::B_PAWN, "g5");
  int epR, epC;
  sq("g6", epR, epC);
  PositionState flags{0x00, squareOf(epR, epC), 0, 1};
  TEST_ASSERT_TRUE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::WHITE, flags));
}

// No EP capture when pawn is on the wrong file.
static void test_hasLegalEnPassantCapture_wrong_file(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "c5");  // c-file pawn
  placePiece(bb, mailbox, Piece::B_PAWN, "e5");   // e-file pawn that doubled
  int epR, epC;
  sq("e6", epR, epC);
  PositionState flags{0x00, squareOf(epR, epC), 0, 1};
  // c-pawn is 2 files away from e6 — no EP possible
  TEST_ASSERT_FALSE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::WHITE, flags));
}

// Black EP capture on h-file edge.
static void test_hasLegalEnPassantCapture_black_h_file(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_PAWN, "h4");
  placePiece(bb, mailbox, Piece::W_PAWN, "g4");
  int epR, epC;
  sq("g3", epR, epC);
  PositionState flags{0x00, squareOf(epR, epC), 0, 1};
  TEST_ASSERT_TRUE(movegen::hasLegalEnPassantCapture(bb, mailbox, Color::BLACK, flags));
}

// ===========================================================================
// isSquareUnderAttack (direct tests)
// ===========================================================================

static void test_isSquareUnderAttack_by_pawn(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  int r, c;
  sq("e4", r, c);
  // e4 is attacked by black pawn from d5 (defending color = white → attacker = black)
  TEST_ASSERT_TRUE(attacks::isSquareUnderAttack(bb, squareOf(r, c), Color::WHITE));
}

static void test_isSquareUnderAttack_by_knight(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "f3");
  int r, c;
  sq("e1", r, c);
  TEST_ASSERT_TRUE(attacks::isSquareUnderAttack(bb, squareOf(r, c), Color::WHITE));
}

static void test_isSquareUnderAttack_not_attacked(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "f3");
  int r, c;
  sq("a4", r, c);
  TEST_ASSERT_FALSE(attacks::isSquareUnderAttack(bb, squareOf(r, c), Color::WHITE));
}

// ===========================================================================
// isValidMove — direct tests
// ===========================================================================

static void test_isValidMove_basic_valid(void) {
  setupInitialBoard(bb, mailbox);
  PositionState flags = PositionState::initial();
  // e2e4 is valid
  TEST_ASSERT_TRUE(movegen::isValidMove(bb, mailbox, squareOf(6, 4), squareOf(4, 4), flags));
}

static void test_isValidMove_illegal_destination(void) {
  setupInitialBoard(bb, mailbox);
  PositionState flags = PositionState::initial();
  // e2e5 (3 squares) is illegal for a pawn
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, squareOf(6, 4), squareOf(3, 4), flags));
}

static void test_isValidMove_empty_source(void) {
  PositionState flags{0x00, SQ_NONE, 0, 1};
  placePiece(bb, mailbox, Piece::W_KING, "h1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  // e4 is empty — moving from empty square is invalid
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, squareOf(4, 4), squareOf(3, 4), flags));
}

// ===========================================================================
// isDraw — direct static tests
// ===========================================================================

static void test_rules_isDraw_insufficient(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  PositionState st{0x00, SQ_NONE, 0, 1};
  HashHistory hh{};
  TEST_ASSERT_TRUE(Position::isDraw(bb, mailbox, Color::WHITE, st, hh));
}

static void test_rules_isDraw_fifty_move(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");  // sufficient material
  PositionState st{0x00, SQ_NONE, 100, 50};
  HashHistory hh{};
  TEST_ASSERT_TRUE(Position::isDraw(bb, mailbox, Color::WHITE, st, hh));
}

static void test_rules_isDraw_false(void) {
  setupInitialBoard(bb, mailbox);
  PositionState st = PositionState::initial();
  HashHistory hh{};
  TEST_ASSERT_FALSE(Position::isDraw(bb, mailbox, Color::WHITE, st, hh));
}

// ===========================================================================
// isGameOver — direct static tests
// ===========================================================================

static void test_rules_isGameOver_checkmate(void) {
  // Back rank mate
  placePiece(bb, mailbox, Piece::B_KING, "g8");
  placePiece(bb, mailbox, Piece::B_PAWN, "f7");
  placePiece(bb, mailbox, Piece::B_PAWN, "g7");
  placePiece(bb, mailbox, Piece::B_PAWN, "h7");
  placePiece(bb, mailbox, Piece::W_ROOK, "e8");
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  PositionState st{0x00, SQ_NONE, 0, 1};
  HashHistory hh{};
  char winner = ' ';
  GameResult result = Position::isGameOver(bb, mailbox, Color::BLACK, st, hh, winner);
  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, result);
  TEST_ASSERT_EQUAL_CHAR('w', winner);
}

static void test_rules_isGameOver_stalemate(void) {
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_QUEEN, "b6");
  placePiece(bb, mailbox, Piece::W_KING, "c6");
  PositionState st{0x00, SQ_NONE, 0, 1};
  HashHistory hh{};
  char winner = ' ';
  GameResult result = Position::isGameOver(bb, mailbox, Color::BLACK, st, hh, winner);
  TEST_ASSERT_ENUM_EQ(GameResult::STALEMATE, result);
}

static void test_rules_isGameOver_in_progress(void) {
  setupInitialBoard(bb, mailbox);
  PositionState st = PositionState::initial();
  HashHistory hh{};
  char winner = ' ';
  GameResult result = Position::isGameOver(bb, mailbox, Color::WHITE, st, hh, winner);
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, result);
}

// ===========================================================================
// isThreefoldRepetition
// ===========================================================================

static void test_rules_isThreefoldRepetition_direct(void) {
  // Fabricate a HashHistory with 3 identical hashes
  HashHistory hh{};
  hh.keys[0] = 0xABCD;
  hh.keys[1] = 0x1234;
  hh.keys[2] = 0xABCD;
  hh.keys[3] = 0x5678;
  hh.keys[4] = 0xABCD;
  hh.count = 5;
  TEST_ASSERT_TRUE(Position::isThreefoldRepetition(hh));
}

static void test_rules_isThreefoldRepetition_not_reached(void) {
  HashHistory hh{};
  hh.keys[0] = 0xABCD;
  hh.keys[1] = 0x1234;
  hh.keys[2] = 0xABCD;
  hh.count = 3;
  TEST_ASSERT_FALSE(Position::isThreefoldRepetition(hh));
}

// ===========================================================================
// Castling
// ===========================================================================

static void test_white_kingside_castle_available(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  PositionState flags{0x0F, SQ_NONE}; // all rights
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_white_queenside_castle_available(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  PositionState flags{0x0F, SQ_NONE};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("c1", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_black_kingside_castle_available(void) {
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  PositionState flags{0x0F, SQ_NONE};
  int r, c;
  sq("e8", r, c);
  int tr, tc;
  sq("g8", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_black_queenside_castle_available(void) {
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "a8");
  PositionState flags{0x0F, SQ_NONE};
  int r, c;
  sq("e8", r, c);
  int tr, tc;
  sq("c8", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_castle_blocked_by_piece(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "g1"); // knight blocks
  PositionState flags{0x0F, SQ_NONE};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_castle_through_check_forbidden(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::B_ROOK, "f8"); // rook controls f1 — king passes through check
  PositionState flags{0x0F, SQ_NONE};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, squareOf(r, c), squareOf(tr, tc), flags));
}

static void test_castle_while_in_check_forbidden(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // rook gives check on e-file
  PositionState flags{0x0F, SQ_NONE};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, squareOf(r, c), squareOf(tr, tc), flags));
}

static void test_castle_destination_under_attack(void) {
  // g1 attacked by black rook on g8, but f1 is safe
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::B_ROOK, "g8"); // attacks g1 (destination)
  PositionState flags{0x0F, SQ_NONE};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, squareOf(r, c), squareOf(tr, tc), flags));
}

static void test_no_castle_right_revoked(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  PositionState flags{0x00, SQ_NONE, 0, 1}; // no rights
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("g1", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_queenside_blocked_b1(void) {
  // b1 must be empty (rook passes through it)
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "b1");
  PositionState flags{0x0F, SQ_NONE};
  int r, c;
  sq("e1", r, c);
  int tr, tc;
  sq("c1", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc, flags));
}

static void test_partial_castling_rights_kingside_only(void) {
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  PositionState flags{0x01, SQ_NONE}; // only white kingside (K)
  int r, c;
  sq("e1", r, c);
  int ktr, ktc, qtr, qtc;
  sq("g1", ktr, ktc);
  sq("c1", qtr, qtc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, ktr, ktc, flags));  // kingside OK
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, qtr, qtc, flags)); // queenside NO
}

// ===========================================================================
// En passant
// ===========================================================================

static void test_en_passant_white_captures(void) {
  // White pawn on e5, Black pawn just double-pushed to d5
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  int epR, epC;
  sq("d6", epR, epC);
  PositionState flags{0x0F, squareOf(epR, epC)}; // d6

  int r, c;
  sq("e5", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, epR, epC, flags));
}

static void test_en_passant_black_captures(void) {
  placePiece(bb, mailbox, Piece::B_PAWN, "d4");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  int epR, epC;
  sq("e3", epR, epC);
  PositionState flags{0x0F, squareOf(epR, epC)}; // e3

  int r, c;
  sq("d4", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, epR, epC, flags));
}

static void test_en_passant_not_available(void) {
  // Pawns adjacent but no en passant target set
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  // Default flags — no en passant

  int r, c;
  sq("e5", r, c);
  int tr, tc;
  sq("d6", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc));
}

static void test_ep_capture_leaves_king_in_check(void) {
  // Horizontal pin: white K on a5, P on d5, black p on e5 (just double-pushed),
  // black rook on h5. EP capture d5→e6 removes e5 pawn, exposes king along rank 5.
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "a5");
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_PAWN, "d5");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_ROOK, "h5");
  int epR, epC;
  sq("e6", epR, epC);
  PositionState flags{0x00, squareOf(epR, epC), 0, 1};
  int r, c;
  sq("d5", r, c);
  TEST_ASSERT_FALSE(movegen::isValidMove(bb, mailbox, squareOf(r, c), squareOf(epR, epC), flags));
}

static void test_ep_a_file_boundary(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "a5");
  placePiece(bb, mailbox, Piece::B_PAWN, "b5");
  int epR, epC;
  sq("b6", epR, epC);
  PositionState flags{0x0F, squareOf(epR, epC)};
  int r, c;
  sq("a5", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, epR, epC, flags));
}

static void test_ep_h_file_boundary(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "h5");
  placePiece(bb, mailbox, Piece::B_PAWN, "g5");
  int epR, epC;
  sq("g6", epR, epC);
  PositionState flags{0x0F, squareOf(epR, epC)};
  int r, c;
  sq("h5", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, epR, epC, flags));
}

// ===========================================================================
// Helpers: isEnPassantMove, isCastlingMove, etc.
// ===========================================================================

static void test_isEnPassantMove_true(void) {
  // White pawn on e5 captures diagonally to d6 (empty) => en passant
  Position pos;
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 1");
  auto info = pos.checkEnPassant(squareOf(3, 4), squareOf( 2, 3));
  TEST_ASSERT_TRUE(info.isCapture);
}

static void test_isEnPassantMove_false_normal_capture(void) {
  // White pawn captures a piece diagonally -- not en passant
  Position pos;
  pos.loadFEN("rnbqkbnr/ppp1pppp/3p4/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1");
  auto info = pos.checkEnPassant(squareOf(3, 4), squareOf( 2, 3));
  TEST_ASSERT_FALSE(info.isCapture);
}

static void test_isEnPassantMove_non_pawn(void) {
  // Bishop on e5 moves diagonally to d6 (empty) -- not a pawn, no EP
  Position pos;
  pos.loadFEN("rnbqkbnr/pppppppp/8/4B3/8/8/PPPPPPPP/RNBQK1NR w KQkq - 0 1");
  TEST_ASSERT_FALSE(pos.checkEnPassant(squareOf(3, 4), squareOf( 2, 3)).isCapture);
}

static void test_isCastlingMove_true(void) {
  // King moves 2 squares
  Position pos;
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  TEST_ASSERT_TRUE(pos.checkCastling(squareOf(7, 4), squareOf( 7, 6)).isCastling);
  TEST_ASSERT_TRUE(pos.checkCastling(squareOf(7, 4), squareOf( 7, 2)).isCastling);
}

static void test_isCastlingMove_false(void) {
  // King moves 1 square
  Position pos;
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  TEST_ASSERT_FALSE(pos.checkCastling(squareOf(7, 4), squareOf( 7, 5)).isCastling);
}

static void test_isCastlingMove_non_king(void) {
  // Rook moves 2 squares -- not castling
  Position pos;
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  TEST_ASSERT_FALSE(pos.checkCastling(squareOf(7, 0), squareOf( 7, 2)).isCastling);
}

static void test_getEnPassantCapturedPawnSq(void) {
  // White captures en passant moving to row 2 (rank 6) -- captured pawn is on row 3
  Position pos;
  pos.loadFEN("rnbqkbnr/1ppppppp/8/p3P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1");
  auto info = pos.checkEnPassant(squareOf(3, 4), squareOf( 2, 3));
  // Diagonal pawn move to empty → EP detected, captured pawn on row 3
  // (Note: this tests EP detection logic, not full legality)

  // Black captures en passant
  Position pos2;
  pos2.loadFEN("rnbqkbnr/pppp1ppp/8/8/Pp6/8/1PPPPPPP/RNBQKBNR b KQkq a3 0 1");
  auto info2 = pos2.checkEnPassant(squareOf(4, 1), squareOf( 5, 0));
  TEST_ASSERT_TRUE(info2.isCapture);
  TEST_ASSERT_EQUAL_INT(squareOf(4, 0), info2.capturedPawnSq);
}

// ===========================================================================
// Registration
// ===========================================================================

void register_position_tests() {
  needsDefaultKings = false;

  // Initial state
  RUN_TEST(test_position_new_game_board);
  RUN_TEST(test_position_new_game_turn);
  RUN_TEST(test_position_new_game_not_over);
  RUN_TEST(test_position_new_game_fen);
  RUN_TEST(test_position_initial_evaluation_zero);
  RUN_TEST(test_position_getSquare_returns_piece);
  RUN_TEST(test_position_invalidMoveResult_fields);

  // Basic moves
  RUN_TEST(test_position_e2e4);
  RUN_TEST(test_position_illegal_move_rejected);
  RUN_TEST(test_position_wrong_turn_rejected);
  RUN_TEST(test_position_empty_square_rejected);
  RUN_TEST(test_position_out_of_bounds_rejected);
  RUN_TEST(test_position_move_after_game_over_rejected);

  // Captures
  RUN_TEST(test_position_simple_capture);

  // En passant
  RUN_TEST(test_position_en_passant_white);
  RUN_TEST(test_position_en_passant_black);
  RUN_TEST(test_position_ep_target_set_after_double_push);
  RUN_TEST(test_position_ep_target_cleared_after_other_move);

  // Castling
  RUN_TEST(test_position_white_kingside_castle);
  RUN_TEST(test_position_white_queenside_castle);
  RUN_TEST(test_position_black_kingside_castle);
  RUN_TEST(test_position_black_queenside_castle);
  RUN_TEST(test_position_castling_revokes_rights);
  RUN_TEST(test_position_rook_move_revokes_right);
  RUN_TEST(test_position_rook_captured_revokes_castling);

  // Promotion
  RUN_TEST(test_position_auto_queen_promotion);
  RUN_TEST(test_position_knight_promotion);
  RUN_TEST(test_position_black_promotion);
  RUN_TEST(test_position_promotion_with_capture);

  // Check
  RUN_TEST(test_position_move_gives_check);
  RUN_TEST(test_position_move_no_check);

  // Checkmate
  RUN_TEST(test_position_scholars_mate);
  RUN_TEST(test_position_back_rank_mate);

  // Stalemate
  RUN_TEST(test_position_stalemate);

  // 50-move rule
  RUN_TEST(test_position_fifty_move_draw);

  // Insufficient material
  RUN_TEST(test_position_insufficient_material_k_vs_k);
  RUN_TEST(test_position_insufficient_material_kb_vs_k);
  RUN_TEST(test_position_insufficient_material_kn_vs_k);
  RUN_TEST(test_position_insufficient_material_kb_vs_kb_same_color);
  RUN_TEST(test_position_insufficient_material_kb_vs_kb_diff_color);
  RUN_TEST(test_position_sufficient_material_knn);
  RUN_TEST(test_position_sufficient_material_kp_vs_k);

  // FEN loading
  RUN_TEST(test_position_load_fen_sets_turn);
  RUN_TEST(test_position_load_fen_resets_game_over);
  RUN_TEST(test_position_load_fen_roundtrip);
  RUN_TEST(test_position_load_fen_complex);

  // FEN validation
  RUN_TEST(test_position_load_fen_rejects_empty);
  RUN_TEST(test_position_load_fen_rejects_too_few_ranks);
  RUN_TEST(test_position_load_fen_rejects_too_many_ranks);
  RUN_TEST(test_position_load_fen_rejects_invalid_piece);
  RUN_TEST(test_position_load_fen_rejects_rank_overflow);
  RUN_TEST(test_position_load_fen_rejects_invalid_turn);
  RUN_TEST(test_position_load_fen_valid_returns_true);
  RUN_TEST(test_position_load_fen_invalid_preserves_state);
  RUN_TEST(test_position_load_fen_missing_king_returns_false);

  // FEN/eval cache
  RUN_TEST(test_position_fen_cache_consistent);
  RUN_TEST(test_position_eval_cache_consistent);
  RUN_TEST(test_position_end_game_preserves_fen);
  RUN_TEST(test_position_eval_after_capture);

  // Position clocks
  RUN_TEST(test_position_halfmove_clock_increments);
  RUN_TEST(test_position_halfmove_clock_resets_on_pawn_move);
  RUN_TEST(test_position_fullmove_increments_after_black);

  // inCheck (no-arg)
  RUN_TEST(test_position_in_check_true);
  RUN_TEST(test_position_in_check_false);

  // isCheckmate (no-arg)
  RUN_TEST(test_position_is_checkmate_true);
  RUN_TEST(test_position_is_checkmate_false);

  // moveNumber
  RUN_TEST(test_position_move_number_initial);
  RUN_TEST(test_position_move_number_after_moves);
  RUN_TEST(test_position_move_number_from_fen);

  // Threefold repetition
  RUN_TEST(test_position_threefold_repetition);
  RUN_TEST(test_position_threefold_different_castling_rights);
  RUN_TEST(test_position_threefold_not_reached);
  RUN_TEST(test_position_threefold_query);
  RUN_TEST(test_position_threefold_with_rook_moves);
  RUN_TEST(test_position_position_history_reset_on_pawn_move);

  // reverseMove / applyMoveEntry
  RUN_TEST(test_position_reverse_move_simple);
  RUN_TEST(test_position_reverse_move_capture);
  RUN_TEST(test_position_reverse_move_en_passant);
  RUN_TEST(test_position_reverse_move_castling);
  RUN_TEST(test_position_reverse_move_promotion);
  RUN_TEST(test_position_reverse_move_clears_game_over);
  RUN_TEST(test_position_apply_move_entry);
  RUN_TEST(test_position_apply_move_entry_promotion);

  // King cache
  RUN_TEST(test_position_king_cache_initial);
  RUN_TEST(test_position_king_cache_after_king_move);
  RUN_TEST(test_position_king_cache_after_non_king_move);
  RUN_TEST(test_position_king_cache_after_castling);
  RUN_TEST(test_position_king_cache_after_load_fen);
  RUN_TEST(test_position_king_cache_after_reverse_move);
  RUN_TEST(test_position_king_cache_reverse_castling);

  // MoveList struct
  RUN_TEST(test_movelist_initial_state);
  RUN_TEST(test_movelist_add_and_access);
  RUN_TEST(test_movelist_clear);
  RUN_TEST(test_movelist_fills_to_capacity);
  RUN_TEST(test_movelist_used_by_get_possible_moves);

  // HashHistory struct
  RUN_TEST(test_hashhistory_initial_state);
  RUN_TEST(test_hashhistory_add_and_read);
  RUN_TEST(test_hashhistory_max_size);

  // loadFEN edge cases
  RUN_TEST(test_position_load_fen_sets_castling_rights);
  RUN_TEST(test_position_load_fen_sets_ep_target);
  RUN_TEST(test_position_load_fen_sets_clocks);

  // Board-level threefold repetition
  RUN_TEST(test_position_isRepetition_query);
  RUN_TEST(test_position_isRepetition_false);

  // reverseMove restoration
  RUN_TEST(test_position_reverse_move_restores_fen);
  RUN_TEST(test_position_reverse_move_restores_eval);

  // make / unmake (raw search interface)
  RUN_TEST(test_make_unmake_roundtrip_quiet);
  RUN_TEST(test_make_unmake_roundtrip_capture);
  RUN_TEST(test_make_unmake_roundtrip_ep);
  RUN_TEST(test_make_unmake_roundtrip_castling);
  RUN_TEST(test_make_unmake_roundtrip_queenside_castling);
  RUN_TEST(test_make_unmake_roundtrip_promotion);
  RUN_TEST(test_make_unmake_roundtrip_promotion_capture);
  RUN_TEST(test_make_hash_matches_compute);
  RUN_TEST(test_make_hash_matches_compute_capture);
  RUN_TEST(test_make_hash_matches_compute_castling);
  RUN_TEST(test_make_hash_matches_compute_ep);
  RUN_TEST(test_make_hash_matches_compute_promotion);
  RUN_TEST(test_make_updates_turn);
  RUN_TEST(test_make_updates_king_cache);
  RUN_TEST(test_make_sets_ep_after_double_push);
  RUN_TEST(test_make_clears_ep_after_normal_move);
  RUN_TEST(test_make_resets_halfmove_on_capture);
  RUN_TEST(test_make_increments_fullmove_after_black);
  RUN_TEST(test_make_unmake_sequence_multiple_moves);
  RUN_TEST(test_make_castling_revokes_castling_rights);

  // Incremental material tracking
  RUN_TEST(test_material_initial_position);
  RUN_TEST(test_material_after_capture);
  RUN_TEST(test_material_after_ep_capture);
  RUN_TEST(test_material_after_promotion);
  RUN_TEST(test_material_make_unmake_sequence);
  RUN_TEST(test_material_null_move);
  RUN_TEST(test_material_after_load_fen);

  // =======================================================================
  // Tests from former test_rules.cpp (Position static methods + rules)
  // =======================================================================
  needsDefaultKings = false;
  // ----- Check detection -----
  RUN_TEST(test_king_not_in_check_initial);
  RUN_TEST(test_king_in_check_by_rook);
  RUN_TEST(test_king_in_check_by_bishop);
  RUN_TEST(test_king_in_check_by_knight);
  RUN_TEST(test_king_in_check_by_pawn);
  RUN_TEST(test_king_in_check_by_queen);
  RUN_TEST(test_king_not_in_check_blocked);
  RUN_TEST(test_black_king_in_check);
  // ----- Checkmate -----
  RUN_TEST(test_back_rank_mate);
  RUN_TEST(test_scholars_mate);
  RUN_TEST(test_not_checkmate_can_block);
  RUN_TEST(test_not_checkmate_can_escape);
  RUN_TEST(test_not_checkmate_can_capture_attacker);
  RUN_TEST(test_smothered_mate);
  // ----- Stalemate -----
  RUN_TEST(test_stalemate_king_only);
  RUN_TEST(test_not_stalemate_has_move);
  RUN_TEST(test_stalemate_with_blocked_pawns);
  // ----- Move legality (pins, check evasion) -----
  RUN_TEST(test_king_cannot_move_into_check);
  RUN_TEST(test_pinned_piece_cannot_move);
  RUN_TEST(test_pinned_piece_can_move_along_pin);
  RUN_TEST(test_diagonal_pin);
  RUN_TEST(test_discovered_check);
  RUN_TEST(test_double_check_only_king_can_move);
  RUN_TEST(test_single_check_slider_can_block);
  RUN_TEST(test_knight_check_no_blocking);
  RUN_TEST(test_two_friendly_shielding_king_not_pinned);
  RUN_TEST(test_ep_horizontal_pin_illegal);
  RUN_TEST(test_getPossibleMoves_idempotent);
  // ----- En passant legality -----
  RUN_TEST(test_hasLegalEnPassantCapture_true);
  RUN_TEST(test_hasLegalEnPassantCapture_false_no_target);
  RUN_TEST(test_hasLegalEnPassantCapture_a_file);
  RUN_TEST(test_hasLegalEnPassantCapture_h_file);
  RUN_TEST(test_hasLegalEnPassantCapture_wrong_file);
  RUN_TEST(test_hasLegalEnPassantCapture_black_h_file);
  // ----- Square attack detection -----
  RUN_TEST(test_isSquareUnderAttack_by_pawn);
  RUN_TEST(test_isSquareUnderAttack_by_knight);
  RUN_TEST(test_isSquareUnderAttack_not_attacked);
  // ----- isValidMove direct -----
  RUN_TEST(test_isValidMove_basic_valid);
  RUN_TEST(test_isValidMove_illegal_destination);
  RUN_TEST(test_isValidMove_empty_source);
  // ----- isDraw -----
  RUN_TEST(test_rules_isDraw_insufficient);
  RUN_TEST(test_rules_isDraw_fifty_move);
  RUN_TEST(test_rules_isDraw_false);
  // ----- isGameOver -----
  RUN_TEST(test_rules_isGameOver_checkmate);
  RUN_TEST(test_rules_isGameOver_stalemate);
  RUN_TEST(test_rules_isGameOver_in_progress);
  // ----- isThreefoldRepetition -----
  RUN_TEST(test_rules_isThreefoldRepetition_direct);
  RUN_TEST(test_rules_isThreefoldRepetition_not_reached);
  // ----- Castling (needsDefaultKings = true for the rest) -----
  needsDefaultKings = true;
  RUN_TEST(test_white_kingside_castle_available);
  RUN_TEST(test_white_queenside_castle_available);
  RUN_TEST(test_black_kingside_castle_available);
  RUN_TEST(test_black_queenside_castle_available);
  RUN_TEST(test_castle_blocked_by_piece);
  RUN_TEST(test_castle_through_check_forbidden);
  RUN_TEST(test_castle_while_in_check_forbidden);
  RUN_TEST(test_castle_destination_under_attack);
  RUN_TEST(test_no_castle_right_revoked);
  RUN_TEST(test_queenside_blocked_b1);
  RUN_TEST(test_partial_castling_rights_kingside_only);
  // ----- En passant -----
  RUN_TEST(test_en_passant_white_captures);
  RUN_TEST(test_en_passant_black_captures);
  RUN_TEST(test_en_passant_not_available);
  RUN_TEST(test_ep_capture_leaves_king_in_check);
  RUN_TEST(test_ep_a_file_boundary);
  RUN_TEST(test_ep_h_file_boundary);
  // ----- Helpers -----
  RUN_TEST(test_isEnPassantMove_true);
  RUN_TEST(test_isEnPassantMove_false_normal_capture);
  RUN_TEST(test_isEnPassantMove_non_pawn);
  RUN_TEST(test_isCastlingMove_true);
  RUN_TEST(test_isCastlingMove_false);
  RUN_TEST(test_isCastlingMove_non_king);
  RUN_TEST(test_getEnPassantCapturedPawnSq);
}
