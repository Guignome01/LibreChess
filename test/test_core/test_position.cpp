#include <unity.h>

#include "../test_helpers.h"
#include <position.h>
#include <evaluation.h>
#include <zobrist.h>
#include <move.h>
#include <square.h>
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
  for (int row = 0; row < 8; row++)
    for (int col = 0; col < 8; col++)
      TEST_ASSERT_ENUM_EQ(Position::INITIAL_BOARD[row][col], pos.getSquare(row, col));
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
  MoveResult r = pos.makeMove(6, 4, 4, 4); // e2e4
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FALSE(r.isCapture);
  TEST_ASSERT_FALSE(r.isCastling);
  TEST_ASSERT_FALSE(r.isEnPassant);
  TEST_ASSERT_FALSE(r.isPromotion);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pos.currentTurn());
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(6, 4)); // e2 empty
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(4, 4)); // e4 has pawn
}

void test_position_illegal_move_rejected(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(6, 4, 3, 4); // e2e5 — not legal
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn()); // turn unchanged
}

void test_position_wrong_turn_rejected(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(1, 4, 3, 4); // e7e5 — black's pawn, but it's white's turn
  TEST_ASSERT_FALSE(r.valid);
}

void test_position_empty_square_rejected(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(4, 4, 3, 4); // e4e5 — empty square
  TEST_ASSERT_FALSE(r.valid);
}

void test_position_out_of_bounds_rejected(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(-1, 0, 0, 0);
  TEST_ASSERT_FALSE(r.valid);
  r = pos.makeMove(0, 0, 8, 0);
  TEST_ASSERT_FALSE(r.valid);
}

void test_position_move_after_game_over_rejected(void) {
  setUpPosition();
  // Reach checkmate via scholar's mate, then try to move
  pos.makeMove(6, 4, 4, 4); // e4
  pos.makeMove(1, 4, 3, 4); // e5
  pos.makeMove(7, 5, 4, 2); // Bc4
  pos.makeMove(1, 0, 2, 0); // a6
  pos.makeMove(7, 3, 3, 7); // Qh5
  pos.makeMove(1, 1, 2, 1); // b6
  MoveResult r = pos.makeMove(3, 7, 1, 5); // Qxf7#
  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, r.gameResult);
  // Board no longer guards; this tests that the position is in checkmate
  TEST_ASSERT_TRUE(pos.isCheckmate());
}

// ---------------------------------------------------------------------------
// getSquare
// ---------------------------------------------------------------------------

void test_position_getSquare_returns_piece(void) {
  setUpPosition();
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(7, 0)); // a1
  TEST_ASSERT_ENUM_EQ(Piece::B_KING, pos.getSquare(0, 4)); // e8
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(4, 4)); // e4 empty
}

// ---------------------------------------------------------------------------
// invalidMoveResult
// ---------------------------------------------------------------------------

void test_position_invalidMoveResult_fields(void) {
  MoveResult r = invalidMoveResult();
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_FALSE(r.isCapture);
  TEST_ASSERT_FALSE(r.isEnPassant);
  TEST_ASSERT_EQUAL_INT(-1, r.epCapturedRow);
  TEST_ASSERT_FALSE(r.isCastling);
  TEST_ASSERT_FALSE(r.isPromotion);
  TEST_ASSERT_ENUM_EQ(Piece::NONE, r.promotedTo);
  TEST_ASSERT_FALSE(r.isCheck);
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
  MoveResult r = pos.makeMove(4, 3, 3, 4); // d4xe5
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCapture);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(3, 4)); // e5 now has white pawn
}

// ---------------------------------------------------------------------------
// En passant
// ---------------------------------------------------------------------------

void test_position_en_passant_white(void) {
  setUpPosition();
  // White pawn on e5, black just played d7d5
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
  MoveResult r = pos.makeMove(3, 4, 2, 3); // e5xd6 en passant
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isEnPassant);
  TEST_ASSERT_TRUE(r.isCapture);
  TEST_ASSERT_EQUAL_INT(3, r.epCapturedRow); // captured pawn was on row 3, col 3
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(3, 3)); // d5 cleared
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(2, 3)); // d6 has white pawn
}

void test_position_en_passant_black(void) {
  setUpPosition();
  // Black pawn on d4, white just played e2e4
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3");
  MoveResult r = pos.makeMove(4, 3, 5, 4); // d4xe3 en passant
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isEnPassant);
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(4, 4)); // e4 cleared
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, pos.getSquare(5, 4)); // e3 has black pawn
}

void test_position_ep_target_set_after_double_push(void) {
  setUpPosition();
  pos.makeMove(6, 4, 4, 4); // e2-e4 (double push)
  const PositionState& st = pos.positionState();
  TEST_ASSERT_EQUAL_INT(5, st.epRow); // EP target = e3 (row 5)
  TEST_ASSERT_EQUAL_INT(4, st.epCol); // col 4
}

void test_position_ep_target_cleared_after_other_move(void) {
  setUpPosition();
  pos.makeMove(6, 4, 4, 4); // e2-e4 sets EP target
  TEST_ASSERT_EQUAL_INT(5, pos.positionState().epRow); // EP target exists
  pos.makeMove(1, 0, 2, 0); // a7-a6 (not a double push)
  TEST_ASSERT_EQUAL_INT(-1, pos.positionState().epRow); // EP target cleared
  TEST_ASSERT_EQUAL_INT(-1, pos.positionState().epCol);
}

// ---------------------------------------------------------------------------
// Castling
// ---------------------------------------------------------------------------

void test_position_white_kingside_castle(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  MoveResult r = pos.makeMove(7, 4, 7, 6); // e1g1
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCastling);
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.getSquare(7, 6)); // king on g1
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(7, 5)); // rook on f1
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(7, 4)); // e1 empty
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(7, 7)); // h1 empty
}

void test_position_white_queenside_castle(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  MoveResult r = pos.makeMove(7, 4, 7, 2); // e1c1
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCastling);
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.getSquare(7, 2)); // king on c1
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(7, 3)); // rook on d1
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(7, 0)); // a1 empty
}

void test_position_black_kingside_castle(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1");
  MoveResult r = pos.makeMove(0, 4, 0, 6); // e8g8
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCastling);
  TEST_ASSERT_ENUM_EQ(Piece::B_KING, pos.getSquare(0, 6)); // king on g8
  TEST_ASSERT_ENUM_EQ(Piece::B_ROOK, pos.getSquare(0, 5)); // rook on f8
}

void test_position_castling_revokes_rights(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  pos.makeMove(7, 4, 7, 6); // white king-side castle
  // White castling rights revoked
  uint8_t rights = pos.getCastlingRights();
  TEST_ASSERT_EQUAL_UINT8(0, rights & 0x03); // K and Q bits cleared
  TEST_ASSERT_EQUAL_UINT8(0x0C, rights & 0x0C); // k and q bits still set
}

void test_position_rook_move_revokes_right(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  pos.makeMove(7, 7, 7, 6); // Rh1-g1 (not castling, just a rook move)
  uint8_t rights = pos.getCastlingRights();
  TEST_ASSERT_EQUAL_UINT8(0, rights & 0x01); // K right revoked
  TEST_ASSERT_EQUAL_UINT8(0x02, rights & 0x02); // Q right still present
}

void test_position_black_queenside_castle(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1");
  MoveResult r = pos.makeMove(0, 4, 0, 2); // e8c8
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCastling);
  TEST_ASSERT_ENUM_EQ(Piece::B_KING, pos.getSquare(0, 2)); // king on c8
  TEST_ASSERT_ENUM_EQ(Piece::B_ROOK, pos.getSquare(0, 3)); // rook on d8
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(0, 0)); // a8 empty
}

void test_position_rook_captured_revokes_castling(void) {
  setUpPosition();
  // Black rook at h2 can capture white rook at h1; white has K castling right
  pos.loadFEN("4k3/8/8/8/8/8/7r/4K2R b K - 0 1");
  MoveResult r = pos.makeMove(6, 7, 7, 7); // rh2 x Rh1
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCapture);
  TEST_ASSERT_EQUAL_UINT8(0, pos.getCastlingRights()); // K right revoked
}

// ---------------------------------------------------------------------------
// Promotion
// ---------------------------------------------------------------------------

void test_position_auto_queen_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/4P3/8/8/8/8/8/4K2k w - - 0 1");
  MoveResult r = pos.makeMove(1, 4, 0, 4); // e7e8 — auto-queen
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isPromotion);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, r.promotedTo);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(0, 4));
}

void test_position_knight_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/4P3/8/8/8/8/8/4K2k w - - 0 1");
  MoveResult r = pos.makeMove(1, 4, 0, 4, 'n'); // e7e8n
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isPromotion);
  TEST_ASSERT_ENUM_EQ(Piece::W_KNIGHT, r.promotedTo); // uppercase for white
  TEST_ASSERT_ENUM_EQ(Piece::W_KNIGHT, pos.getSquare(0, 4));
}

void test_position_black_promotion(void) {
  setUpPosition();
  pos.loadFEN("4K2k/8/8/8/8/8/4p3/8 b - - 0 1");
  MoveResult r = pos.makeMove(6, 4, 7, 4); // e2e1 — auto-queen
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isPromotion);
  TEST_ASSERT_ENUM_EQ(Piece::B_QUEEN, r.promotedTo); // lowercase for black
}

void test_position_promotion_with_capture(void) {
  setUpPosition();
  // White pawn on d7 captures black rook on e8 and promotes
  pos.loadFEN("4r2k/3P4/8/8/8/8/8/4K3 w - - 0 1");
  MoveResult r = pos.makeMove(1, 3, 0, 4); // d7xe8=Q
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCapture);
  TEST_ASSERT_TRUE(r.isPromotion);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, r.promotedTo);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(0, 4));
}

// ---------------------------------------------------------------------------
// Check detection
// ---------------------------------------------------------------------------

void test_position_move_gives_check(void) {
  setUpPosition();
  // White rook on a1, black king on e8 — Ra1-a8+ is check along rank 8
  pos.loadFEN("4k3/8/8/8/8/8/4K3/R7 w - - 0 1");
  MoveResult r = pos.makeMove(7, 0, 0, 0); // Ra1-a8+
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCheck);
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult); // not checkmate
}

void test_position_move_no_check(void) {
  setUpPosition();
  MoveResult r = pos.makeMove(6, 4, 4, 4); // e2e4 — not check
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FALSE(r.isCheck);
}

// ---------------------------------------------------------------------------
// Checkmate
// ---------------------------------------------------------------------------

void test_position_scholars_mate(void) {
  setUpPosition();
  pos.makeMove(6, 4, 4, 4); // e2e4
  pos.makeMove(1, 4, 3, 4); // e7e5
  pos.makeMove(7, 5, 4, 2); // Bf1-c4
  pos.makeMove(1, 0, 2, 0); // a7a6
  pos.makeMove(7, 3, 3, 7); // Qd1-h5
  pos.makeMove(1, 1, 2, 1); // b7b6
  MoveResult r = pos.makeMove(3, 7, 1, 5); // Qh5xf7# — checkmate
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(GameResult::CHECKMATE, r.gameResult);
  TEST_ASSERT_EQUAL_CHAR('w', r.winnerColor);
  TEST_ASSERT_TRUE(pos.isCheckmate());
}

void test_position_back_rank_mate(void) {
  setUpPosition();
  // White rook delivers back-rank mate
  pos.loadFEN("6k1/5ppp/8/8/8/8/8/R3K3 w Q - 0 1");
  MoveResult r = pos.makeMove(7, 0, 0, 0); // Ra1-a8#
  TEST_ASSERT_TRUE(r.valid);
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
  MoveResult r = pos.makeMove(7, 1, 2, 1); // Qb1-b6 — stalemates black king
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(GameResult::STALEMATE, r.gameResult);
  TEST_ASSERT_EQUAL_CHAR('d', r.winnerColor);
  TEST_ASSERT_TRUE(pos.isStalemate());
}

// ---------------------------------------------------------------------------
// 50-move rule
// ---------------------------------------------------------------------------

void test_position_fifty_move_draw(void) {
  setUpPosition();
  // Load position with halfmove clock at 99
  pos.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 99 50");
  MoveResult r = pos.makeMove(7, 0, 7, 1); // Ra1-b1 (non-capture, non-pawn = clock hits 100)
  TEST_ASSERT_TRUE(r.valid);
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
  MoveResult r = pos.makeMove(7, 4, 7, 3); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_INSUFFICIENT, r.gameResult);
  TEST_ASSERT_TRUE(pos.isInsufficientMaterial());
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_insufficient_material_kb_vs_k(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4KB2 w - - 0 1");
  MoveResult r = pos.makeMove(7, 4, 7, 3); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_INSUFFICIENT, r.gameResult);
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_insufficient_material_kn_vs_k(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K1N1 w - - 0 1");
  MoveResult r = pos.makeMove(7, 4, 7, 3); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_INSUFFICIENT, r.gameResult);
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_insufficient_material_kb_vs_kb_same_color(void) {
  setUpPosition();
  // Both bishops on light squares (c1=dark, d1 would be light, f1=light)
  // c8 is light square (row 0 + col 2 = even), f1 is light square (row 7 + col 5 = even)
  pos.loadFEN("2b1k3/8/8/8/8/8/8/4KB2 w - - 0 1");
  MoveResult r = pos.makeMove(7, 4, 7, 3); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_INSUFFICIENT, r.gameResult);
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_insufficient_material_kb_vs_kb_diff_color(void) {
  setUpPosition();
  // White bishop on f1 (light: 7+5=even), black bishop on c8 is light too.
  // Put black bishop on d8 (dark: 0+3=odd) for different colors.
  pos.loadFEN("3bk3/8/8/8/8/8/8/4KB2 w - - 0 1");
  MoveResult r = pos.makeMove(7, 4, 7, 3); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid);
  // Different color bishops — NOT insufficient
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_FALSE(pos.isInsufficientMaterial());
}

void test_position_sufficient_material_knn(void) {
  setUpPosition();
  // K+N+N vs K — sufficient material (checkmate is possible)
  pos.loadFEN("4k3/8/8/8/8/8/8/2N1KN2 w - - 0 1");
  MoveResult r = pos.makeMove(7, 4, 7, 3); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_FALSE(pos.isCheckmate());
}

void test_position_sufficient_material_kp_vs_k(void) {
  setUpPosition();
  // K+P vs K — sufficient material (pawn can promote)
  pos.loadFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
  MoveResult r = pos.makeMove(7, 4, 7, 3); // Ke1-d1
  TEST_ASSERT_TRUE(r.valid);
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
  pos.makeMove(6, 4, 4, 4); // e4
  pos.makeMove(1, 4, 3, 4); // e5
  pos.makeMove(7, 5, 4, 2); // Bc4
  pos.makeMove(1, 0, 2, 0); // a6
  pos.makeMove(7, 3, 3, 7); // Qh5
  pos.makeMove(1, 1, 2, 1); // b6
  MoveResult r = pos.makeMove(3, 7, 1, 5); // Qxf7#
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
  TEST_ASSERT_EQUAL_INT(-1, pos.positionState().epRow);     // no EP
  TEST_ASSERT_EQUAL_INT(4, pos.positionState().halfmoveClock);
  TEST_ASSERT_EQUAL_INT(4, pos.positionState().fullmoveClock);
  TEST_ASSERT_ENUM_EQ(Piece::W_BISHOP, pos.getSquare(4, 2)); // Bc4
  TEST_ASSERT_ENUM_EQ(Piece::B_KNIGHT, pos.getSquare(2, 2)); // Nc6
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
  pos.makeMove(6, 4, 4, 4); // e2e4
  std::string fenBefore = pos.getFen();
  // Invalid FEN should leave state unchanged
  TEST_ASSERT_FALSE(pos.loadFEN("bad_fen"));
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

  pos.makeMove(6, 4, 4, 4);  // e2e4
  std::string fen3 = pos.getFen();
  TEST_ASSERT_TRUE(fen3 != fen1);  // FEN changed after move
}

void test_position_eval_cache_consistent(void) {
  setUpPosition();
  int eval1 = eval::evaluatePosition(pos.bitboards());
  int eval2 = eval::evaluatePosition(pos.bitboards());
  TEST_ASSERT_EQUAL_INT(eval1, eval2);

  // Load asymmetric position (white missing e-pawn) and verify eval updates
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1");
  int eval3 = eval::evaluatePosition(pos.bitboards());
  // White has one fewer pawn → negative evaluation
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
  pos.makeMove(4, 3, 3, 4); // d4xe5
  int evalAfter = eval::evaluatePosition(pos.bitboards());
  TEST_ASSERT_TRUE(evalAfter > initialEval); // white gained material
}

// ---------------------------------------------------------------------------
// Position clocks (halfmove / fullmove)
// ---------------------------------------------------------------------------

void test_position_halfmove_clock_increments(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
  pos.makeMove(7, 0, 7, 1); // Ra1-b1 (non-capture, non-pawn)
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().halfmoveClock);
  pos.makeMove(0, 4, 0, 3); // Ke8-d8
  TEST_ASSERT_EQUAL_INT(2, pos.positionState().halfmoveClock);
}

void test_position_halfmove_clock_resets_on_pawn_move(void) {
  setUpPosition();
  // Start with halfmove=5 and make a pawn move
  pos.loadFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 5 10");
  TEST_ASSERT_EQUAL_INT(5, pos.positionState().halfmoveClock);
  pos.makeMove(6, 4, 4, 4); // e2-e4 (pawn move resets clock)
  TEST_ASSERT_EQUAL_INT(0, pos.positionState().halfmoveClock);
}

void test_position_fullmove_increments_after_black(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().fullmoveClock);
  pos.makeMove(6, 4, 4, 4); // e2-e4 (white moves, fullmove stays 1)
  TEST_ASSERT_EQUAL_INT(1, pos.positionState().fullmoveClock);
  pos.makeMove(0, 4, 0, 3); // Ke8-d8 (black moves, fullmove → 2)
  TEST_ASSERT_EQUAL_INT(2, pos.positionState().fullmoveClock);
}

// ---------------------------------------------------------------------------
// findPiece
// ---------------------------------------------------------------------------

void test_position_find_piece_kings_initial(void) {
  setUpPosition();
  int positions[2][2];
  int count = pos.findPiece(Piece::W_KING, positions, 2);
  TEST_ASSERT_EQUAL_INT(1, count);
  TEST_ASSERT_EQUAL_INT(7, positions[0][0]); // row 7 = rank 1
  TEST_ASSERT_EQUAL_INT(4, positions[0][1]); // col 4 = file e
}

void test_position_find_piece_black_pawns_initial(void) {
  setUpPosition();
  int positions[8][2];
  int count = pos.findPiece(Piece::B_PAWN, positions, 8);
  TEST_ASSERT_EQUAL_INT(8, count);
  // All should be on row 1 (rank 7)
  for (int i = 0; i < count; i++) {
    TEST_ASSERT_EQUAL_INT(1, positions[i][0]);
  }
}

void test_position_find_piece_not_found(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  int positions[8][2];
  int count = pos.findPiece(Piece::W_QUEEN, positions, 8);
  TEST_ASSERT_EQUAL_INT(0, count);
}

void test_position_find_piece_multiple_bishops(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/2B1B3/8/4K3 w - - 0 1");
  int positions[4][2];
  int count = pos.findPiece(Piece::W_BISHOP, positions, 4);
  TEST_ASSERT_EQUAL_INT(2, count);
}

void test_position_find_piece_max_limit(void) {
  setUpPosition();
  // 8 white pawns, but limit to 3
  int positions[3][2];
  int count = pos.findPiece(Piece::W_PAWN, positions, 3);
  TEST_ASSERT_EQUAL_INT(3, count);
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
// isStalemate (no-arg, uses current turn)
// ---------------------------------------------------------------------------

void test_position_is_stalemate_true(void) {
  setUpPosition();
  // Stalemate: black king on a8, white queen on b6, white king on c8 — black to move
  pos.loadFEN("k7/8/1Q6/8/8/8/8/2K5 b - - 0 1");
  TEST_ASSERT_TRUE(pos.isStalemate());
}

void test_position_is_stalemate_false(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.isStalemate());
}

// ---------------------------------------------------------------------------
// isInsufficientMaterial (public)
// ---------------------------------------------------------------------------

void test_position_is_insufficient_material_true(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  TEST_ASSERT_TRUE(pos.isInsufficientMaterial());
}

void test_position_is_insufficient_material_false(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.isInsufficientMaterial());
}

// ---------------------------------------------------------------------------
// isAttacked
// ---------------------------------------------------------------------------

void test_position_is_attacked_by_white(void) {
  setUpPosition();
  // White pawn on e4 attacks d5 and f5
  pos.loadFEN("4k3/8/8/8/4P3/8/8/4K3 w - - 0 1");
  TEST_ASSERT_TRUE(pos.isAttacked(3, 3, Color::WHITE));  // d5 attacked by white
  TEST_ASSERT_TRUE(pos.isAttacked(3, 5, Color::WHITE));  // f5 attacked by white
  TEST_ASSERT_FALSE(pos.isAttacked(3, 4, Color::WHITE)); // e5 not attacked by white pawn
}

void test_position_is_attacked_by_black(void) {
  setUpPosition();
  // Black knight on f6 attacks e4, g4, d5, h5, d7, h7, e8, g8
  pos.loadFEN("4k3/8/5n2/8/8/8/8/4K3 w - - 0 1");
  TEST_ASSERT_TRUE(pos.isAttacked(4, 4, Color::BLACK));  // e4 attacked by black knight
  TEST_ASSERT_TRUE(pos.isAttacked(4, 6, Color::BLACK));  // g4 attacked by black knight
  TEST_ASSERT_FALSE(pos.isAttacked(4, 5, Color::BLACK)); // f4 not attacked by black knight
}

void test_position_is_attacked_empty_square(void) {
  setUpPosition();
  // Empty square can be attacked
  pos.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
  TEST_ASSERT_TRUE(pos.isAttacked(0, 0, Color::WHITE)); // a8 attacked by Ra1 along file
}

// ---------------------------------------------------------------------------
// moveNumber
// ---------------------------------------------------------------------------

void test_position_move_number_initial(void) {
  setUpPosition();
  TEST_ASSERT_EQUAL_INT(1, pos.moveNumber());
}

void test_position_move_number_after_moves(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
  pos.makeMove(6, 4, 4, 4); // e2-e4 (white, fullmove stays 1)
  TEST_ASSERT_EQUAL_INT(1, pos.moveNumber());
  pos.makeMove(0, 4, 0, 3); // Ke8-d8 (black, fullmove → 2)
  TEST_ASSERT_EQUAL_INT(2, pos.moveNumber());
}

void test_position_move_number_from_fen(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 42");
  TEST_ASSERT_EQUAL_INT(42, pos.moveNumber());
}

// ---------------------------------------------------------------------------
// Threefold repetition (Zobrist position tracking in Position)
// ---------------------------------------------------------------------------

void test_position_threefold_repetition(void) {
  setUpPosition();
  // Position where Ke1 and Ke8 shuffle back and forth (pawns provide sufficient material)
  pos.loadFEN("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1");
  // Move 1: Ke1-d1, Ke8-d8
  pos.makeMove(7, 4, 7, 3);
  pos.makeMove(0, 4, 0, 3);
  // Move 2: Kd1-e1, Kd8-e8 (back to original — occurrence 2)
  pos.makeMove(7, 3, 7, 4);
  pos.makeMove(0, 3, 0, 4);
  // Move 3: Ke1-d1, Ke8-d8
  pos.makeMove(7, 4, 7, 3);
  pos.makeMove(0, 4, 0, 3);
  // Move 4: Kd1-e1, Kd8-e8 (back to original — occurrence 3)
  pos.makeMove(7, 3, 7, 4);
  MoveResult r = pos.makeMove(0, 3, 0, 4);  // third repetition
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_3FOLD, r.gameResult);
  TEST_ASSERT_TRUE(pos.isThreefoldRepetition());
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_threefold_different_castling_rights(void) {
  setUpPosition();
  // King move loses castling rights → initial hash differs from subsequent hashes
  pos.loadFEN("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
  pos.makeMove(7, 4, 7, 3); // Ke1-d1 (loses K castling)
  pos.makeMove(0, 4, 0, 3);
  pos.makeMove(7, 3, 7, 4);
  pos.makeMove(0, 3, 0, 4); // board same as start, but castling=0
  pos.makeMove(7, 4, 7, 3);
  pos.makeMove(0, 4, 0, 3);
  pos.makeMove(7, 3, 7, 4);
  MoveResult r = pos.makeMove(0, 3, 0, 4); // occurrence 2 (not 3)
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_FALSE(pos.isThreefoldRepetition());
}

void test_position_threefold_not_reached(void) {
  setUpPosition();
  // Only 2 occurrences — not enough for threefold
  pos.loadFEN("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1");
  pos.makeMove(7, 4, 7, 3);
  pos.makeMove(0, 4, 0, 3);
  pos.makeMove(7, 3, 7, 4);
  MoveResult r = pos.makeMove(0, 3, 0, 4); // occurrence 2
  TEST_ASSERT_ENUM_EQ(GameResult::IN_PROGRESS, r.gameResult);
  TEST_ASSERT_FALSE(pos.isThreefoldRepetition());
}

void test_position_threefold_query(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.isThreefoldRepetition());
}

void test_position_threefold_with_rook_moves(void) {
  setUpPosition();
  // Repeat rook moves to produce threefold repetition (sufficient material)
  pos.loadFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
  pos.makeMove(7, 0, 7, 1); // Ra1-b1
  pos.makeMove(0, 4, 0, 3); // Ke8-d8
  pos.makeMove(7, 1, 7, 0); // Rb1-a1
  pos.makeMove(0, 3, 0, 4); // Kd8-e8
  pos.makeMove(7, 0, 7, 1);
  pos.makeMove(0, 4, 0, 3);
  pos.makeMove(7, 1, 7, 0);
  MoveResult r = pos.makeMove(0, 3, 0, 4); // 3rd time
  TEST_ASSERT_ENUM_EQ(GameResult::DRAW_3FOLD, r.gameResult);
  TEST_ASSERT_TRUE(pos.isThreefoldRepetition());
  TEST_ASSERT_TRUE(pos.isDraw());
}

void test_position_position_history_reset_on_pawn_move(void) {
  setUpPosition();
  // Start with some moves to build position history, then a pawn move resets it
  pos.loadFEN("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1");
  pos.makeMove(7, 4, 7, 3); // Ke1-d1
  pos.makeMove(0, 4, 0, 3); // Ke8-d8
  pos.makeMove(7, 3, 7, 4); // Kd1-e1
  pos.makeMove(0, 3, 0, 4); // Kd8-e8 (occurrence 2)
  // Pawn move resets halfmove clock, which resets position history
  pos.makeMove(6, 4, 4, 4); // e2-e4
  pos.makeMove(0, 4, 0, 3); // Ke8-d8
  // Now even if positions repeat, the prior history is gone
  TEST_ASSERT_FALSE(pos.isThreefoldRepetition());
}

// ---------------------------------------------------------------------------
// reverseMove / applyMoveEntry
// ---------------------------------------------------------------------------

// Helper: build a MoveEntry from scratch
static MoveEntry makeBoardEntry(int fr, int fc, int tr, int tc, Piece piece,
                                Piece captured, const PositionState& prev,
                                Piece promo = Piece::NONE, bool ep = false,
                                int epRow = -1, bool castle = false,
                                bool check = false) {
  MoveEntry e = {};
  e.fromRow = fr; e.fromCol = fc; e.toRow = tr; e.toCol = tc;
  e.piece = piece; e.captured = captured; e.promotion = promo;
  e.isCapture = (captured != Piece::NONE);
  e.isEnPassant = ep; e.epCapturedRow = epRow;
  e.isCastling = castle;
  e.isPromotion = (promo != Piece::NONE);
  e.isCheck = check;
  e.prevState = prev;
  return e;
}

void test_position_reverse_move_simple(void) {
  setUpPosition();
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(6, 4, 4, 4);  // e2-e4
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pos.currentTurn());

  MoveEntry e = makeBoardEntry(6, 4, 4, 4, Piece::W_PAWN, Piece::NONE, before);
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(6, 4));  // pawn back on e2
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(4, 4));  // e4 empty
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_reverse_move_capture(void) {
  setUpPosition();
  pos.makeMove(6, 4, 4, 4);  // e4
  pos.makeMove(1, 3, 3, 3);  // d5
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(4, 4, 3, 3);  // exd5 capture
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCapture);

  MoveEntry e = makeBoardEntry(4, 4, 3, 3, Piece::W_PAWN, Piece::B_PAWN, before);
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(4, 4));  // pawn back on e5
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, pos.getSquare(3, 3));  // black pawn restored on d5
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_reverse_move_en_passant(void) {
  setUpPosition();
  pos.makeMove(6, 4, 4, 4);  // e4
  pos.makeMove(1, 0, 2, 0);  // a6
  pos.makeMove(4, 4, 3, 4);  // e5
  pos.makeMove(1, 3, 3, 3);  // d5
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(3, 4, 2, 3);  // exd6 en passant
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isEnPassant);

  MoveEntry e = makeBoardEntry(3, 4, 2, 3, Piece::W_PAWN, Piece::B_PAWN, before, Piece::NONE, true, 3);
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(3, 4));  // pawn back on e5
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(2, 3));  // d6 empty
  TEST_ASSERT_ENUM_EQ(Piece::B_PAWN, pos.getSquare(3, 3));  // black pawn restored on d5
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_reverse_move_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(7, 4, 7, 6);  // O-O white kingside
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isCastling);

  MoveEntry e = makeBoardEntry(7, 4, 7, 6, Piece::W_KING, Piece::NONE, before, Piece::NONE, false, -1, true);
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.getSquare(7, 4));  // king back on e1
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(7, 6));  // g1 empty
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.getSquare(7, 7));  // rook back on h1
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(7, 5));  // f1 empty
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
  // Castling rights should be restored
  TEST_ASSERT_EQUAL(0x0F, pos.positionState().castlingRights);
}

void test_position_reverse_move_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/P4k2/8/8/8/8/5K2/8 w - - 0 1");
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(1, 0, 0, 0, 'q');  // a7-a8=Q
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isPromotion);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(0, 0));

  MoveEntry e = makeBoardEntry(1, 0, 0, 0, Piece::W_PAWN, Piece::NONE, before, Piece::W_QUEEN);
  pos.reverseMove(e);

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(1, 0));  // pawn back on a7
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(0, 0));  // a8 empty
  TEST_ASSERT_ENUM_EQ(Color::WHITE, pos.currentTurn());
}

void test_position_reverse_move_clears_game_over(void) {
  setUpPosition();
  // Scholar's mate
  pos.makeMove(6, 4, 4, 4);  // e4
  pos.makeMove(1, 4, 3, 4);  // e5
  pos.makeMove(7, 5, 4, 2);  // Bc4
  pos.makeMove(1, 0, 2, 0);  // a6
  pos.makeMove(7, 3, 3, 7);  // Qh5
  pos.makeMove(1, 1, 2, 1);  // b6
  PositionState before = pos.positionState();
  MoveResult r = pos.makeMove(3, 7, 1, 5);  // Qxf7#
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
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.getSquare(4, 4));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.getSquare(6, 4));
  TEST_ASSERT_ENUM_EQ(Color::BLACK, pos.currentTurn());
}

void test_position_apply_move_entry_promotion(void) {
  setUpPosition();
  pos.loadFEN("8/P4k2/8/8/8/8/5K2/8 w - - 0 1");
  PositionState before = pos.positionState();
  MoveEntry e = makeBoardEntry(1, 0, 0, 0, Piece::W_PAWN, Piece::NONE, before, Piece::W_QUEEN);

  MoveResult r = pos.applyMoveEntry(e);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.isPromotion);
  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.getSquare(0, 0));
}

// ---------------------------------------------------------------------------
// King cache
// ---------------------------------------------------------------------------

void test_position_king_cache_initial(void) {
  setUpPosition();
  TEST_ASSERT_EQUAL_INT(7, pos.kingRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(4, pos.kingCol(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(0, pos.kingRow(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(4, pos.kingCol(Color::BLACK));
}

void test_position_king_cache_after_king_move(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  pos.makeMove(7, 4, 7, 3);  // Ke1-d1
  TEST_ASSERT_EQUAL_INT(7, pos.kingRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(3, pos.kingCol(Color::WHITE));
  // Black king unchanged
  TEST_ASSERT_EQUAL_INT(0, pos.kingRow(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(4, pos.kingCol(Color::BLACK));
}

void test_position_king_cache_after_non_king_move(void) {
  setUpPosition();
  pos.makeMove(6, 4, 4, 4);  // e2-e4 (pawn move)
  // Kings haven't moved
  TEST_ASSERT_EQUAL_INT(7, pos.kingRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(4, pos.kingCol(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(0, pos.kingRow(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(4, pos.kingCol(Color::BLACK));
}

void test_position_king_cache_after_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  pos.makeMove(7, 4, 7, 6);  // O-O white kingside
  TEST_ASSERT_EQUAL_INT(7, pos.kingRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(6, pos.kingCol(Color::WHITE));
}

void test_position_king_cache_after_load_fen(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1");
  TEST_ASSERT_EQUAL_INT(7, pos.kingRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(4, pos.kingCol(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(0, pos.kingRow(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(4, pos.kingCol(Color::BLACK));

  // Now a position with kings off-center
  pos.loadFEN("8/8/8/3k4/8/8/8/1K6 w - - 0 1");
  TEST_ASSERT_EQUAL_INT(7, pos.kingRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(1, pos.kingCol(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(3, pos.kingRow(Color::BLACK));
  TEST_ASSERT_EQUAL_INT(3, pos.kingCol(Color::BLACK));
}

void test_position_king_cache_after_reverse_move(void) {
  setUpPosition();
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  PositionState before = pos.positionState();
  pos.makeMove(7, 4, 7, 3);  // Ke1-d1
  TEST_ASSERT_EQUAL_INT(3, pos.kingCol(Color::WHITE));

  MoveEntry e = makeBoardEntry(7, 4, 7, 3, Piece::W_KING, Piece::NONE, before);
  pos.reverseMove(e);

  TEST_ASSERT_EQUAL_INT(7, pos.kingRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(4, pos.kingCol(Color::WHITE));
}

void test_position_king_cache_reverse_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  PositionState before = pos.positionState();
  pos.makeMove(7, 4, 7, 6);  // O-O
  TEST_ASSERT_EQUAL_INT(6, pos.kingCol(Color::WHITE));

  MoveEntry e = makeBoardEntry(7, 4, 7, 6, Piece::W_KING, Piece::NONE, before,
                               Piece::NONE, false, -1, true);
  pos.reverseMove(e);

  TEST_ASSERT_EQUAL_INT(7, pos.kingRow(Color::WHITE));
  TEST_ASSERT_EQUAL_INT(4, pos.kingCol(Color::WHITE));
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
  TEST_ASSERT_EQUAL_INT(3, moves.targetRow(0));
  TEST_ASSERT_EQUAL_INT(5, moves.targetCol(0));
  TEST_ASSERT_EQUAL_INT(0, moves.targetRow(1));
  TEST_ASSERT_EQUAL_INT(7, moves.targetCol(1));
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
  TEST_ASSERT_EQUAL_INT(0, moves.targetRow(0));
  TEST_ASSERT_EQUAL_INT(0, moves.targetCol(0));
}

void test_movelist_used_by_get_possible_moves(void) {
  setUpPosition();
  MoveList moves;
  pos.getPossibleMoves(6, 4, moves);  // e2 pawn: e3 and e4
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
  TEST_ASSERT_EQUAL_INT(256, HashHistory::MAX_SIZE);
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
  TEST_ASSERT_EQUAL_INT(5, st.epRow);  // e3 = row 5
  TEST_ASSERT_EQUAL_INT(4, st.epCol);  // e = col 4
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

void test_position_isThreefoldRepetition_query(void) {
  setUpPosition();
  pos.loadFEN("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1");
  // Repeat position 3 times
  pos.makeMove(7, 4, 7, 3);  // Ke1-d1
  pos.makeMove(0, 4, 0, 3);  // Ke8-d8
  pos.makeMove(7, 3, 7, 4);  // Kd1-e1
  pos.makeMove(0, 3, 0, 4);  // Kd8-e8 — 2nd occurrence
  pos.makeMove(7, 4, 7, 3);  // Ke1-d1
  pos.makeMove(0, 4, 0, 3);  // Ke8-d8
  pos.makeMove(7, 3, 7, 4);  // Kd1-e1
  pos.makeMove(0, 3, 0, 4);  // Kd8-e8 — 3rd occurrence
  TEST_ASSERT_TRUE(pos.isThreefoldRepetition());
}

void test_position_isThreefoldRepetition_false(void) {
  setUpPosition();
  TEST_ASSERT_FALSE(pos.isThreefoldRepetition());
}

// ---------------------------------------------------------------------------
// reverseMove cache restoration
// ---------------------------------------------------------------------------

void test_position_reverse_move_restores_fen(void) {
  setUpPosition();
  std::string fenBefore = pos.getFen();
  PositionState before = pos.positionState();
  pos.makeMove(6, 4, 4, 4);  // e2-e4
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
  MoveResult r = pos.makeMove(4, 4, 3, 3);  // exd5 capture
  TEST_ASSERT_TRUE(r.valid);
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
      TEST_ASSERT_ENUM_EQ(a.getSquare(row, col), b.getSquare(row, col));
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

  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.pieceOn(squareOf(3, 3)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.pieceOn(squareOf(4, 4)));

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
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.pieceOn(squareOf(2, 3)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.pieceOn(squareOf(3, 3)));  // captured pawn removed
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.pieceOn(squareOf(3, 4)));

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
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.pieceOn(squareOf(7, 6)));
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.pieceOn(squareOf(7, 5)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.pieceOn(squareOf(7, 4)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.pieceOn(squareOf(7, 7)));

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
  TEST_ASSERT_ENUM_EQ(Piece::W_KING, pos.pieceOn(squareOf(7, 2)));
  TEST_ASSERT_ENUM_EQ(Piece::W_ROOK, pos.pieceOn(squareOf(7, 3)));

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

  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.pieceOn(squareOf(0, 0)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.pieceOn(squareOf(1, 0)));

  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
  TEST_ASSERT_EQUAL_UINT64(hashBefore, pos.hash());
  // Unmake should restore pawn, not queen
  TEST_ASSERT_ENUM_EQ(Piece::W_PAWN, pos.pieceOn(squareOf(1, 0)));
  TEST_ASSERT_ENUM_EQ(Piece::NONE, pos.pieceOn(squareOf(0, 0)));
}

void test_make_unmake_roundtrip_promotion_capture(void) {
  setUpPosition();
  pos.loadFEN("1n6/P4k2/8/8/8/8/5K2/8 w - - 0 1");
  std::string fenBefore = pos.getFen();
  uint64_t hashBefore = pos.hash();

  uint8_t queenPromo = Move::promoFlags(Move::promoIndexFromType(PieceType::QUEEN));
  Move m(squareOf(1, 0), squareOf(0, 1), MOVE_CAPTURE | queenPromo);  // axb8=Q
  UndoInfo undo = pos.make(m);

  TEST_ASSERT_ENUM_EQ(Piece::W_QUEEN, pos.pieceOn(squareOf(0, 1)));

  pos.unmake(m, undo);
  TEST_ASSERT_EQUAL_STRING(fenBefore.c_str(), pos.getFen().c_str());
  TEST_ASSERT_EQUAL_UINT64(hashBefore, pos.hash());
  // Knight should be restored
  TEST_ASSERT_ENUM_EQ(Piece::B_KNIGHT, pos.pieceOn(squareOf(0, 1)));
}

void test_make_hash_matches_compute(void) {
  setUpPosition();
  Move m(squareOf(6, 4), squareOf(4, 4));  // e2-e4
  pos.make(m);

  uint64_t incremental = pos.hash();
  uint64_t computed = zobrist::computeHash(
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState());
  TEST_ASSERT_EQUAL_UINT64(computed, incremental);
}

void test_make_hash_matches_compute_capture(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");

  Move m(squareOf(4, 4), squareOf(3, 3), MOVE_CAPTURE);  // exd5
  pos.make(m);

  uint64_t incremental = pos.hash();
  uint64_t computed = zobrist::computeHash(
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState());
  TEST_ASSERT_EQUAL_UINT64(computed, incremental);
}

void test_make_hash_matches_compute_castling(void) {
  setUpPosition();
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");

  Move m(squareOf(7, 4), squareOf(7, 6), MOVE_CASTLING);  // O-O
  pos.make(m);

  uint64_t incremental = pos.hash();
  uint64_t computed = zobrist::computeHash(
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState());
  TEST_ASSERT_EQUAL_UINT64(computed, incremental);
}

void test_make_hash_matches_compute_ep(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");

  Move m(squareOf(3, 4), squareOf(2, 3), MOVE_CAPTURE | MOVE_EP);  // exd6 EP
  pos.make(m);

  uint64_t incremental = pos.hash();
  uint64_t computed = zobrist::computeHash(
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState());
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
      pos.bitboards(), pos.mailbox(), pos.currentTurn(), pos.positionState());
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
  TEST_ASSERT_EQUAL_INT(squareOf(7, 4), pos.kingSquare(Color::WHITE));

  Move m(squareOf(7, 4), squareOf(6, 4));  // Ke1-e2
  pos.make(m);
  TEST_ASSERT_EQUAL_INT(squareOf(6, 4), pos.kingSquare(Color::WHITE));
}

void test_make_sets_ep_after_double_push(void) {
  setUpPosition();
  Move m(squareOf(6, 4), squareOf(4, 4));  // e2-e4
  pos.make(m);
  TEST_ASSERT_EQUAL_INT(5, pos.positionState().epRow);
  TEST_ASSERT_EQUAL_INT(4, pos.positionState().epCol);
}

void test_make_clears_ep_after_normal_move(void) {
  setUpPosition();
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
  // Black plays Nf6, should clear EP
  Move m(squareOf(0, 6), squareOf(2, 5));  // Ng8-f6
  pos.make(m);
  TEST_ASSERT_EQUAL_INT(-1, pos.positionState().epRow);
  TEST_ASSERT_EQUAL_INT(-1, pos.positionState().epCol);
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
  TEST_ASSERT_EQUAL_INT(1, pos.moveNumber());

  Move e4(squareOf(6, 4), squareOf(4, 4));
  pos.make(e4);
  TEST_ASSERT_EQUAL_INT(1, pos.moveNumber());  // still 1 after White

  Move e5(squareOf(1, 4), squareOf(3, 4));
  pos.make(e5);
  TEST_ASSERT_EQUAL_INT(2, pos.moveNumber());  // increments after Black
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
// Registration
// ---------------------------------------------------------------------------

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

  // FEN/eval cache
  RUN_TEST(test_position_fen_cache_consistent);
  RUN_TEST(test_position_eval_cache_consistent);
  RUN_TEST(test_position_end_game_preserves_fen);
  RUN_TEST(test_position_eval_after_capture);

  // Position clocks
  RUN_TEST(test_position_halfmove_clock_increments);
  RUN_TEST(test_position_halfmove_clock_resets_on_pawn_move);
  RUN_TEST(test_position_fullmove_increments_after_black);

  // findPiece
  RUN_TEST(test_position_find_piece_kings_initial);
  RUN_TEST(test_position_find_piece_black_pawns_initial);
  RUN_TEST(test_position_find_piece_not_found);
  RUN_TEST(test_position_find_piece_multiple_bishops);
  RUN_TEST(test_position_find_piece_max_limit);

  // inCheck (no-arg)
  RUN_TEST(test_position_in_check_true);
  RUN_TEST(test_position_in_check_false);

  // isCheckmate (no-arg)
  RUN_TEST(test_position_is_checkmate_true);
  RUN_TEST(test_position_is_checkmate_false);

  // isStalemate (no-arg)
  RUN_TEST(test_position_is_stalemate_true);
  RUN_TEST(test_position_is_stalemate_false);

  // isInsufficientMaterial (public)
  RUN_TEST(test_position_is_insufficient_material_true);
  RUN_TEST(test_position_is_insufficient_material_false);

  // isAttacked
  RUN_TEST(test_position_is_attacked_by_white);
  RUN_TEST(test_position_is_attacked_by_black);
  RUN_TEST(test_position_is_attacked_empty_square);

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
  RUN_TEST(test_position_isThreefoldRepetition_query);
  RUN_TEST(test_position_isThreefoldRepetition_false);

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
}
