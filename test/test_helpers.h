#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <bitboard.h>
#include <utils.h>
#include <fen.h>
#include <movegen.h>
#include <notation.h>
#include <position.h>
#include <rules.h>

#include <cstring>

using namespace LibreChess;

/// Set up the standard initial chess position.
inline void setupInitialBoard(BitboardSet& bb, Piece mailbox[]) {
  bb.clear();
  memset(mailbox, 0, 64);
  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col) {
      Piece p = Position::INITIAL_BOARD[row][col];
      if (p != Piece::NONE) {
        Square sq = squareOf(row, col);
        bb.setPiece(sq, p);
        mailbox[sq] = p;
      }
    }
}

/// Clear the board (all empty squares).
inline void clearBoard(BitboardSet& bb, Piece mailbox[]) {
  bb.clear();
  memset(mailbox, 0, 64);
}

/// Place a single piece on a cleared board at the given algebraic position.
inline void placePiece(BitboardSet& bb, Piece mailbox[], Piece piece, const char* square) {
  int col = square[0] - 'a';
  int row = 8 - (square[1] - '0');
  Square sq = squareOf(row, col);
  if (mailbox[sq] != Piece::NONE) {
    bb.removePiece(sq, mailbox[sq]);
    mailbox[sq] = Piece::NONE;
  }
  bb.setPiece(sq, piece);
  mailbox[sq] = piece;
}

/// Return whether a given move exists in the rules's possible-move list.
inline bool moveExists(const BitboardSet& bb, const Piece mailbox[], int fromRow, int fromCol, int toRow, int toCol, const PositionState& state = {}) {
  MoveList moves;
  movegen::getPossibleMoves(bb, mailbox, fromRow, fromCol, state, moves);
  for (int i = 0; i < moves.count; i++) {
    if (moves.targetRow(i) == toRow && moves.targetCol(i) == toCol)
      return true;
  }
  return false;
}

/// Algebraic square to (row, col). "e4" -> (4, 4).
inline void sq(const char* s, int& row, int& col) {
  col = s[0] - 'a';
  row = 8 - (s[1] - '0');
}

/// Build a MoveEntry for common test moves.
inline MoveEntry makeEntry(int fr, int fc, int tr, int tc, Piece piece,
                           Piece captured = Piece::NONE, Piece promo = Piece::NONE) {
  MoveEntry e = {};
  e.fromRow = fr; e.fromCol = fc; e.toRow = tr; e.toCol = tc;
  e.piece = piece; e.captured = captured; e.promotion = promo;
  e.isCapture = (captured != Piece::NONE);
  e.isEnPassant = false; e.epCapturedRow = -1;
  e.isCastling = false; e.isPromotion = (promo != Piece::NONE);
  e.isCheck = false;
  return e;
}

/// Compare two enum class values via their underlying integer representation.
/// Provides clear failure messages with numeric values (e.g., "Expected 1 Was 0").
#define TEST_ASSERT_ENUM_EQ(expected, actual) \
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

// Shared test globals — defined in test_shared.cpp, compiled into every suite.
extern BitboardSet bb;
extern Piece mailbox[64];
extern bool needsDefaultKings;

#endif // TEST_HELPERS_H
