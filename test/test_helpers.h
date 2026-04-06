#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <bitboard.h>
#include <epd.h>
#include <evaluation.h>
#include <fen.h>
#include <movegen.h>
#include <notation.h>
#include <position.h>
#include <search.h>
#include <utils.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Display-coordinate helpers (row/col ↔ LERF) — test-only utilities
// ---------------------------------------------------------------------------
// Row/col convention (matching game layer / firmware):
//   row 0 = rank 8 (black back rank), col 0 = file a.
//   col is identical to file.  row = 7 - rank.
// Production code uses rankOf/fileOf/makeSquare (LERF) instead.
// Firmware uses rowColToSquare/squareToRow/squareToCol from game/types.h.
// ---------------------------------------------------------------------------

constexpr Square squareOf(int row, int col) {
  return (7 - row) * 8 + col;
}

constexpr int rowOf(Square sq) {
  return 7 - (sq >> 3);  // 7 - rank
}

static uint64_t nowUs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch())
          .count());
}

static uint32_t chronoMillis() {
  using namespace std::chrono;
  static const auto epoch = steady_clock::now();
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now() - epoch).count());
}

/// Set up the standard initial chess position.
inline void setupInitialBoard(BitboardSet& bb, Piece mailbox[]) {
  bb.clear();
  memset(mailbox, 0, 64);
  for (Square sq = 0; sq < 64; ++sq) {
    Piece p = Position::INITIAL_BOARD[sq];
    if (p != Piece::NONE) {
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
  movegen::getPossibleMoves(bb, mailbox, squareOf(fromRow, fromCol), state, moves);
  for (int i = 0; i < moves.count; i++) {
    if (rowOf(moves.moves[i].to) == toRow && fileOf(moves.moves[i].to) == toCol)
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
  e.from = squareOf(fr, fc); e.to = squareOf(tr, tc);
  e.piece = piece; e.captured = captured; e.promotion = promo;
  e.flags = 0;
  if (captured != Piece::NONE) e.flags |= MR_CAPTURE;
  if (promo != Piece::NONE) e.flags |= MR_PROMOTION;
  e.epCapturedSq = SQ_NONE;
  return e;
}

/// Compare two enum class values via their underlying integer representation.
/// Provides clear failure messages with numeric values (e.g., "Expected 1 Was 0").
#define TEST_ASSERT_ENUM_EQ(expected, actual) \
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

// ---------------------------------------------------------------------------
// Search & EPD helpers — shared across position / benchmark / stats suites.
// ---------------------------------------------------------------------------

/// Get the directory containing the given source file.
/// Usage: testFileDir(__FILE__) returns the directory of the calling .cpp.
inline std::string testFileDir(const char* file) {
  std::string s(file);
  auto pos = s.find_last_of("/\\");
  return (pos != std::string::npos) ? s.substr(0, pos + 1) : "";
}

/// Load all EPD records from a file path.
inline std::vector<EPDRecord> loadEPDFile(const std::string& path) {
  std::vector<EPDRecord> records;
  std::ifstream in(path);
  if (!in.is_open()) return records;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    EPDRecord rec = epd::parseEPDLine(line);
    if (!rec.fen.empty()) records.push_back(rec);
  }
  return records;
}

/// Convert a Move to UCI coordinate string (e.g. "e2e4", "e7e8q").
inline std::string moveToStr(Move m) {
  char promo = ' ';
  if (m.isPromotion()) {
    static constexpr char PROMO_CHARS[] = {'n', 'b', 'r', 'q'};
    promo = PROMO_CHARS[m.promoIndex()];
  }
  return notation::toCoordinate(m.from, m.to, promo);
}

/// Parse a SAN move string into coordinate notation using the position context.
inline std::string sanToCoordinate(const Position& pos,
                                   const std::string& san) {
  Square from, to;
  char promotion = ' ';
  bool ok =
      notation::parseSAN(pos.bitboards(), pos.mailbox(), pos.positionState(),
                         pos.sideToMove(), san, from, to,
                         promotion);
  if (!ok) return "";
  return notation::toCoordinate(from, to, promotion);
}

/// Pre-allocated hash tables shared across test positions.
struct SharedTables {
  search::TranspositionTable tt;
  eval::PawnHashTable pawn;
  eval::EvalHashTable eval;

  void init() {
    tt.resize(search::DEFAULT_TT_SIZE);
    pawn.resize(eval::DEFAULT_PAWN_HASH_SIZE);
    eval.resize(eval::DEFAULT_EVAL_HASH_SIZE);
  }
  void clear() { tt.clear(); pawn.clear(); eval.clear(); }
  void free() { tt.free(); pawn.free(); eval.free(); }
};

// Shared test globals — defined in test_shared.cpp, compiled into every suite.
extern BitboardSet bb;
extern Piece mailbox[64];
extern bool needsDefaultKings;

// ---------------------------------------------------------------------------
// Relocated test-only helpers (removed from production headers)
// ---------------------------------------------------------------------------

/// Convert color character to Color enum (relocated from piece.h).
inline constexpr Color charToColor(char c) {
  return (c == 'b' || c == 'B') ? Color::BLACK : Color::WHITE;
}

/// Identify which piece sits on a square by scanning all 12 bitboards.
/// O(12) scan — use the mailbox for hot-path lookups instead.
/// Relocated from BitboardSet::pieceOn().
inline Piece pieceOn(const BitboardSet& bs, Square sq) {
  Bitboard bit = squareBB(sq);
  if (!(bs.occupied & bit)) return Piece::NONE;
  for (int i = 0; i < NUM_PIECE_BOARDS; ++i) {
    if (bs.byPiece[i] & bit) {
      Color c = (i < 6) ? Color::WHITE : Color::BLACK;
      auto t = static_cast<PieceType>((i % 6) + 1);
      return piece::makePiece(c, t);
    }
  }
  return Piece::NONE;
}

#endif // TEST_HELPERS_H
