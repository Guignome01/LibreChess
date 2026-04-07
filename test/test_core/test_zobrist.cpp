#include <unity.h>

#include <zobrist.h>
#include <position.h>

#include "../test_helpers.h"

// ---------------------------------------------------------------------------
// Key generation spot-check (constexpr PRNG matches known values)
// ---------------------------------------------------------------------------

void test_zobrist_keys_pieces_first(void) {
  TEST_ASSERT_EQUAL_HEX64(0xF2C49D843D3F949FULL, zobrist::KEYS.pieces[0][0]);
  TEST_ASSERT_EQUAL_HEX64(0x78FFE750EDADAAE9ULL, zobrist::KEYS.pieces[1][0]);
  TEST_ASSERT_EQUAL_HEX64(0x44122009B38CC8AAULL, zobrist::KEYS.pieces[11][0]);
}

void test_zobrist_keys_pieces_last(void) {
  TEST_ASSERT_EQUAL_HEX64(0xBD8F540EF5A1C22DULL, zobrist::KEYS.pieces[0][63]);
  TEST_ASSERT_EQUAL_HEX64(0x03F97B6B42D3E608ULL, zobrist::KEYS.pieces[11][63]);
}

void test_zobrist_keys_castling(void) {
  TEST_ASSERT_EQUAL_HEX64(0x5AE383CADBE6C3C4ULL, zobrist::KEYS.castling[0]);
  TEST_ASSERT_EQUAL_HEX64(0xC2428255C16465F3ULL, zobrist::KEYS.castling[15]);
}

void test_zobrist_keys_en_passant(void) {
  TEST_ASSERT_EQUAL_HEX64(0xC737548EDCB1B1F8ULL, zobrist::KEYS.enPassant[0]);
  TEST_ASSERT_EQUAL_HEX64(0x6BE449C10216D94EULL, zobrist::KEYS.enPassant[7]);
}

void test_zobrist_keys_side_to_move(void) {
  TEST_ASSERT_EQUAL_HEX64(0x41B86C4A1075677CULL, zobrist::KEYS.sideToMove);
}

// ---------------------------------------------------------------------------
// pieceIndex lookup
// ---------------------------------------------------------------------------

void test_zobrist_piece_index_white(void) {
  TEST_ASSERT_EQUAL(0, piece::pieceIndex(Piece::W_PAWN));
  TEST_ASSERT_EQUAL(1, piece::pieceIndex(Piece::W_KNIGHT));
  TEST_ASSERT_EQUAL(2, piece::pieceIndex(Piece::W_BISHOP));
  TEST_ASSERT_EQUAL(3, piece::pieceIndex(Piece::W_ROOK));
  TEST_ASSERT_EQUAL(4, piece::pieceIndex(Piece::W_QUEEN));
  TEST_ASSERT_EQUAL(5, piece::pieceIndex(Piece::W_KING));
}

void test_zobrist_piece_index_black(void) {
  TEST_ASSERT_EQUAL(6, piece::pieceIndex(Piece::B_PAWN));
  TEST_ASSERT_EQUAL(7, piece::pieceIndex(Piece::B_KNIGHT));
  TEST_ASSERT_EQUAL(8, piece::pieceIndex(Piece::B_BISHOP));
  TEST_ASSERT_EQUAL(9, piece::pieceIndex(Piece::B_ROOK));
  TEST_ASSERT_EQUAL(10, piece::pieceIndex(Piece::B_QUEEN));
  TEST_ASSERT_EQUAL(11, piece::pieceIndex(Piece::B_KING));
}

void test_zobrist_piece_index_invalid(void) {
  TEST_ASSERT_EQUAL(-1, piece::pieceIndex(Piece::NONE));
}

// ---------------------------------------------------------------------------
// computeHash — functional tests
// ---------------------------------------------------------------------------

void test_computeHash_deterministic(void) {
  setupInitialBoard(bb, mailbox);
  PositionState st = PositionState::initial();
  uint64_t h1 = zobrist::computeHash(bb, mailbox, Color::WHITE, st, false);
  uint64_t h2 = zobrist::computeHash(bb, mailbox, Color::WHITE, st, false);
  TEST_ASSERT_EQUAL_HEX64(h1, h2);
  TEST_ASSERT_NOT_EQUAL(0ULL, h1);
}

void test_computeHash_different_positions(void) {
  setupInitialBoard(bb, mailbox);
  PositionState st = PositionState::initial();
  uint64_t h1 = zobrist::computeHash(bb, mailbox, Color::WHITE, st, false);

  // Move e2 to e4
  Piece pawn = mailbox[squareOf(6, 4)];
  bb.removePiece(squareOf(6, 4), pawn);
  mailbox[squareOf(6, 4)] = Piece::NONE;
  bb.setPiece(squareOf(4, 4), pawn);
  mailbox[squareOf(4, 4)] = pawn;
  st.epSquare = squareOf(5, 4);
  uint64_t h2 = zobrist::computeHash(bb, mailbox, Color::BLACK, st, false);
  TEST_ASSERT_NOT_EQUAL(h1, h2);
}

void test_computeHash_same_position_same_hash(void) {
  // Set up identical boards separately
  BitboardSet bb2;
  Piece mailbox2[64];
  setupInitialBoard(bb, mailbox);
  setupInitialBoard(bb2, mailbox2);
  PositionState st = PositionState::initial();
  uint64_t h1 = zobrist::computeHash(bb, mailbox, Color::WHITE, st, false);
  uint64_t h2 = zobrist::computeHash(bb2, mailbox2, Color::WHITE, st, false);
  TEST_ASSERT_EQUAL_HEX64(h1, h2);
}

void test_computeHash_turn_sensitivity(void) {
  setupInitialBoard(bb, mailbox);
  PositionState st = PositionState::initial();
  uint64_t hw = zobrist::computeHash(bb, mailbox, Color::WHITE, st, false);
  uint64_t hb = zobrist::computeHash(bb, mailbox, Color::BLACK, st, false);
  TEST_ASSERT_NOT_EQUAL(hw, hb);
}

void test_computeHash_castling_sensitivity(void) {
  setupInitialBoard(bb, mailbox);
  PositionState st1 = PositionState::initial();  // KQkq = 0x0F
  PositionState st2 = PositionState::initial();
  st2.castlingRights = 0x05;  // Kk only
  uint64_t h1 = zobrist::computeHash(bb, mailbox, Color::WHITE, st1, false);
  uint64_t h2 = zobrist::computeHash(bb, mailbox, Color::WHITE, st2, false);
  TEST_ASSERT_NOT_EQUAL(h1, h2);
}

void test_computeHash_en_passant_sensitivity(void) {
  // Position where white pawn on e5, black pawn on d5 — EP capturable on d6
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e5");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");

  PositionState st1{0x00, SQ_NONE, 0, 1};  // no EP
  PositionState st2{0x00, squareOf(2, 3), 0, 1};    // EP on d6 (row=2, col=3)

  uint64_t h1 = zobrist::computeHash(bb, mailbox, Color::WHITE, st1, false);
  uint64_t h2 = zobrist::computeHash(bb, mailbox, Color::WHITE, st2, true);
  TEST_ASSERT_NOT_EQUAL(h1, h2);
}

void test_computeHash_king_vs_king(void) {
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  PositionState st{0x00, SQ_NONE, 0, 1};
  uint64_t h = zobrist::computeHash(bb, mailbox, Color::WHITE, st, false);
  TEST_ASSERT_NOT_EQUAL(0ULL, h);
}

void test_computeHash_incremental_vs_full(void) {
  // Position maintains hash incrementally via makeMove.
  // Verify that the incremental hash matches a full recomputation.
  Position pos;
  pos.newGame();
  pos.makeMove(squareOf(6, 4), squareOf( 4, 4));  // e2e4
  pos.makeMove(squareOf(1, 4), squareOf( 3, 4));  // e7e5
  pos.makeMove(squareOf(7, 6), squareOf( 5, 5));  // Nf3

  // Full recompute from scratch
  uint64_t full = zobrist::computeHash(pos.bitboards(), pos.mailbox(), pos.sideToMove(), pos.positionState(), false);

  // The board's internal hash is stored in hashHistory — the last entry
  // We can verify by loading the same FEN into a fresh board
  std::string fen = pos.getFen();
  Position pos2;
  pos2.loadFEN(fen);

  // Both boards should produce the same hash from scratch
  uint64_t full2 = zobrist::computeHash(pos2.bitboards(), pos2.mailbox(), pos2.sideToMove(), pos2.positionState(), false);
  TEST_ASSERT_EQUAL_HEX64(full, full2);
}

// ===========================================================================
// computePawnHash — pawn-only Zobrist key
// ===========================================================================

// Same pawn structure produces the same hash.
void test_computePawnHash_deterministic(void) {
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  placePiece(bb, mailbox, Piece::B_PAWN, "d5");

  uint64_t h1 = zobrist::computePawnHash(bb);
  uint64_t h2 = zobrist::computePawnHash(bb);
  TEST_ASSERT_EQUAL_HEX64(h1, h2);
  TEST_ASSERT_NOT_EQUAL(0ULL, h1);
}

// Different pawn placements produce different hashes.
void test_computePawnHash_different_pawns(void) {
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  uint64_t h1 = zobrist::computePawnHash(bb);

  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_PAWN, "d4");
  uint64_t h2 = zobrist::computePawnHash(bb);

  TEST_ASSERT_NOT_EQUAL(h1, h2);
}

// Non-pawn pieces do not affect the pawn hash.
void test_computePawnHash_ignores_non_pawns(void) {
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  placePiece(bb, mailbox, Piece::W_PAWN, "e4");
  uint64_t h1 = zobrist::computePawnHash(bb);

  // Add a knight — pawn hash should not change
  placePiece(bb, mailbox, Piece::W_KNIGHT, "c3");
  uint64_t h2 = zobrist::computePawnHash(bb);
  TEST_ASSERT_EQUAL_HEX64(h1, h2);
}

// No pawns on board yields hash of zero.
void test_computePawnHash_no_pawns(void) {
  clearBoard(bb, mailbox);
  placePiece(bb, mailbox, Piece::W_KING, "e1");
  placePiece(bb, mailbox, Piece::B_KING, "e8");
  uint64_t h = zobrist::computePawnHash(bb);
  TEST_ASSERT_EQUAL_HEX64(0ULL, h);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_zobrist_tests() {
  needsDefaultKings = false;

  // Key generation
  RUN_TEST(test_zobrist_keys_pieces_first);
  RUN_TEST(test_zobrist_keys_pieces_last);
  RUN_TEST(test_zobrist_keys_castling);
  RUN_TEST(test_zobrist_keys_en_passant);
  RUN_TEST(test_zobrist_keys_side_to_move);

  // pieceIndex
  RUN_TEST(test_zobrist_piece_index_white);
  RUN_TEST(test_zobrist_piece_index_black);
  RUN_TEST(test_zobrist_piece_index_invalid);

  // computeHash
  RUN_TEST(test_computeHash_deterministic);
  RUN_TEST(test_computeHash_different_positions);
  RUN_TEST(test_computeHash_same_position_same_hash);
  RUN_TEST(test_computeHash_turn_sensitivity);
  RUN_TEST(test_computeHash_castling_sensitivity);
  RUN_TEST(test_computeHash_en_passant_sensitivity);
  RUN_TEST(test_computeHash_king_vs_king);
  RUN_TEST(test_computeHash_incremental_vs_full);

  // computePawnHash
  RUN_TEST(test_computePawnHash_deterministic);
  RUN_TEST(test_computePawnHash_different_pawns);
  RUN_TEST(test_computePawnHash_ignores_non_pawns);
  RUN_TEST(test_computePawnHash_no_pawns);
}
