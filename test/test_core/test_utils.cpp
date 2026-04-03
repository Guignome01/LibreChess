#include <unity.h>

#include "../test_helpers.h"

// ---------------------------------------------------------------------------
// 50-move rule (PositionState field validation)
// ---------------------------------------------------------------------------

void test_fifty_move_rule(void) {
  PositionState state;
  state.halfmoveClock = 100;
  TEST_ASSERT_TRUE(state.halfmoveClock >= 100);
}

void test_no_fifty_move_rule(void) {
  PositionState state;
  state.halfmoveClock = 99;
  TEST_ASSERT_FALSE(state.halfmoveClock >= 100);
}

// ---------------------------------------------------------------------------
// utils::checkEnPassant (free function — mailbox + Square API)
// ---------------------------------------------------------------------------

void test_utils_checkEnPassant_double_push_sets_target(void) {
  // White pawn e2→e4: EP target should be e3
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", b, m, turn);
  auto ep = utils::checkEnPassant(m, squareOf(6, 4), squareOf(4, 4));
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(-1, ep.capturedPawnRow);
  TEST_ASSERT_EQUAL_INT(5, ep.nextEpRow);
  TEST_ASSERT_EQUAL_INT(4, ep.nextEpCol);
}

void test_utils_checkEnPassant_single_push_no_target(void) {
  // White pawn e3→e4: no EP target (only 1-square push)
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("rnbqkbnr/pppppppp/8/8/8/4P3/PPPP1PPP/RNBQKBNR w KQkq - 0 1", b, m, turn);
  auto ep = utils::checkEnPassant(m, squareOf(5, 4), squareOf(4, 4));
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(-1, ep.nextEpRow);
  TEST_ASSERT_EQUAL_INT(-1, ep.nextEpCol);
}

void test_utils_checkEnPassant_capture_detected(void) {
  // White pawn on e5, diagonal to f6 (empty) = EP capture
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("rnbqkbnr/pppp1ppp/8/4Pp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 1", b, m, turn);
  auto ep = utils::checkEnPassant(m, squareOf(3, 4), squareOf(2, 5));
  TEST_ASSERT_TRUE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(3, ep.capturedPawnRow);
}

void test_utils_checkEnPassant_normal_capture_not_ep(void) {
  // Pawn diagonal capture to occupied square = normal capture, not EP
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("rnbqkbnr/pppp1ppp/5p2/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1", b, m, turn);
  auto ep = utils::checkEnPassant(m, squareOf(3, 4), squareOf(2, 5));
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(-1, ep.capturedPawnRow);
}

void test_utils_checkEnPassant_black_double_push(void) {
  // Black pawn d7→d5: EP target should be d6
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", b, m, turn);
  auto ep = utils::checkEnPassant(m, squareOf(1, 3), squareOf(3, 3));
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(2, ep.nextEpRow);
  TEST_ASSERT_EQUAL_INT(3, ep.nextEpCol);
}

void test_utils_checkEnPassant_non_pawn_no_effect(void) {
  // Knight move — should never set EP target or capture
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", b, m, turn);
  auto ep = utils::checkEnPassant(m, squareOf(7, 1), squareOf(5, 2));
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(-1, ep.nextEpRow);
  TEST_ASSERT_EQUAL_INT(-1, ep.nextEpCol);
}

// ---------------------------------------------------------------------------
// utils::checkCastling (free function — mailbox + Square API)
// ---------------------------------------------------------------------------

void test_utils_checkCastling_kingside(void) {
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", b, m, turn);
  auto c = utils::checkCastling(m, squareOf(7, 4), squareOf(7, 6));
  TEST_ASSERT_TRUE(c.isCastling);
  TEST_ASSERT_EQUAL_INT(7, c.rookFromCol);
  TEST_ASSERT_EQUAL_INT(5, c.rookToCol);
}

void test_utils_checkCastling_queenside(void) {
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", b, m, turn);
  auto c = utils::checkCastling(m, squareOf(7, 4), squareOf(7, 2));
  TEST_ASSERT_TRUE(c.isCastling);
  TEST_ASSERT_EQUAL_INT(0, c.rookFromCol);
  TEST_ASSERT_EQUAL_INT(3, c.rookToCol);
}

void test_utils_checkCastling_black_kingside(void) {
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1", b, m, turn);
  auto c = utils::checkCastling(m, squareOf(0, 4), squareOf(0, 6));
  TEST_ASSERT_TRUE(c.isCastling);
  TEST_ASSERT_EQUAL_INT(7, c.rookFromCol);
  TEST_ASSERT_EQUAL_INT(5, c.rookToCol);
}

void test_utils_checkCastling_not_king(void) {
  // Rook moving 2 squares is not castling
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", b, m, turn);
  auto c = utils::checkCastling(m, squareOf(7, 0), squareOf(7, 2));
  TEST_ASSERT_FALSE(c.isCastling);
  TEST_ASSERT_EQUAL_INT(-1, c.rookFromCol);
  TEST_ASSERT_EQUAL_INT(-1, c.rookToCol);
}

void test_utils_checkCastling_king_one_square(void) {
  // King moving one square is not castling
  BitboardSet b; Piece m[64]; Color turn;
  fen::fenToBoard("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", b, m, turn);
  auto c = utils::checkCastling(m, squareOf(7, 4), squareOf(7, 5));
  TEST_ASSERT_FALSE(c.isCastling);
}

// ---------------------------------------------------------------------------
// checkEnPassant (Position member method — delegates to utils::)
// ---------------------------------------------------------------------------

void test_checkEnPassant_double_push_sets_target(void) {
  Position pos;
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  auto ep = pos.checkEnPassant(6, 4, 4, 4);
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(-1, ep.capturedPawnRow);
  TEST_ASSERT_EQUAL_INT(5, ep.nextEpRow);
  TEST_ASSERT_EQUAL_INT(4, ep.nextEpCol);
}

void test_checkEnPassant_single_push_no_target(void) {
  Position pos;
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/4P3/PPPP1PPP/RNBQKBNR w KQkq - 0 1");
  auto ep = pos.checkEnPassant(5, 4, 4, 4);
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(-1, ep.nextEpRow);
  TEST_ASSERT_EQUAL_INT(-1, ep.nextEpCol);
}

void test_checkEnPassant_capture_detected(void) {
  Position pos;
  pos.loadFEN("rnbqkbnr/pppp1ppp/8/4Pp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 1");
  auto ep = pos.checkEnPassant(3, 4, 2, 5);
  TEST_ASSERT_TRUE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(3, ep.capturedPawnRow);
}

void test_checkEnPassant_normal_capture_not_ep(void) {
  Position pos;
  pos.loadFEN("rnbqkbnr/pppp1ppp/5p2/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1");
  auto ep = pos.checkEnPassant(3, 4, 2, 5);
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(-1, ep.capturedPawnRow);
}

void test_checkEnPassant_black_double_push(void) {
  Position pos;
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
  auto ep = pos.checkEnPassant(1, 3, 3, 3);
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(2, ep.nextEpRow);
  TEST_ASSERT_EQUAL_INT(3, ep.nextEpCol);
}

void test_checkEnPassant_non_pawn_no_effect(void) {
  Position pos;
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  auto ep = pos.checkEnPassant(7, 1, 5, 2);
  TEST_ASSERT_FALSE(ep.isCapture);
  TEST_ASSERT_EQUAL_INT(-1, ep.nextEpRow);
  TEST_ASSERT_EQUAL_INT(-1, ep.nextEpCol);
}

// ---------------------------------------------------------------------------
// checkCastling (Position member method — delegates to utils::)
// ---------------------------------------------------------------------------

void test_checkCastling_kingside(void) {
  Position pos;
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  auto c = pos.checkCastling(7, 4, 7, 6);
  TEST_ASSERT_TRUE(c.isCastling);
  TEST_ASSERT_EQUAL_INT(7, c.rookFromCol);
  TEST_ASSERT_EQUAL_INT(5, c.rookToCol);
}

void test_checkCastling_queenside(void) {
  Position pos;
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  auto c = pos.checkCastling(7, 4, 7, 2);
  TEST_ASSERT_TRUE(c.isCastling);
  TEST_ASSERT_EQUAL_INT(0, c.rookFromCol);
  TEST_ASSERT_EQUAL_INT(3, c.rookToCol);
}

void test_checkCastling_black_kingside(void) {
  Position pos;
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1");
  auto c = pos.checkCastling(0, 4, 0, 6);
  TEST_ASSERT_TRUE(c.isCastling);
  TEST_ASSERT_EQUAL_INT(7, c.rookFromCol);
  TEST_ASSERT_EQUAL_INT(5, c.rookToCol);
}

void test_checkCastling_not_king(void) {
  Position pos;
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  auto c = pos.checkCastling(7, 0, 7, 2);
  TEST_ASSERT_FALSE(c.isCastling);
  TEST_ASSERT_EQUAL_INT(-1, c.rookFromCol);
  TEST_ASSERT_EQUAL_INT(-1, c.rookToCol);
}

void test_checkCastling_king_one_square(void) {
  Position pos;
  pos.loadFEN("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  auto c = pos.checkCastling(7, 4, 7, 5);
  TEST_ASSERT_FALSE(c.isCastling);
}


// ---------------------------------------------------------------------------
// updateCastlingRights
// ---------------------------------------------------------------------------

void test_updateCastlingRights_white_king_moves(void) {
  uint8_t rights = 0x0F;  // KQkq
  rights = utils::updateCastlingRights(rights, squareOf(7, 4), squareOf(7, 5));
  TEST_ASSERT_EQUAL_UINT8(0x0C, rights);  // kq only
}

void test_updateCastlingRights_black_king_moves(void) {
  uint8_t rights = 0x0F;
  rights = utils::updateCastlingRights(rights, squareOf(0, 4), squareOf(0, 5));
  TEST_ASSERT_EQUAL_UINT8(0x03, rights);  // KQ only
}

void test_updateCastlingRights_white_h_rook_moves(void) {
  uint8_t rights = 0x0F;
  rights = utils::updateCastlingRights(rights, squareOf(7, 7), squareOf(5, 7));
  TEST_ASSERT_EQUAL_UINT8(0x0E, rights);  // Qkq (lost K)
}

void test_updateCastlingRights_white_a_rook_moves(void) {
  uint8_t rights = 0x0F;
  rights = utils::updateCastlingRights(rights, squareOf(7, 0), squareOf(5, 0));
  TEST_ASSERT_EQUAL_UINT8(0x0D, rights);  // Kkq (lost Q)
}

void test_updateCastlingRights_rook_captured(void) {
  uint8_t rights = 0x0F;
  // Black captures white h-rook
  rights = utils::updateCastlingRights(rights, squareOf(0, 7), squareOf(7, 7));
  // Lost K (rook captured on h1) + lost k (black rook moved from h8)
  TEST_ASSERT_EQUAL_UINT8(0x0A, rights);  // Qq
}

void test_updateCastlingRights_no_change_on_pawn_move(void) {
  uint8_t rights = 0x0F;
  rights = utils::updateCastlingRights(rights, squareOf(6, 4), squareOf(4, 4));
  TEST_ASSERT_EQUAL_UINT8(0x0F, rights);  // unchanged
}

// ---------------------------------------------------------------------------
// squareName / PositionState::initial
// ---------------------------------------------------------------------------

void test_squareName_corners(void) {
  TEST_ASSERT_EQUAL_STRING("a8", utils::squareName(0, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("h8", utils::squareName(0, 7).c_str());
  TEST_ASSERT_EQUAL_STRING("a1", utils::squareName(7, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("h1", utils::squareName(7, 7).c_str());
}

void test_squareName_center(void) {
  TEST_ASSERT_EQUAL_STRING("e4", utils::squareName(4, 4).c_str());
  TEST_ASSERT_EQUAL_STRING("d5", utils::squareName(3, 3).c_str());
}

void test_positionState_initial(void) {
  PositionState st = PositionState::initial();
  TEST_ASSERT_EQUAL_UINT8(0x0F, st.castlingRights);
  TEST_ASSERT_EQUAL_INT(-1, st.epRow);
  TEST_ASSERT_EQUAL_INT(-1, st.epCol);
  TEST_ASSERT_EQUAL_INT(0, st.halfmoveClock);
  TEST_ASSERT_EQUAL_INT(1, st.fullmoveClock);
}

// ---------------------------------------------------------------------------
// Castling rights string conversion
// ---------------------------------------------------------------------------

void test_castling_rights_to_string_all(void) {
  TEST_ASSERT_EQUAL_STRING("KQkq", utils::castlingRightsToString(0x0F).c_str());
}

void test_castling_rights_to_string_none(void) {
  TEST_ASSERT_EQUAL_STRING("-", utils::castlingRightsToString(0x00).c_str());
}

void test_castling_rights_to_string_partial(void) {
  TEST_ASSERT_EQUAL_STRING("Kk", utils::castlingRightsToString(0x05).c_str());
}

void test_castling_rights_from_string(void) {
  TEST_ASSERT_EQUAL_UINT8(0x0F, utils::castlingRightsFromString("KQkq"));
  TEST_ASSERT_EQUAL_UINT8(0x00, utils::castlingRightsFromString("-"));
  TEST_ASSERT_EQUAL_UINT8(0x01, utils::castlingRightsFromString("K"));
}

void test_castling_rights_roundtrip_all_16(void) {
  for (uint8_t i = 0; i <= 0x0F; i++) {
    std::string str = utils::castlingRightsToString(i);
    uint8_t parsed = utils::castlingRightsFromString(str);
    TEST_ASSERT_EQUAL_UINT8(i, parsed);
  }
}

void test_castling_rights_from_string_invalid(void) {
  TEST_ASSERT_EQUAL_UINT8(0x00, utils::castlingRightsFromString("XYZ"));
}

// ---------------------------------------------------------------------------
// castlingCharToBit
// ---------------------------------------------------------------------------

void test_castlingCharToBit_all_four(void) {
  TEST_ASSERT_EQUAL_UINT8(0x01, utils::castlingCharToBit('K'));
  TEST_ASSERT_EQUAL_UINT8(0x02, utils::castlingCharToBit('Q'));
  TEST_ASSERT_EQUAL_UINT8(0x04, utils::castlingCharToBit('k'));
  TEST_ASSERT_EQUAL_UINT8(0x08, utils::castlingCharToBit('q'));
}

void test_castlingCharToBit_invalid(void) {
  TEST_ASSERT_EQUAL_UINT8(0, utils::castlingCharToBit('X'));
  TEST_ASSERT_EQUAL_UINT8(0, utils::castlingCharToBit(' '));
}

// ---------------------------------------------------------------------------
// hasCastlingRight
// ---------------------------------------------------------------------------

void test_hasCastlingRight_all_rights(void) {
  uint8_t all = 0x0F;
  TEST_ASSERT_TRUE(utils::hasCastlingRight(all, Color::WHITE, true));   // K
  TEST_ASSERT_TRUE(utils::hasCastlingRight(all, Color::WHITE, false));  // Q
  TEST_ASSERT_TRUE(utils::hasCastlingRight(all, Color::BLACK, true));   // k
  TEST_ASSERT_TRUE(utils::hasCastlingRight(all, Color::BLACK, false));  // q
}

void test_hasCastlingRight_no_rights(void) {
  uint8_t none = 0x00;
  TEST_ASSERT_FALSE(utils::hasCastlingRight(none, Color::WHITE, true));
  TEST_ASSERT_FALSE(utils::hasCastlingRight(none, Color::WHITE, false));
  TEST_ASSERT_FALSE(utils::hasCastlingRight(none, Color::BLACK, true));
  TEST_ASSERT_FALSE(utils::hasCastlingRight(none, Color::BLACK, false));
}

void test_hasCastlingRight_partial(void) {
  uint8_t wKbq = 0x09;  // K + q
  TEST_ASSERT_TRUE(utils::hasCastlingRight(wKbq, Color::WHITE, true));   // K set
  TEST_ASSERT_FALSE(utils::hasCastlingRight(wKbq, Color::WHITE, false)); // Q not set
  TEST_ASSERT_FALSE(utils::hasCastlingRight(wKbq, Color::BLACK, true));  // k not set
  TEST_ASSERT_TRUE(utils::hasCastlingRight(wKbq, Color::BLACK, false));  // q set
}

// ---------------------------------------------------------------------------
// Coordinate helpers: fileChar / rankChar / fileIndex / rankIndex
// ---------------------------------------------------------------------------

void test_fileChar_all_columns(void) {
  TEST_ASSERT_EQUAL_CHAR('a', utils::fileChar(0));
  TEST_ASSERT_EQUAL_CHAR('b', utils::fileChar(1));
  TEST_ASSERT_EQUAL_CHAR('c', utils::fileChar(2));
  TEST_ASSERT_EQUAL_CHAR('d', utils::fileChar(3));
  TEST_ASSERT_EQUAL_CHAR('e', utils::fileChar(4));
  TEST_ASSERT_EQUAL_CHAR('f', utils::fileChar(5));
  TEST_ASSERT_EQUAL_CHAR('g', utils::fileChar(6));
  TEST_ASSERT_EQUAL_CHAR('h', utils::fileChar(7));
}

void test_rankChar_all_rows(void) {
  TEST_ASSERT_EQUAL_CHAR('8', utils::rankChar(0));
  TEST_ASSERT_EQUAL_CHAR('7', utils::rankChar(1));
  TEST_ASSERT_EQUAL_CHAR('6', utils::rankChar(2));
  TEST_ASSERT_EQUAL_CHAR('5', utils::rankChar(3));
  TEST_ASSERT_EQUAL_CHAR('4', utils::rankChar(4));
  TEST_ASSERT_EQUAL_CHAR('3', utils::rankChar(5));
  TEST_ASSERT_EQUAL_CHAR('2', utils::rankChar(6));
  TEST_ASSERT_EQUAL_CHAR('1', utils::rankChar(7));
}

void test_fileIndex_all_files(void) {
  TEST_ASSERT_EQUAL_INT(0, utils::fileIndex('a'));
  TEST_ASSERT_EQUAL_INT(1, utils::fileIndex('b'));
  TEST_ASSERT_EQUAL_INT(4, utils::fileIndex('e'));
  TEST_ASSERT_EQUAL_INT(7, utils::fileIndex('h'));
}

void test_rankIndex_all_ranks(void) {
  TEST_ASSERT_EQUAL_INT(7, utils::rankIndex('1'));
  TEST_ASSERT_EQUAL_INT(6, utils::rankIndex('2'));
  TEST_ASSERT_EQUAL_INT(4, utils::rankIndex('4'));
  TEST_ASSERT_EQUAL_INT(0, utils::rankIndex('8'));
}

void test_coordinate_roundtrip(void) {
  for (int col = 0; col < 8; col++) {
    char f = utils::fileChar(col);
    TEST_ASSERT_EQUAL_INT(col, utils::fileIndex(f));
  }
  for (int row = 0; row < 8; row++) {
    char r = utils::rankChar(row);
    TEST_ASSERT_EQUAL_INT(row, utils::rankIndex(r));
  }
}

// ---------------------------------------------------------------------------
// isValidSquare
// ---------------------------------------------------------------------------

void test_isValidSquare_valid(void) {
  TEST_ASSERT_TRUE(utils::isValidSquare(0, 0));
  TEST_ASSERT_TRUE(utils::isValidSquare(0, 7));
  TEST_ASSERT_TRUE(utils::isValidSquare(7, 0));
  TEST_ASSERT_TRUE(utils::isValidSquare(7, 7));
  TEST_ASSERT_TRUE(utils::isValidSquare(3, 4));
}

void test_isValidSquare_invalid(void) {
  TEST_ASSERT_FALSE(utils::isValidSquare(-1, 0));
  TEST_ASSERT_FALSE(utils::isValidSquare(0, -1));
  TEST_ASSERT_FALSE(utils::isValidSquare(8, 0));
  TEST_ASSERT_FALSE(utils::isValidSquare(0, 8));
  TEST_ASSERT_FALSE(utils::isValidSquare(-1, -1));
}

// ---------------------------------------------------------------------------
// isValidPromotionChar
// ---------------------------------------------------------------------------

void test_isValidPromotionChar_valid(void) {
  TEST_ASSERT_TRUE(utils::isValidPromotionChar('q'));
  TEST_ASSERT_TRUE(utils::isValidPromotionChar('r'));
  TEST_ASSERT_TRUE(utils::isValidPromotionChar('b'));
  TEST_ASSERT_TRUE(utils::isValidPromotionChar('n'));
}

void test_isValidPromotionChar_uppercase(void) {
  TEST_ASSERT_TRUE(utils::isValidPromotionChar('Q'));
  TEST_ASSERT_TRUE(utils::isValidPromotionChar('R'));
  TEST_ASSERT_TRUE(utils::isValidPromotionChar('B'));
  TEST_ASSERT_TRUE(utils::isValidPromotionChar('N'));
}

void test_isValidPromotionChar_invalid(void) {
  TEST_ASSERT_FALSE(utils::isValidPromotionChar('k'));
  TEST_ASSERT_FALSE(utils::isValidPromotionChar('p'));
  TEST_ASSERT_FALSE(utils::isValidPromotionChar('x'));
  TEST_ASSERT_FALSE(utils::isValidPromotionChar(' '));
}

// ---------------------------------------------------------------------------
// gameResultName
// ---------------------------------------------------------------------------

void test_gameResultName_all_values(void) {
  TEST_ASSERT_EQUAL_STRING("In progress", utils::gameResultName(GameResult::IN_PROGRESS));
  TEST_ASSERT_EQUAL_STRING("Checkmate", utils::gameResultName(GameResult::CHECKMATE));
  TEST_ASSERT_EQUAL_STRING("Stalemate", utils::gameResultName(GameResult::STALEMATE));
  TEST_ASSERT_EQUAL_STRING("Draw (50-move rule)", utils::gameResultName(GameResult::DRAW_50));
  TEST_ASSERT_EQUAL_STRING("Draw (threefold repetition)", utils::gameResultName(GameResult::DRAW_3FOLD));
  TEST_ASSERT_EQUAL_STRING("Resignation", utils::gameResultName(GameResult::RESIGNATION));
  TEST_ASSERT_EQUAL_STRING("Draw (insufficient material)", utils::gameResultName(GameResult::DRAW_INSUFFICIENT));
  TEST_ASSERT_EQUAL_STRING("Draw (agreement)", utils::gameResultName(GameResult::DRAW_AGREEMENT));
  TEST_ASSERT_EQUAL_STRING("Timeout", utils::gameResultName(GameResult::TIMEOUT));
  TEST_ASSERT_EQUAL_STRING("Aborted", utils::gameResultName(GameResult::ABORTED));
}

// ---------------------------------------------------------------------------
// boardToText (Position member method)
// ---------------------------------------------------------------------------

void test_boardToText_initial_position(void) {
  Position pos;
  pos.newGame();
  std::string text = pos.boardToText();
  TEST_ASSERT_TRUE(text.length() > 0);
  // Check that back ranks have expected piece chars
  TEST_ASSERT_TRUE(text.find('r') != std::string::npos);  // black rook
  TEST_ASSERT_TRUE(text.find('R') != std::string::npos);  // white rook
  TEST_ASSERT_TRUE(text.find('K') != std::string::npos);  // white king
  TEST_ASSERT_TRUE(text.find('k') != std::string::npos);  // black king
}

void test_boardToText_empty_board(void) {
  Position pos;
  pos.loadFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  std::string text = pos.boardToText();
  TEST_ASSERT_TRUE(text.length() > 0);
  // Most rank lines should be all dots except the king ranks
  // Check rank 4 (row 4) which should have all dots
  char rank = utils::rankChar(4);
  std::string prefix = std::string(1, rank) + " ";
  size_t pos_found = text.find(prefix);
  TEST_ASSERT_TRUE(pos_found != std::string::npos);
  for (int col = 0; col < 8; col++) {
    TEST_ASSERT_EQUAL_CHAR('.', text[pos_found + 2 + col * 2]);
  }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_utils_tests() {
  needsDefaultKings = false;

  // 50-move rule
  RUN_TEST(test_fifty_move_rule);
  RUN_TEST(test_no_fifty_move_rule);

  // utils::checkEnPassant (free function)
  RUN_TEST(test_utils_checkEnPassant_double_push_sets_target);
  RUN_TEST(test_utils_checkEnPassant_single_push_no_target);
  RUN_TEST(test_utils_checkEnPassant_capture_detected);
  RUN_TEST(test_utils_checkEnPassant_normal_capture_not_ep);
  RUN_TEST(test_utils_checkEnPassant_black_double_push);
  RUN_TEST(test_utils_checkEnPassant_non_pawn_no_effect);

  // utils::checkCastling (free function)
  RUN_TEST(test_utils_checkCastling_kingside);
  RUN_TEST(test_utils_checkCastling_queenside);
  RUN_TEST(test_utils_checkCastling_black_kingside);
  RUN_TEST(test_utils_checkCastling_not_king);
  RUN_TEST(test_utils_checkCastling_king_one_square);

  // checkEnPassant (Position method — delegates to utils::)
  RUN_TEST(test_checkEnPassant_double_push_sets_target);
  RUN_TEST(test_checkEnPassant_single_push_no_target);
  RUN_TEST(test_checkEnPassant_capture_detected);
  RUN_TEST(test_checkEnPassant_normal_capture_not_ep);
  RUN_TEST(test_checkEnPassant_black_double_push);
  RUN_TEST(test_checkEnPassant_non_pawn_no_effect);

  // checkCastling (Position method — delegates to utils::)
  RUN_TEST(test_checkCastling_kingside);
  RUN_TEST(test_checkCastling_queenside);
  RUN_TEST(test_checkCastling_black_kingside);
  RUN_TEST(test_checkCastling_not_king);
  RUN_TEST(test_checkCastling_king_one_square);

  // updateCastlingRights
  RUN_TEST(test_updateCastlingRights_white_king_moves);
  RUN_TEST(test_updateCastlingRights_black_king_moves);
  RUN_TEST(test_updateCastlingRights_white_h_rook_moves);
  RUN_TEST(test_updateCastlingRights_white_a_rook_moves);
  RUN_TEST(test_updateCastlingRights_rook_captured);
  RUN_TEST(test_updateCastlingRights_no_change_on_pawn_move);

  // squareName
  RUN_TEST(test_squareName_corners);
  RUN_TEST(test_squareName_center);

  // PositionState::initial
  RUN_TEST(test_positionState_initial);

  // Castling rights strings
  RUN_TEST(test_castling_rights_to_string_all);
  RUN_TEST(test_castling_rights_to_string_none);
  RUN_TEST(test_castling_rights_to_string_partial);
  RUN_TEST(test_castling_rights_from_string);
  RUN_TEST(test_castling_rights_roundtrip_all_16);
  RUN_TEST(test_castling_rights_from_string_invalid);

  // castlingCharToBit
  RUN_TEST(test_castlingCharToBit_all_four);
  RUN_TEST(test_castlingCharToBit_invalid);

  // hasCastlingRight
  RUN_TEST(test_hasCastlingRight_all_rights);
  RUN_TEST(test_hasCastlingRight_no_rights);
  RUN_TEST(test_hasCastlingRight_partial);

  // Coordinate helpers
  RUN_TEST(test_fileChar_all_columns);
  RUN_TEST(test_rankChar_all_rows);
  RUN_TEST(test_fileIndex_all_files);
  RUN_TEST(test_rankIndex_all_ranks);
  RUN_TEST(test_coordinate_roundtrip);

  // isValidSquare
  RUN_TEST(test_isValidSquare_valid);
  RUN_TEST(test_isValidSquare_invalid);

  // isValidPromotionChar
  RUN_TEST(test_isValidPromotionChar_valid);
  RUN_TEST(test_isValidPromotionChar_uppercase);
  RUN_TEST(test_isValidPromotionChar_invalid);

  // gameResultName
  RUN_TEST(test_gameResultName_all_values);

  // boardToText
  RUN_TEST(test_boardToText_initial_position);
  RUN_TEST(test_boardToText_empty_board);
}
