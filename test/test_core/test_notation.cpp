#include <unity.h>

#include "../test_helpers.h"

// ===========================================================================
// toCoordinate
// ===========================================================================

void test_toCoordinate_basic(void) {
  std::string s = notation::toCoordinate(makeSquare(1, 4), makeSquare(3, 4));  // e2e4
  TEST_ASSERT_EQUAL_STRING("e2e4", s.c_str());
}

void test_toCoordinate_with_promotion(void) {
  std::string s = notation::toCoordinate(makeSquare(6, 4), makeSquare(7, 4), 'q');  // e7e8q
  TEST_ASSERT_EQUAL_STRING("e7e8q", s.c_str());
}

void test_toCoordinate_all_promo_chars(void) {
  TEST_ASSERT_EQUAL_STRING("e7e8r", notation::toCoordinate(makeSquare(6, 4), makeSquare(7, 4), 'r').c_str());
  TEST_ASSERT_EQUAL_STRING("e7e8b", notation::toCoordinate(makeSquare(6, 4), makeSquare(7, 4), 'b').c_str());
  TEST_ASSERT_EQUAL_STRING("e7e8n", notation::toCoordinate(makeSquare(6, 4), makeSquare(7, 4), 'n').c_str());
}

// ===========================================================================
// parseCoordinate
// ===========================================================================

void test_parseCoordinate_basic(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseCoordinate("e2e4", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(1, 4), from);  // e2
  TEST_ASSERT_EQUAL_UINT8(makeSquare(3, 4), to);    // e4
  TEST_ASSERT_EQUAL_CHAR(' ', promo);
}

void test_parseCoordinate_with_promotion(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseCoordinate("e7e8q", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(6, 4), from);  // e7
  TEST_ASSERT_EQUAL_UINT8(makeSquare(7, 4), to);    // e8
  TEST_ASSERT_EQUAL_CHAR('q', promo);
}

void test_parseCoordinate_invalid_too_short(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseCoordinate("e2", from, to, promo));
}

void test_parseCoordinate_invalid_same_square(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseCoordinate("e2e2", from, to, promo));
}

void test_parseCoordinate_invalid_file(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseCoordinate("z2e4", from, to, promo));
}

void test_parseCoordinate_invalid_promotion(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseCoordinate("e7e8x", from, to, promo));
}

void test_parseCoordinate_invalid_rank(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseCoordinate("e9e4", from, to, promo));
}

void test_parseCoordinate_empty_string(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseCoordinate("", from, to, promo));
}

void test_parseCoordinate_uppercase_promotion(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseCoordinate("e7e8Q", from, to, promo));
  TEST_ASSERT_EQUAL_CHAR('Q', promo);
}

// ===========================================================================
// toSAN — uses a Position for board context
// ===========================================================================

void test_toSAN_pawn_push(void) {
  // Standard initial position: e2-e4
  Position b;
  b.newGame();
  MoveEntry m = makeEntry(6, 4, 4, 4, Piece::W_PAWN);
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("e4", san.c_str());
}

void test_toSAN_knight_move(void) {
  Position b;
  b.newGame();
  MoveEntry m = makeEntry(7, 6, 5, 5, Piece::W_KNIGHT);  // Ng1-f3
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("Nf3", san.c_str());
}

void test_toSAN_pawn_capture(void) {
  // Position where white pawn on e4 captures black pawn on d5
  Position b;
  b.loadFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
  MoveEntry m = makeEntry(4, 4, 3, 3, Piece::W_PAWN, Piece::B_PAWN);
  m.flags |= MR_CAPTURE;
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("exd5", san.c_str());
}

void test_toSAN_kingside_castling(void) {
  Position b;
  b.loadFEN("r1bqk1nr/ppppbppp/2n5/4p3/4P3/5N2/PPPPBPPP/RNBQK2R w KQkq - 4 4");
  MoveEntry m = makeEntry(7, 4, 7, 6, Piece::W_KING);
  m.flags |= MR_CASTLING;
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("O-O", san.c_str());
}

void test_toSAN_queenside_castling(void) {
  Position b;
  b.loadFEN("r3kbnr/pppqpppp/2n5/3p1b2/3P1B2/2N5/PPPQPPPP/R3KBNR w KQkq - 6 5");
  MoveEntry m = makeEntry(7, 4, 7, 2, Piece::W_KING);
  m.flags |= MR_CASTLING;
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("O-O-O", san.c_str());
}

void test_toSAN_promotion(void) {
  Position b;
  b.loadFEN("8/4P3/8/8/8/8/4p3/4K2k w - - 0 1");
  MoveEntry m = makeEntry(1, 4, 0, 4, Piece::W_PAWN, Piece::NONE, Piece::W_QUEEN);
  m.flags |= MR_PROMOTION;
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("e8=Q", san.c_str());
}

void test_toSAN_knight_disambiguation_by_file(void) {
  // Two knights on g1 and a3 can both reach b5 — no, let's use a simpler case:
  // Two knights that can reach the same square, distinguished by file
  Position b;
  b.loadFEN("4k3/8/8/8/8/8/8/1N2K1N1 w - - 0 1");
  // Nb1 and Ng1 — both knights. Nb1 can go to a3/c3/d2; Ng1 can go to f3/h3/e2
  // Actually neither can reach the same square. Let's set up:
  // Knights on c3 and g3, both can reach e4
  b.loadFEN("4k3/8/8/8/8/2N3N1/8/4K3 w - - 0 1");
  MoveEntry m = makeEntry(5, 2, 4, 4, Piece::W_KNIGHT);  // Nc3-e4
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("Nce4", san.c_str());
}

void test_toSAN_knight_disambiguation_by_rank(void) {
  // Two knights on e2 and e6, both can reach d4
  Position b;
  b.loadFEN("4k3/8/4N3/8/8/8/4N3/4K3 w - - 0 1");
  MoveEntry m = makeEntry(6, 4, 4, 3, Piece::W_KNIGHT);  // Ne2-d4
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("N2d4", san.c_str());
}

void test_toSAN_rook_capture(void) {
  Position b;
  b.loadFEN("4k3/8/8/4p3/8/8/8/4K2R w K - 0 1");
  MoveEntry m = makeEntry(7, 7, 3, 4, Piece::W_ROOK, Piece::B_PAWN);
  m.flags |= MR_CAPTURE;
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("Rxe5", san.c_str());
}

// ===========================================================================
// toLAN
// ===========================================================================

void test_toLAN_pawn_push(void) {
  MoveEntry m = makeEntry(6, 4, 4, 4, Piece::W_PAWN);
  std::string lan = notation::toLAN(m);
  TEST_ASSERT_EQUAL_STRING("e2-e4", lan.c_str());
}

void test_toLAN_knight_move(void) {
  MoveEntry m = makeEntry(7, 6, 5, 5, Piece::W_KNIGHT);
  std::string lan = notation::toLAN(m);
  TEST_ASSERT_EQUAL_STRING("Ng1-f3", lan.c_str());
}

void test_toLAN_capture(void) {
  MoveEntry m = makeEntry(5, 5, 3, 4, Piece::W_KNIGHT, Piece::B_PAWN);
  m.flags |= MR_CAPTURE;
  std::string lan = notation::toLAN(m);
  TEST_ASSERT_EQUAL_STRING("Nf3xe5", lan.c_str());
}

void test_toLAN_kingside_castling(void) {
  MoveEntry m = makeEntry(7, 4, 7, 6, Piece::W_KING);
  m.flags |= MR_CASTLING;
  std::string lan = notation::toLAN(m);
  TEST_ASSERT_EQUAL_STRING("O-O", lan.c_str());
}

void test_toLAN_queenside_castling(void) {
  MoveEntry m = makeEntry(7, 4, 7, 2, Piece::W_KING);
  m.flags |= MR_CASTLING;
  std::string lan = notation::toLAN(m);
  TEST_ASSERT_EQUAL_STRING("O-O-O", lan.c_str());
}

void test_toLAN_promotion(void) {
  MoveEntry m = makeEntry(1, 4, 0, 4, Piece::W_PAWN, Piece::NONE, Piece::W_QUEEN);
  m.flags |= MR_PROMOTION;
  std::string lan = notation::toLAN(m);
  TEST_ASSERT_EQUAL_STRING("e7-e8=Q", lan.c_str());
}

void test_toLAN_pawn_capture(void) {
  MoveEntry m = makeEntry(4, 4, 3, 3, Piece::W_PAWN, Piece::B_PAWN);
  m.flags |= MR_CAPTURE;
  std::string lan = notation::toLAN(m);
  TEST_ASSERT_EQUAL_STRING("e4xd5", lan.c_str());
}

// ===========================================================================
// parseLAN
// ===========================================================================

void test_parseLAN_pawn_push(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseLAN("e2-e4", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(1, 4), from);  // e2
  TEST_ASSERT_EQUAL_UINT8(makeSquare(3, 4), to);    // e4
  TEST_ASSERT_EQUAL_CHAR(' ', promo);
}

void test_parseLAN_knight_move(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseLAN("Ng1-f3", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, from);             // g1
  TEST_ASSERT_EQUAL_UINT8(makeSquare(2, 5), to);    // f3
}

void test_parseLAN_capture(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseLAN("Nf3xe5", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(2, 5), from);  // f3
  TEST_ASSERT_EQUAL_UINT8(makeSquare(4, 4), to);    // e5
}

void test_parseLAN_promotion(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseLAN("e7-e8=Q", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(6, 4), from);  // e7
  TEST_ASSERT_EQUAL_UINT8(makeSquare(7, 4), to);    // e8
  TEST_ASSERT_EQUAL_CHAR('Q', promo);
}

void test_parseLAN_with_check_suffix(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseLAN("Ng1-f3+", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, from);             // g1
  TEST_ASSERT_EQUAL_UINT8(makeSquare(2, 5), to);    // f3
}

void test_parseLAN_castling_returns_false(void) {
  // LAN parser returns false for castling — needs board context (handled by parseMove → parseSAN)
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseLAN("O-O", from, to, promo));
  TEST_ASSERT_FALSE(notation::parseLAN("O-O-O", from, to, promo));
}

void test_parseLAN_empty_string(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseLAN("", from, to, promo));
}

// ===========================================================================
// parseSAN
// ===========================================================================

void test_parseSAN_pawn_push(void) {
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "e4", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(1, 4), from);  // e2
  TEST_ASSERT_EQUAL_UINT8(makeSquare(3, 4), to);    // e4
}

void test_parseSAN_knight_move(void) {
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "Nf3", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, from);             // g1
  TEST_ASSERT_EQUAL_UINT8(makeSquare(2, 5), to);    // f3
}

void test_parseSAN_pawn_capture(void) {
  Position b;
  b.loadFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "exd5", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(3, 4), from);  // e4
  TEST_ASSERT_EQUAL_UINT8(makeSquare(4, 3), to);    // d5
}

void test_parseSAN_kingside_castling(void) {
  Position b;
  b.loadFEN("r1bqk1nr/ppppbppp/2n5/4p3/4P3/5N2/PPPPBPPP/RNBQK2R w KQkq - 4 4");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "O-O", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_E1, from);             // e1
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, to);               // g1
}

void test_parseSAN_queenside_castling(void) {
  Position b;
  b.loadFEN("r3kbnr/pppqpppp/2n5/3p1b2/3P1B2/2N5/PPPQPPPP/R3KBNR w KQkq - 6 5");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "O-O-O", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_E1, from);             // e1
  TEST_ASSERT_EQUAL_UINT8(SQ_C1, to);               // c1
}

void test_parseSAN_promotion(void) {
  Position b;
  b.loadFEN("8/4P3/8/8/8/8/4p3/4K2k w - - 0 1");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "e8=Q", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(6, 4), from);  // e7
  TEST_ASSERT_EQUAL_UINT8(makeSquare(7, 4), to);    // e8
  TEST_ASSERT_EQUAL_CHAR('q', promo);
}

void test_parseSAN_with_check_suffix(void) {
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  // "Nf3+" — the '+' should be stripped, parsed as Nf3
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "Nf3+", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, from);             // g1
  TEST_ASSERT_EQUAL_UINT8(makeSquare(2, 5), to);    // f3
}

void test_parseSAN_with_checkmate_suffix(void) {
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "Nf3#", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, from);             // g1
}

void test_parseSAN_empty_string(void) {
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "", from, to, promo));
}

void test_parseSAN_disambiguation_by_file(void) {
  // Two knights on c3 and g3, both can reach e4
  Position b;
  b.loadFEN("4k3/8/8/8/8/2N3N1/8/4K3 w - - 0 1");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "Nce4", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(2, 2), from);  // c3
  TEST_ASSERT_EQUAL_UINT8(makeSquare(3, 4), to);    // e4
}

void test_parseSAN_castling_with_zeros(void) {
  // Castling with '0' instead of 'O'
  Position b;
  b.loadFEN("r1bqk1nr/ppppbppp/2n5/4p3/4P3/5N2/PPPPBPPP/RNBQK2R w KQkq - 4 4");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "0-0", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_E1, from);             // e1
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, to);               // g1
}

// ===========================================================================
// parseMove — auto-detection
// ===========================================================================

void test_parseMove_coordinate(void) {
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseMove(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "e2e4", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(1, 4), from);  // e2
  TEST_ASSERT_EQUAL_UINT8(makeSquare(3, 4), to);    // e4
}

void test_parseMove_lan(void) {
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseMove(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "Ng1-f3", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, from);             // g1
  TEST_ASSERT_EQUAL_UINT8(makeSquare(2, 5), to);    // f3
}

void test_parseMove_san(void) {
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseMove(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "Nf3", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, from);             // g1
  TEST_ASSERT_EQUAL_UINT8(makeSquare(2, 5), to);    // f3
}

void test_parseMove_san_castling(void) {
  // Castling via parseMove should fall through LAN (returns false) → SAN (succeeds)
  Position b;
  b.loadFEN("r1bqk1nr/ppppbppp/2n5/4p3/4P3/5N2/PPPPBPPP/RNBQK2R w KQkq - 4 4");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseMove(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "O-O", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_E1, from);             // e1
  TEST_ASSERT_EQUAL_UINT8(SQ_G1, to);               // g1
}

void test_parseMove_empty_string(void) {
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseMove(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "", from, to, promo));
}

// ===========================================================================
// Roundtrip: toSAN → parseSAN → same coordinates
// ===========================================================================

void test_roundtrip_san_scholar_mate(void) {
  // Play Scholar's Mate: 1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7#
  Position b;
  b.newGame();

  struct Step {
    int fr, fc, tr, tc;
    Piece captured;
    Piece promo;
    bool castle;
    bool ep;
    const char* expectedSAN;
  };

  Step steps[] = {
      {6, 4, 4, 4, Piece::NONE, Piece::NONE, false, false, "e4"},      // 1. e4
      {1, 4, 3, 4, Piece::NONE, Piece::NONE, false, false, "e5"},      // 1... e5
      {7, 5, 4, 2, Piece::NONE, Piece::NONE, false, false, "Bc4"},     // 2. Bc4
      {0, 1, 2, 2, Piece::NONE, Piece::NONE, false, false, "Nc6"},     // 2... Nc6
      {7, 3, 3, 7, Piece::NONE, Piece::NONE, false, false, "Qh5"},     // 3. Qh5
      {0, 6, 2, 5, Piece::NONE, Piece::NONE, false, false, "Nf6"},     // 3... Nf6
      {3, 7, 1, 5, Piece::B_PAWN, Piece::NONE, false, false, "Qxf7"},    // 4. Qxf7 (checkmate — suffix added by caller)
  };

  for (int i = 0; i < 7; ++i) {
    Step& s = steps[i];
    MoveEntry m = makeEntry(s.fr, s.fc, s.tr, s.tc, b.getSquare(squareOf(s.fr, s.fc)), s.captured, s.promo);
    if (s.castle) m.flags |= MR_CASTLING;
    if (s.ep) m.flags |= MR_EP;

    std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
    TEST_ASSERT_EQUAL_STRING(s.expectedSAN, san.c_str());

    // Parse back
    Square pfrom, pto;
    char pp;
    TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), san, pfrom, pto, pp));
    TEST_ASSERT_EQUAL_UINT8(squareOf(s.fr, s.fc), pfrom);
    TEST_ASSERT_EQUAL_UINT8(squareOf(s.tr, s.tc), pto);

    // Apply the move to advance the board
    char promoChar = piece::pieceToChar(s.promo);
    b.makeMove(squareOf(s.fr, s.fc), squareOf(s.tr, s.tc), promoChar);
  }
}

// ===========================================================================
// Edge-case coverage (Phase 4I)
// ===========================================================================

void test_toSAN_en_passant(void) {
  // White pawn on e5, black pawn just pushed d7-d5: en passant capture exd6
  Position b;
  b.loadFEN("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
  MoveEntry m = makeEntry(3, 4, 2, 3, Piece::W_PAWN, Piece::B_PAWN);
  m.flags |= MR_EP;
  m.epCapturedSq = squareOf(3, 3);
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("exd6", san.c_str());
}

void test_toSAN_promotion_with_capture(void) {
  // White pawn on d7 captures on e8 with promotion
  Position b;
  b.loadFEN("4rb2/3P4/8/8/8/8/8/4K2k w - - 0 1");
  MoveEntry m = makeEntry(1, 3, 0, 4, Piece::W_PAWN, Piece::B_ROOK, Piece::W_QUEEN);
  m.flags |= MR_CAPTURE;
  m.flags |= MR_PROMOTION;
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("dxe8=Q", san.c_str());
}

void test_toSAN_full_disambiguation(void) {
  // Full disambiguation (file+rank) occurs when ≥2 ambiguous pieces exist:
  // one on the same file (needs rank) and one on the same rank (needs file).
  // Three queens: d1(7,3), d5(3,3), a1(7,0) — all can reach d4(4,3).
  // Qd5 shares file d → needRank. Qa1 shares rank 1 → needFile.
  Position b;
  b.loadFEN("4k3/8/8/3Q4/8/8/8/Q2Q3K w - - 0 1");
  MoveEntry m = makeEntry(7, 3, 4, 3, Piece::W_QUEEN);  // Qd1-d4
  std::string san = notation::toSAN(b.bitboards(), b.mailbox(), b.positionState(), m);
  TEST_ASSERT_EQUAL_STRING("Qd1d4", san.c_str());
}

void test_parseSAN_en_passant_capture(void) {
  Position b;
  b.loadFEN("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "exd6", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(4, 4), from);  // e5
  TEST_ASSERT_EQUAL_UINT8(makeSquare(5, 3), to);    // d6
}

void test_parseSAN_promotion_with_capture(void) {
  Position b;
  b.loadFEN("4rb2/3P4/8/8/8/8/8/4K2k w - - 0 1");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "dxe8=Q", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(6, 3), from);  // d7
  TEST_ASSERT_EQUAL_UINT8(makeSquare(7, 4), to);    // e8
  TEST_ASSERT_EQUAL_CHAR('q', promo);  // parseSAN lowercases promotion
}

void test_parseSAN_invalid_no_matching_piece(void) {
  // No knight can reach e4 from the initial position via SAN "Ne4"
  Position b;
  b.newGame();
  Square from, to;
  char promo;
  TEST_ASSERT_FALSE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "Ne4", from, to, promo));
}

void test_parseLAN_pawn_capture(void) {
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseLAN("e4xd5", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(3, 4), from);  // e4
  TEST_ASSERT_EQUAL_UINT8(makeSquare(4, 3), to);    // d5
}

void test_parseSAN_disambiguation_by_rank(void) {
  // Two knights on e2 and e6, both can reach d4 — need rank hint
  Position b;
  b.loadFEN("4k3/8/4N3/8/8/8/4N3/4K3 w - - 0 1");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "N2d4", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(makeSquare(1, 4), from);  // e2
  TEST_ASSERT_EQUAL_UINT8(makeSquare(3, 3), to);    // d4
}

void test_parseSAN_queenside_castling_with_zeros(void) {
  Position b;
  b.loadFEN("r3kbnr/pppqpppp/2n5/3p1b2/3P1B2/2N5/PPPQPPPP/R3KBNR w KQkq - 6 5");
  Square from, to;
  char promo;
  TEST_ASSERT_TRUE(notation::parseSAN(b.bitboards(), b.mailbox(), b.positionState(), b.currentTurn(), "0-0-0", from, to, promo));
  TEST_ASSERT_EQUAL_UINT8(SQ_E1, from);             // e1
  TEST_ASSERT_EQUAL_UINT8(SQ_C1, to);               // c1
}

void test_toLAN_en_passant(void) {
  MoveEntry m = makeEntry(3, 4, 2, 3, Piece::W_PAWN, Piece::B_PAWN);
  m.flags |= MR_CAPTURE;
  m.flags |= MR_EP;
  std::string lan = notation::toLAN(m);
  TEST_ASSERT_EQUAL_STRING("e5xd6", lan.c_str());
}

void test_toLAN_promotion_with_capture(void) {
  MoveEntry m = makeEntry(1, 3, 0, 4, Piece::W_PAWN, Piece::B_ROOK, Piece::W_QUEEN);
  m.flags |= MR_CAPTURE;
  m.flags |= MR_PROMOTION;
  std::string lan = notation::toLAN(m);
  TEST_ASSERT_EQUAL_STRING("d7xe8=Q", lan.c_str());
}

// ===========================================================================
// Registration
// ===========================================================================

void register_notation_tests() {
  needsDefaultKings = false;

  // toCoordinate
  RUN_TEST(test_toCoordinate_basic);
  RUN_TEST(test_toCoordinate_with_promotion);
  RUN_TEST(test_toCoordinate_all_promo_chars);

  // parseCoordinate
  RUN_TEST(test_parseCoordinate_basic);
  RUN_TEST(test_parseCoordinate_with_promotion);
  RUN_TEST(test_parseCoordinate_invalid_too_short);
  RUN_TEST(test_parseCoordinate_invalid_same_square);
  RUN_TEST(test_parseCoordinate_invalid_file);
  RUN_TEST(test_parseCoordinate_invalid_promotion);
  RUN_TEST(test_parseCoordinate_invalid_rank);
  RUN_TEST(test_parseCoordinate_empty_string);
  RUN_TEST(test_parseCoordinate_uppercase_promotion);

  // toSAN
  RUN_TEST(test_toSAN_pawn_push);
  RUN_TEST(test_toSAN_knight_move);
  RUN_TEST(test_toSAN_pawn_capture);
  RUN_TEST(test_toSAN_kingside_castling);
  RUN_TEST(test_toSAN_queenside_castling);
  RUN_TEST(test_toSAN_promotion);
  RUN_TEST(test_toSAN_knight_disambiguation_by_file);
  RUN_TEST(test_toSAN_knight_disambiguation_by_rank);
  RUN_TEST(test_toSAN_rook_capture);

  // toLAN
  RUN_TEST(test_toLAN_pawn_push);
  RUN_TEST(test_toLAN_knight_move);
  RUN_TEST(test_toLAN_capture);
  RUN_TEST(test_toLAN_kingside_castling);
  RUN_TEST(test_toLAN_queenside_castling);
  RUN_TEST(test_toLAN_promotion);
  RUN_TEST(test_toLAN_pawn_capture);

  // parseLAN
  RUN_TEST(test_parseLAN_pawn_push);
  RUN_TEST(test_parseLAN_knight_move);
  RUN_TEST(test_parseLAN_capture);
  RUN_TEST(test_parseLAN_promotion);
  RUN_TEST(test_parseLAN_with_check_suffix);
  RUN_TEST(test_parseLAN_castling_returns_false);
  RUN_TEST(test_parseLAN_empty_string);

  // parseSAN
  RUN_TEST(test_parseSAN_pawn_push);
  RUN_TEST(test_parseSAN_knight_move);
  RUN_TEST(test_parseSAN_pawn_capture);
  RUN_TEST(test_parseSAN_kingside_castling);
  RUN_TEST(test_parseSAN_queenside_castling);
  RUN_TEST(test_parseSAN_promotion);
  RUN_TEST(test_parseSAN_with_check_suffix);
  RUN_TEST(test_parseSAN_with_checkmate_suffix);
  RUN_TEST(test_parseSAN_empty_string);
  RUN_TEST(test_parseSAN_disambiguation_by_file);
  RUN_TEST(test_parseSAN_castling_with_zeros);

  // parseMove (auto-detect)
  RUN_TEST(test_parseMove_coordinate);
  RUN_TEST(test_parseMove_lan);
  RUN_TEST(test_parseMove_san);
  RUN_TEST(test_parseMove_san_castling);
  RUN_TEST(test_parseMove_empty_string);

  // Roundtrip
  RUN_TEST(test_roundtrip_san_scholar_mate);

  // Edge cases (Phase 4I)
  RUN_TEST(test_toSAN_en_passant);
  RUN_TEST(test_toSAN_promotion_with_capture);
  RUN_TEST(test_toSAN_full_disambiguation);
  RUN_TEST(test_parseSAN_en_passant_capture);
  RUN_TEST(test_parseSAN_promotion_with_capture);
  RUN_TEST(test_parseSAN_invalid_no_matching_piece);
  RUN_TEST(test_parseLAN_pawn_capture);
  RUN_TEST(test_parseSAN_disambiguation_by_rank);
  RUN_TEST(test_parseSAN_queenside_castling_with_zeros);
  RUN_TEST(test_toLAN_en_passant);
  RUN_TEST(test_toLAN_promotion_with_capture);
}
