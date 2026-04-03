#include <unity.h>
#include <iterator.h>

#include "../test_helpers.h"

// ---------------------------------------------------------------------------
// Pawn moves
// ---------------------------------------------------------------------------

void test_white_pawn_single_push(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "e2");
  int r, c;
  sq("e2", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, r - 1, c)); // e3
}

void test_white_pawn_double_push_from_start(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "e2");
  int r, c;
  sq("e2", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, r - 2, c)); // e4
}

void test_white_pawn_no_double_push_from_rank3(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "e3");
  int r, c;
  sq("e3", r, c);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, r - 2, c)); // e5 double push
}

void test_white_pawn_blocked(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5"); // blocker
  int r, c;
  sq("e4", r, c);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, r - 1, c));
}

void test_white_pawn_double_push_blocked_on_first_sq(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "e2");
  placePiece(bb, mailbox, Piece::B_PAWN, "e3"); // block first square
  int r, c;
  sq("e2", r, c);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, r - 2, c)); // e4
}

void test_white_pawn_capture_diagonal(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5"); // capturable
  int r, c;
  sq("d4", r, c);
  int tr, tc;
  sq("e5", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc));
}

void test_white_pawn_no_capture_own_piece(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "e5"); // own piece
  int r, c;
  sq("d4", r, c);
  int tr, tc;
  sq("e5", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc));
}

void test_black_pawn_single_push(void) {
  placePiece(bb, mailbox, Piece::B_PAWN, "e7");
  int r, c;
  sq("e7", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, r + 1, c)); // e6
}

void test_black_pawn_double_push_from_start(void) {
  placePiece(bb, mailbox, Piece::B_PAWN, "e7");
  int r, c;
  sq("e7", r, c);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, r + 2, c)); // e5
}

// ---------------------------------------------------------------------------
// Knight moves
// ---------------------------------------------------------------------------

void test_knight_center_moves(void) {
  placePiece(bb, mailbox, Piece::W_KNIGHT, "d4");
  int r, c;
  sq("d4", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(8, moves.count); // Knight in center has 8 moves
}

void test_knight_corner_moves(void) {
  placePiece(bb, mailbox, Piece::W_KNIGHT, "a1");
  int r, c;
  sq("a1", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(2, moves.count); // Corner knight has 2 moves
}

void test_knight_captures_enemy(void) {
  placePiece(bb, mailbox, Piece::W_KNIGHT, "d4");
  placePiece(bb, mailbox, Piece::B_PAWN, "e6"); // enemy on one of the squares
  int r, c;
  sq("d4", r, c);
  int tr, tc;
  sq("e6", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc));
}

void test_knight_blocked_by_own(void) {
  placePiece(bb, mailbox, Piece::W_KNIGHT, "d4");
  placePiece(bb, mailbox, Piece::W_PAWN, "e6"); // own piece on target
  int r, c;
  sq("d4", r, c);
  int tr, tc;
  sq("e6", tr, tc);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr, tc));
}

// ---------------------------------------------------------------------------
// Bishop moves
// ---------------------------------------------------------------------------

void test_bishop_center_moves(void) {
  placePiece(bb, mailbox, Piece::W_BISHOP, "d4");
  int r, c;
  sq("d4", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(13, moves.count); // d4 bishop on empty board
}

void test_bishop_blocked_by_own(void) {
  placePiece(bb, mailbox, Piece::W_BISHOP, "c1");
  placePiece(bb, mailbox, Piece::W_PAWN, "d2"); // own piece blocks
  placePiece(bb, mailbox, Piece::W_PAWN, "b2"); // own piece blocks
  int r, c;
  sq("c1", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

void test_bishop_captures_and_stops(void) {
  placePiece(bb, mailbox, Piece::W_BISHOP, "a1");
  placePiece(bb, mailbox, Piece::B_PAWN, "d4"); // enemy blocks diagonal
  int r, c;
  sq("a1", r, c);
  int tr, tc;
  sq("d4", tr, tc);
  // Can capture d4
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc));
  // But cannot go past d4 to e5
  int tr2, tc2;
  sq("e5", tr2, tc2);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr2, tc2));
}

// ---------------------------------------------------------------------------
// Rook moves
// ---------------------------------------------------------------------------

void test_rook_center_moves(void) {
  placePiece(bb, mailbox, Piece::W_ROOK, "d4");
  int r, c;
  sq("d4", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(14, moves.count); // Rook in center on empty board
}

void test_rook_blocked_by_own(void) {
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  placePiece(bb, mailbox, Piece::W_PAWN, "a2"); // above
  placePiece(bb, mailbox, Piece::W_PAWN, "b1"); // right
  int r, c;
  sq("a1", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

// ---------------------------------------------------------------------------
// Queen moves
// ---------------------------------------------------------------------------

void test_queen_center_moves(void) {
  placePiece(bb, mailbox, Piece::W_QUEEN, "d4");
  int r, c;
  sq("d4", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(27, moves.count); // 13 bishop + 14 rook
}

// ---------------------------------------------------------------------------
// King moves
// ---------------------------------------------------------------------------

void test_king_center_moves(void) {
  placePiece(bb, mailbox, Piece::W_KING, "d4");
  int r, c;
  sq("d4", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(8, moves.count); // King in center has 8 moves
}

void test_king_corner_moves(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  int r, c;
  sq("a1", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(3, moves.count); // Corner king has 3 moves
}

void test_king_no_move_onto_own(void) {
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::W_PAWN, "a2");
  placePiece(bb, mailbox, Piece::W_PAWN, "b2");
  placePiece(bb, mailbox, Piece::W_PAWN, "b1");
  int r, c;
  sq("a1", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

// ---------------------------------------------------------------------------
// Black piece moves (verify lowercase piece handling)
// ---------------------------------------------------------------------------

void test_black_pawn_diagonal_capture(void) {
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4"); // white piece to capture
  int r, c;
  sq("d5", r, c);
  int tr, tc;
  sq("e4", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc));
}

void test_black_knight_center(void) {
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "d4");
  int r, c;
  sq("d4", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(8, moves.count);
}

void test_black_rook_center(void) {
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_ROOK, "d4");
  int r, c;
  sq("d4", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(14, moves.count);
}

void test_black_bishop_center(void) {
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_KING, "a1");
  placePiece(bb, mailbox, Piece::B_BISHOP, "d4");
  int r, c;
  sq("d4", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(13, moves.count);
}

void test_rook_capture_and_stop(void) {
  placePiece(bb, mailbox, Piece::W_ROOK, "d4");
  placePiece(bb, mailbox, Piece::B_PAWN, "d7"); // enemy rook captures d7
  int r, c;
  sq("d4", r, c);
  int tr, tc;
  sq("d7", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc));
  // Cannot continue past d7 to d8
  int tr2, tc2;
  sq("d8", tr2, tc2);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, tr2, tc2));
}

void test_white_pawn_double_push_blocked_second_sq(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "e2");
  // e3 is empty, e4 is blocked
  placePiece(bb, mailbox, Piece::B_PAWN, "e4");
  int r, c;
  sq("e2", r, c);
  TEST_ASSERT_FALSE(moveExists(bb, mailbox, r, c, r - 2, c)); // e4 blocked
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, r - 1, c));  // e3 still ok
}

// ---------------------------------------------------------------------------
// Edge/boundary positions
// ---------------------------------------------------------------------------

void test_pawn_a_file_capture(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "a4");
  placePiece(bb, mailbox, Piece::B_PAWN, "b5"); // can capture right
  int r, c;
  sq("a4", r, c);
  int tr, tc;
  sq("b5", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc));
  // No left capture possible (off-board)
  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  // Should have: a5 (push) + b5 (capture) = 2
  TEST_ASSERT_EQUAL_INT(2, moves.count);
}

void test_pawn_h_file_capture(void) {
  placePiece(bb, mailbox, Piece::W_PAWN, "h4");
  placePiece(bb, mailbox, Piece::B_PAWN, "g5"); // can capture left
  int r, c;
  sq("h4", r, c);
  int tr, tc;
  sq("g5", tr, tc);
  TEST_ASSERT_TRUE(moveExists(bb, mailbox, r, c, tr, tc));
  // Should have: h5 (push) + g5 (capture) = 2
  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(2, moves.count);
}

void test_knight_edge_b1(void) {
  placePiece(bb, mailbox, Piece::W_KNIGHT, "b1");
  int r, c;
  sq("b1", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(3, moves.count); // a3, c3, d2
}

void test_queen_blocked(void) {
  placePiece(bb, mailbox, Piece::W_QUEEN, "d4");
  // Surround with own pieces on all 8 directions
  placePiece(bb, mailbox, Piece::W_PAWN, "d5");
  placePiece(bb, mailbox, Piece::W_PAWN, "d3");
  placePiece(bb, mailbox, Piece::W_PAWN, "c4");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  placePiece(bb, mailbox, Piece::W_PAWN, "c5");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::W_PAWN, "c3");
  placePiece(bb, mailbox, Piece::W_PAWN, "e3");
  int r, c;
  sq("d4", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

void test_bishop_corner_a1(void) {
  placePiece(bb, mailbox, Piece::W_BISHOP, "a1");
  int r, c;
  sq("a1", r, c);

  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, r, c, {}, moves);
  TEST_ASSERT_EQUAL_INT(7, moves.count); // a1-h8 diagonal
}

// ---------------------------------------------------------------------------
// Initial position move counts
// ---------------------------------------------------------------------------

void test_initial_position_white_moves(void) {
  setupInitialBoard(bb, mailbox);
  PositionState flags{0x0F, -1, -1};

  // Each of the 8 pawns has 2 moves, each of 2 knights has 2 moves = 20 total
  int totalMoves = 0;
  iterator::forEachPiece(bb, mailbox, [&](int row, int col, Piece piece) {
    if (piece::pieceColor(piece) != Color::WHITE) return;
    MoveList moves;
    movegen::getPossibleMoves(bb, mailbox, row, col, flags, moves);
    totalMoves += moves.count;
  });
  TEST_ASSERT_EQUAL_INT(20, totalMoves);
}

void test_initial_position_black_moves(void) {
  setupInitialBoard(bb, mailbox);
  PositionState flags{0x0F, -1, -1};

  int totalMoves = 0;
  iterator::forEachPiece(bb, mailbox, [&](int row, int col, Piece piece) {
    if (piece::pieceColor(piece) != Color::BLACK) return;
    MoveList moves;
    movegen::getPossibleMoves(bb, mailbox, row, col, flags, moves);
    totalMoves += moves.count;
  });
  TEST_ASSERT_EQUAL_INT(20, totalMoves);
}

// ---------------------------------------------------------------------------
// Bulk legal move generation (generateAllMoves / generateCaptures)
// ---------------------------------------------------------------------------

void test_generateAllMoves_initial_position(void) {
  setupInitialBoard(bb, mailbox);
  PositionState state{0x0F, -1, -1};

  MoveList moves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, state, moves);
  TEST_ASSERT_EQUAL_INT(20, moves.count);

  movegen::generateAllMoves(bb, mailbox, Color::BLACK, state, moves);
  TEST_ASSERT_EQUAL_INT(20, moves.count);
}

void test_generateAllMoves_captures_only(void) {
  // White queen on d4, black pawns on c5, e5, black king on h8, white king on h1.
  // Queen can capture c5, e5 = 2 captures.
  placePiece(bb, mailbox, Piece::W_KING, "h1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::W_QUEEN, "d4");
  placePiece(bb, mailbox, Piece::B_PAWN, "c5");
  placePiece(bb, mailbox, Piece::B_PAWN, "e5");
  PositionState state{};
  state.castlingRights = 0;

  MoveList caps;
  movegen::generateCaptures(bb, mailbox, Color::WHITE, state, caps);

  // Count captures actually flagged
  int captureCount = 0;
  for (int i = 0; i < caps.count; i++) {
    TEST_ASSERT_TRUE(caps.moves[i].isCapture());
    captureCount++;
  }
  // Queen captures c5 + e5, king has no captures → 2
  TEST_ASSERT_EQUAL_INT(2, captureCount);
}

void test_generateAllMoves_under_check(void) {
  // White king on e1, black rook giving check from e8, white rook on a1.
  // Legal: king moves to escape, or rook blocks on e-file.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  PositionState state{};
  state.castlingRights = 0;

  MoveList moves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, state, moves);

  // All generated moves must be legal evasions.
  TEST_ASSERT_TRUE(moves.count > 0);

  // King escape moves (d1, f1, d2, f2 — but some may be attacked)
  // + rook interpositions on the e-file (e2, e3, e4, e5, e6, e7) or capture e8
  // Exact count depends on attack geometry; just verify it's reasonable (> 0).
  // Also verify king can't stay on e-file (still in check from e8).
  for (int i = 0; i < moves.count; i++) {
    Move m = moves.moves[i];
    uint8_t from = m.from;
    uint8_t to = m.to;
    // If this is a king move, destination should not be on e-file
    // (rook attacks entire e-file).
    if (from == squareOf(7, 4)) { // e1
      TEST_ASSERT_NOT_EQUAL(4, colOf(to)); // not e-file
    }
  }
}

void test_generateAllMoves_double_check(void) {
  // Double check: only king can move.
  // White king on e1, black rook on e8 (check), black bishop on b4 (check).
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8");
  placePiece(bb, mailbox, Piece::B_BISHOP, "b4");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1"); // Can't block two checks
  PositionState state{};
  state.castlingRights = 0;

  MoveList moves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, state, moves);

  // All moves must be king moves.
  uint8_t kingSq = static_cast<uint8_t>(squareOf(7, 4)); // e1
  for (int i = 0; i < moves.count; i++)
    TEST_ASSERT_EQUAL_UINT8(kingSq, moves.moves[i].from);
}

void test_generateAllMoves_flags_capture(void) {
  // Verify capture flag is set correctly.
  placePiece(bb, mailbox, Piece::W_KING, "h1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "d4");
  placePiece(bb, mailbox, Piece::B_PAWN, "e6");
  PositionState state{};
  state.castlingRights = 0;

  MoveList moves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, state, moves);

  uint8_t e6 = static_cast<uint8_t>(squareOf(2, 4)); // e6
  bool foundCapture = false;
  for (int i = 0; i < moves.count; i++) {
    Move m = moves.moves[i];
    if (m.to == e6) {
      TEST_ASSERT_TRUE(m.isCapture());
      TEST_ASSERT_FALSE(m.isEP());
      TEST_ASSERT_FALSE(m.isCastling());
      TEST_ASSERT_FALSE(m.isPromotion());
      foundCapture = true;
    }
  }
  TEST_ASSERT_TRUE(foundCapture);
}

void test_generateAllMoves_flags_ep(void) {
  // Verify EP flag.
  // White pawn on e5, black pawn just moved d7-d5 → EP target d6.
  placePiece(bb, mailbox, Piece::W_KING, "h1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  PositionState state{};
  state.castlingRights = 0;
  state.epRow = 2;  // d6 → row 2 (rank 6)
  state.epCol = 3;  // file d

  MoveList moves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, state, moves);

  uint8_t d6 = static_cast<uint8_t>(squareOf(2, 3));
  bool foundEP = false;
  for (int i = 0; i < moves.count; i++) {
    Move m = moves.moves[i];
    if (m.to == d6 && m.isEP()) {
      TEST_ASSERT_TRUE(m.isCapture());
      TEST_ASSERT_TRUE(m.isEP());
      foundEP = true;
    }
  }
  TEST_ASSERT_TRUE(foundEP);
}

void test_generateAllMoves_flags_castling(void) {
  // Verify castling flag.
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  PositionState state{};
  state.castlingRights = 0x01; // White kingside only

  MoveList moves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, state, moves);

  uint8_t g1 = static_cast<uint8_t>(squareOf(7, 6));
  bool foundCastle = false;
  for (int i = 0; i < moves.count; i++) {
    Move m = moves.moves[i];
    if (m.to == g1 && m.isCastling()) {
      TEST_ASSERT_FALSE(m.isCapture());
      foundCastle = true;
    }
  }
  TEST_ASSERT_TRUE(foundCastle);
}

void test_generateAllMoves_flags_promotion(void) {
  // White pawn on e7, empty e8 → 4 promotion moves.
  placePiece(bb, mailbox, Piece::W_KING, "h1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e7");
  PositionState state{};
  state.castlingRights = 0;

  MoveList moves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, state, moves);

  uint8_t e8 = static_cast<uint8_t>(squareOf(0, 4));
  int promoCount = 0;
  bool promoIndices[4] = {};
  for (int i = 0; i < moves.count; i++) {
    Move m = moves.moves[i];
    if (m.to == e8 && m.isPromotion()) {
      promoIndices[m.promoIndex()] = true;
      promoCount++;
    }
  }
  TEST_ASSERT_EQUAL_INT(4, promoCount);
  // All 4 promotion types present
  for (int i = 0; i < 4; i++)
    TEST_ASSERT_TRUE(promoIndices[i]);

  // Verify promotion type mapping round-trips
  TEST_ASSERT_ENUM_EQ(PieceType::KNIGHT, Move::promoTypeFromIndex(0));
  TEST_ASSERT_ENUM_EQ(PieceType::BISHOP, Move::promoTypeFromIndex(1));
  TEST_ASSERT_ENUM_EQ(PieceType::ROOK,   Move::promoTypeFromIndex(2));
  TEST_ASSERT_ENUM_EQ(PieceType::QUEEN,  Move::promoTypeFromIndex(3));
}

void test_generateAllMoves_matches_perPiece(void) {
  // Verify generateAllMoves produces the same move set as iterating
  // getPossibleMoves per piece (for non-promotion positions).
  setupInitialBoard(bb, mailbox);
  PositionState state{0x0F, -1, -1};

  // Count via per-piece getPossibleMoves
  int perPieceCount = 0;
  iterator::forEachPiece(bb, mailbox, [&](int r, int c, Piece piece) {
    if (piece::pieceColor(piece) != Color::WHITE) return;
    MoveList moves;
    movegen::getPossibleMoves(bb, mailbox, r, c, state, moves);
    perPieceCount += moves.count;
  });

  // Count via bulk generateAllMoves
  MoveList bulk;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, state, bulk);

  TEST_ASSERT_EQUAL_INT(perPieceCount, bulk.count);

  // Verify every bulk move has a from-square belonging to a white piece
  for (int i = 0; i < bulk.count; i++) {
    Piece p = mailbox[bulk.moves[i].from];
    TEST_ASSERT_TRUE(piece::pieceColor(p) == Color::WHITE);
  }
}

void test_generateCaptures_ep_included(void) {
  // generateCaptures must include EP captures.
  placePiece(bb, mailbox, Piece::W_KING, "h1");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");
  PositionState state{};
  state.castlingRights = 0;
  state.epRow = 2;  // d6
  state.epCol = 3;

  MoveList caps;
  movegen::generateCaptures(bb, mailbox, Color::WHITE, state, caps);

  bool foundEP = false;
  for (int i = 0; i < caps.count; i++) {
    if (caps.moves[i].isEP()) {
      foundEP = true;
      TEST_ASSERT_TRUE(caps.moves[i].isCapture());
    }
  }
  TEST_ASSERT_TRUE(foundEP);
}

void test_generateCaptures_no_quiet_moves(void) {
  // generateCaptures should not include non-capture quiet moves.
  setupInitialBoard(bb, mailbox);
  PositionState state{0x0F, -1, -1};

  MoveList caps;
  movegen::generateCaptures(bb, mailbox, Color::WHITE, state, caps);

  // Initial position has zero captures available.
  TEST_ASSERT_EQUAL_INT(0, caps.count);
}

void test_generateAllMoves_stalemate(void) {
  // Stalemate: no legal moves → count = 0.
  // Simple stalemate: black king on h8, white queen on g6, white king on f7.
  placePiece(bb, mailbox, Piece::W_KING, "f7");
  placePiece(bb, mailbox, Piece::W_QUEEN, "g6");
  placePiece(bb, mailbox, Piece::B_KING, "h8");
  PositionState state{};
  state.castlingRights = 0;

  MoveList moves;
  movegen::generateAllMoves(bb, mailbox, Color::BLACK, state, moves);
  TEST_ASSERT_EQUAL_INT(0, moves.count);
}

void test_Move_struct_basics(void) {
  Move m(10, 20, MOVE_CAPTURE | MOVE_EP);
  TEST_ASSERT_EQUAL_UINT8(10, m.from);
  TEST_ASSERT_EQUAL_UINT8(20, m.to);
  TEST_ASSERT_TRUE(m.isCapture());
  TEST_ASSERT_TRUE(m.isEP());
  TEST_ASSERT_FALSE(m.isCastling());
  TEST_ASSERT_FALSE(m.isPromotion());

  Move m2;
  TEST_ASSERT_EQUAL_UINT8(0, m2.from);
  TEST_ASSERT_EQUAL_UINT8(0, m2.flags);

  // Equality
  Move m3(10, 20, MOVE_CAPTURE | MOVE_EP);
  TEST_ASSERT_TRUE(m == m3);
  Move m4(10, 21, MOVE_CAPTURE | MOVE_EP);
  TEST_ASSERT_FALSE(m == m4);
}

void test_MoveList_add_clear(void) {
  MoveList list;
  TEST_ASSERT_EQUAL_INT(0, list.count);

  list.add(Move(0, 8, 0));
  list.add(Move(1, 9, MOVE_CAPTURE));
  TEST_ASSERT_EQUAL_INT(2, list.count);
  TEST_ASSERT_EQUAL_UINT8(0, list.moves[0].from);
  TEST_ASSERT_EQUAL_UINT8(9, list.moves[1].to);
  TEST_ASSERT_TRUE(list.moves[1].isCapture());

  list.clear();
  TEST_ASSERT_EQUAL_INT(0, list.count);
}

void test_Move_promo_index_roundtrip(void) {
  // Verify promotion index ↔ PieceType round-trip.
  PieceType types[] = {PieceType::KNIGHT, PieceType::BISHOP, PieceType::ROOK, PieceType::QUEEN};
  for (int i = 0; i < 4; i++) {
    uint8_t idx = Move::promoIndexFromType(types[i]);
    TEST_ASSERT_EQUAL_UINT8(i, idx);
    TEST_ASSERT_ENUM_EQ(types[i], Move::promoTypeFromIndex(idx));
  }
}

// ---------------------------------------------------------------------------
// hasAnyLegalMove
// ---------------------------------------------------------------------------

void test_has_legal_moves_initial(void) {
  setupInitialBoard(bb, mailbox);
  PositionState flags{0x0F, -1, -1};
  TEST_ASSERT_TRUE(movegen::hasAnyLegalMove(bb, mailbox, Color::WHITE, flags));
  TEST_ASSERT_TRUE(movegen::hasAnyLegalMove(bb, mailbox, Color::BLACK, flags));
}

void test_no_legal_moves_stalemate(void) {
  // Same stalemate position from check_mechanics
  placePiece(bb, mailbox, Piece::B_KING, "a8");
  placePiece(bb, mailbox, Piece::W_QUEEN, "b6");
  placePiece(bb, mailbox, Piece::W_KING, "c6");
  PositionState flags{0x00, -1, -1, 0, 1};
  TEST_ASSERT_FALSE(movegen::hasAnyLegalMove(bb, mailbox, Color::BLACK, flags));
}

void test_hasAnyLegalMove_in_check_with_escape(void) {
  // King in check but can escape
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_ROOK, "e8"); // rook checks
  PositionState flags{0x00, -1, -1, 0, 1};
  // King can escape to d1, d2, f1, f2
  TEST_ASSERT_TRUE(movegen::hasAnyLegalMove(bb, mailbox, Color::WHITE, flags));
}

// ---------------------------------------------------------------------------
// Staged move generation (captures vs. quiets with shared LegalityContext)
// ---------------------------------------------------------------------------

// staged captures + quiets == generateAllMoves in the initial position.
void test_staged_matches_all_moves_initial(void) {
  setupInitialBoard(bb, mailbox);
  PositionState flags{0x0F, -1, -1, 0, 1};

  MoveList allMoves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, flags, allMoves);

  int kidx = piece::pieceZobristIndex(piece::makePiece(Color::WHITE, PieceType::KING));
  Square kingSq = lsb(bb.byPiece[kidx]);
  movegen::LegalityContext ctx = movegen::buildLegalityContext(bb, Color::WHITE, kingSq);

  MoveList caps, quiets;
  movegen::generateCaptures(bb, mailbox, Color::WHITE, flags, ctx, caps);
  movegen::generateQuiets(bb, mailbox, Color::WHITE, flags, ctx, quiets);

  // No captures in initial position, 20 quiets
  TEST_ASSERT_EQUAL_INT(0, caps.count);
  TEST_ASSERT_EQUAL_INT(allMoves.count, caps.count + quiets.count);
}

// staged captures + quiets == generateAllMoves in a complex middlegame.
void test_staged_matches_all_moves_middlegame(void) {
  clearBoard(bb, mailbox);
  // Kiwipete position: r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::W_ROOK, "a1");
  placePiece(bb, mailbox, Piece::W_ROOK, "h1");
  placePiece(bb, mailbox, Piece::W_QUEEN, "f3");
  placePiece(bb, mailbox, Piece::W_BISHOP, "d2");
  placePiece(bb, mailbox, Piece::W_BISHOP, "e2");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "c3");
  placePiece(bb, mailbox, Piece::W_KNIGHT, "e5");
  placePiece(bb, mailbox, Piece::W_PAWN, "a2");
  placePiece(bb, mailbox, Piece::W_PAWN, "b2");
  placePiece(bb, mailbox, Piece::W_PAWN, "c2");
  placePiece(bb, mailbox, Piece::W_PAWN, "d5");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  placePiece(bb, mailbox, Piece::W_PAWN, "f2");
  placePiece(bb, mailbox, Piece::W_PAWN, "g2");
  placePiece(bb, mailbox, Piece::W_PAWN, "h2");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "a8");
  placePiece(bb, mailbox, Piece::B_ROOK, "h8");
  placePiece(bb, mailbox, Piece::B_QUEEN, "e7");
  placePiece(bb, mailbox, Piece::B_BISHOP, "a6");
  placePiece(bb, mailbox, Piece::B_BISHOP, "g7");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "b6");
  placePiece(bb, mailbox, Piece::B_KNIGHT, "f6");
  placePiece(bb, mailbox, Piece::B_PAWN, "a7");
  placePiece(bb, mailbox, Piece::B_PAWN, "b4");
  placePiece(bb, mailbox, Piece::B_PAWN, "c7");
  placePiece(bb, mailbox, Piece::B_PAWN, "d7");
  placePiece(bb, mailbox, Piece::B_PAWN, "e6");
  placePiece(bb, mailbox, Piece::B_PAWN, "f7");
  placePiece(bb, mailbox, Piece::B_PAWN, "g6");
  placePiece(bb, mailbox, Piece::B_PAWN, "h3");
  PositionState flags{0x0F, -1, -1, 0, 1};

  MoveList allMoves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, flags, allMoves);

  int kidx = piece::pieceZobristIndex(piece::makePiece(Color::WHITE, PieceType::KING));
  Square kingSq = lsb(bb.byPiece[kidx]);
  movegen::LegalityContext ctx = movegen::buildLegalityContext(bb, Color::WHITE, kingSq);

  MoveList caps, quiets;
  movegen::generateCaptures(bb, mailbox, Color::WHITE, flags, ctx, caps);
  movegen::generateQuiets(bb, mailbox, Color::WHITE, flags, ctx, quiets);

  TEST_ASSERT_EQUAL_INT(allMoves.count, caps.count + quiets.count);

  // Verify no overlap: every capture is a capture, every quiet is not.
  for (int i = 0; i < caps.count; i++)
    TEST_ASSERT_TRUE(caps.moves[i].isCapture() || caps.moves[i].isPromotion());
  for (int i = 0; i < quiets.count; i++)
    TEST_ASSERT_TRUE(!quiets.moves[i].isCapture() && !quiets.moves[i].isPromotion());
}

// staged captures + quiets == generateAllMoves when in check.
void test_staged_matches_all_moves_in_check(void) {
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::B_ROOK, "e4");  // checks white king
  placePiece(bb, mailbox, Piece::W_KNIGHT, "d3"); // can block or capture
  PositionState flags{0x00, -1, -1, 0, 1};

  MoveList allMoves;
  movegen::generateAllMoves(bb, mailbox, Color::WHITE, flags, allMoves);

  int kidx = piece::pieceZobristIndex(piece::makePiece(Color::WHITE, PieceType::KING));
  Square kingSq = lsb(bb.byPiece[kidx]);
  movegen::LegalityContext ctx = movegen::buildLegalityContext(bb, Color::WHITE, kingSq);

  MoveList caps, quiets;
  movegen::generateCaptures(bb, mailbox, Color::WHITE, flags, ctx, caps);
  movegen::generateQuiets(bb, mailbox, Color::WHITE, flags, ctx, quiets);

  TEST_ASSERT_EQUAL_INT(allMoves.count, caps.count + quiets.count);
}

void register_movegen_tests() {
  needsDefaultKings = true;

  // Pawns
  RUN_TEST(test_white_pawn_single_push);
  RUN_TEST(test_white_pawn_double_push_from_start);
  RUN_TEST(test_white_pawn_no_double_push_from_rank3);
  RUN_TEST(test_white_pawn_blocked);
  RUN_TEST(test_white_pawn_double_push_blocked_on_first_sq);
  RUN_TEST(test_white_pawn_double_push_blocked_second_sq);
  RUN_TEST(test_white_pawn_capture_diagonal);
  RUN_TEST(test_white_pawn_no_capture_own_piece);
  RUN_TEST(test_black_pawn_single_push);
  RUN_TEST(test_black_pawn_double_push_from_start);
  RUN_TEST(test_black_pawn_diagonal_capture);
  RUN_TEST(test_pawn_a_file_capture);
  RUN_TEST(test_pawn_h_file_capture);

  // Knights
  RUN_TEST(test_knight_center_moves);
  RUN_TEST(test_knight_corner_moves);
  RUN_TEST(test_knight_captures_enemy);
  RUN_TEST(test_knight_blocked_by_own);
  RUN_TEST(test_knight_edge_b1);

  // Bishops
  RUN_TEST(test_bishop_center_moves);
  RUN_TEST(test_bishop_blocked_by_own);
  RUN_TEST(test_bishop_captures_and_stops);
  RUN_TEST(test_bishop_corner_a1);

  // Rooks
  RUN_TEST(test_rook_center_moves);
  RUN_TEST(test_rook_blocked_by_own);
  RUN_TEST(test_rook_capture_and_stop);

  // Queen
  RUN_TEST(test_queen_center_moves);
  RUN_TEST(test_queen_blocked);

  // King
  RUN_TEST(test_king_center_moves);
  RUN_TEST(test_king_corner_moves);
  RUN_TEST(test_king_no_move_onto_own);

  // Black pieces
  RUN_TEST(test_black_knight_center);
  RUN_TEST(test_black_rook_center);
  RUN_TEST(test_black_bishop_center);

  // Initial position
  RUN_TEST(test_initial_position_white_moves);
  RUN_TEST(test_initial_position_black_moves);

  // Bulk move generation (generateAllMoves / generateCaptures)
  RUN_TEST(test_Move_struct_basics);
  RUN_TEST(test_Move_promo_index_roundtrip);
  RUN_TEST(test_MoveList_add_clear);
  RUN_TEST(test_generateAllMoves_initial_position);
  RUN_TEST(test_generateAllMoves_captures_only);
  RUN_TEST(test_generateAllMoves_under_check);
  RUN_TEST(test_generateAllMoves_double_check);
  RUN_TEST(test_generateAllMoves_flags_capture);
  RUN_TEST(test_generateAllMoves_flags_ep);
  RUN_TEST(test_generateAllMoves_flags_castling);
  RUN_TEST(test_generateAllMoves_flags_promotion);
  RUN_TEST(test_generateAllMoves_matches_perPiece);
  RUN_TEST(test_generateCaptures_ep_included);
  RUN_TEST(test_generateCaptures_no_quiet_moves);
  RUN_TEST(test_generateAllMoves_stalemate);

  // hasAnyLegalMove
  needsDefaultKings = false;
  RUN_TEST(test_has_legal_moves_initial);
  RUN_TEST(test_no_legal_moves_stalemate);
  RUN_TEST(test_hasAnyLegalMove_in_check_with_escape);

  // Staged move generation (captures + quiets via LegalityContext)
  RUN_TEST(test_staged_matches_all_moves_initial);
  RUN_TEST(test_staged_matches_all_moves_middlegame);
  RUN_TEST(test_staged_matches_all_moves_in_check);
}
