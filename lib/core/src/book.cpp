#include "book.h"

#include <cstdint>

// ---------------------------------------------------------------------------
// Internal dependencies — used only in the book builder, not in the
// public header.  The book header exposes only <cstdint> types.
// ---------------------------------------------------------------------------

#include "piece.h"    // Piece, PieceType, Color, raw(), makePiece, pieceIndex
#include "zobrist.h"  // Keys, xorshift64

namespace LibreChess {
namespace book {

// ===========================================================================
// Opening book builder
//
// A lightweight "replay board" (64-byte mailbox + minimal state) replays
// coordinate-notation opening lines.  At each ply the Zobrist hash is
// computed and a BookEntry is recorded.  The final array is probed at
// runtime via linear scan with deduplication.
//
// On GCC 6+ (ESP32): fully constexpr — the entire book lives in .rodata
// (flash), zero RAM.  On GCC 5.x (MinGW native tests): runtime static
// initialization due to constexpr evaluator bugs that produce incorrect
// Zobrist hashes.  The conditional is transparent to callers.
//
// Reference: https://www.chessprogramming.org/Opening_Book
// ===========================================================================

// ---------------------------------------------------------------------------
// Zobrist key source — conditional on compiler version.
//
// GCC 6+ (ESP32): a file-local constexpr copy enables full compile-time
// evaluation, placing the entire book in .rodata (flash, zero RAM).
//
// GCC 5.x (MinGW native tests): the constexpr evaluator produces
// incorrect hashes for complex computations.  Fall back to the
// canonical runtime zobrist::KEYS.
//
// BOOK_CX expands to `constexpr` on GCC 6+ and to nothing on GCC 5.x,
// so builder functions are constexpr only when the toolchain is correct.
// ---------------------------------------------------------------------------

#if __GNUC__ > 5
static constexpr zobrist::Keys BOOK_KEYS{};
#define BOOK_CX constexpr
#else
static const zobrist::Keys& BOOK_KEYS = zobrist::KEYS;
#define BOOK_CX
#endif

// ---------------------------------------------------------------------------
// ReplayBoard — minimal board for move replay during book building.
//
// 64-byte mailbox (Piece enum values stored as uint8_t), castling rights
// bitmask, en passant file, and side-to-move flag.  Supports quiet moves,
// captures, double pawn pushes, castling, and en passant — everything
// needed for the first ~10 half-moves of standard openings.
//
// No promotion support (unnecessary within opening-book depth).
// ---------------------------------------------------------------------------

struct ReplayBoard {
  uint8_t squares[64]{};  // Piece raw values; 0 = empty
  uint8_t castling = 0x0F;  // bits: 0x01=WK 0x02=WQ 0x04=BK 0x08=BQ
  uint8_t epFile   = 0xFF;  // file of EP target (0–7), or 0xFF if none
  bool whiteToMove = true;

  // Set up the standard starting position.
  constexpr ReplayBoard() {
    // Back ranks
    squares[ 0] = raw(Piece::W_ROOK);
    squares[ 1] = raw(Piece::W_KNIGHT);
    squares[ 2] = raw(Piece::W_BISHOP);
    squares[ 3] = raw(Piece::W_QUEEN);
    squares[ 4] = raw(Piece::W_KING);
    squares[ 5] = raw(Piece::W_BISHOP);
    squares[ 6] = raw(Piece::W_KNIGHT);
    squares[ 7] = raw(Piece::W_ROOK);
    for (int f = 0; f < 8; ++f) squares[8  + f] = raw(Piece::W_PAWN);
    for (int f = 0; f < 8; ++f) squares[48 + f] = raw(Piece::B_PAWN);
    squares[56] = raw(Piece::B_ROOK);
    squares[57] = raw(Piece::B_KNIGHT);
    squares[58] = raw(Piece::B_BISHOP);
    squares[59] = raw(Piece::B_QUEEN);
    squares[60] = raw(Piece::B_KING);
    squares[61] = raw(Piece::B_BISHOP);
    squares[62] = raw(Piece::B_KNIGHT);
    squares[63] = raw(Piece::B_ROOK);
  }
};

// ---------------------------------------------------------------------------
// Constexpr helpers
// ---------------------------------------------------------------------------

constexpr int fileOf(int sq) { return sq & 7; }
constexpr int rankOf(int sq) { return sq >> 3; }

// Parse a square string like "e2" → LERF index.  Returns -1 on invalid.
constexpr int parseSquare(const char* s) {
  int file = s[0] - 'a';
  int rank = s[1] - '1';
  if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
  return rank * 8 + file;
}

// Check whether an adjacent enemy pawn can capture on the EP square.
// Pin-unaware — acceptable for standard opening positions where pinned
// EP captures are effectively impossible.
constexpr bool hasAdjacentEnemyPawn(const ReplayBoard& b, int epTargetSq) {
  // The capturing pawn must be on the same rank as the double-pushed pawn
  // (one rank behind the EP target from the capturer's perspective).
  int captureRank = b.whiteToMove ? 4 : 3;  // rank where the capturer sits
  int epFile = fileOf(epTargetSq);

  uint8_t pawnVal = b.whiteToMove ? raw(Piece::W_PAWN) : raw(Piece::B_PAWN);

  if (epFile > 0) {
    int adjSq = captureRank * 8 + (epFile - 1);
    if (b.squares[adjSq] == pawnVal) return true;
  }
  if (epFile < 7) {
    int adjSq = captureRank * 8 + (epFile + 1);
    if (b.squares[adjSq] == pawnVal) return true;
  }
  return false;
}

// Compute Zobrist hash for the current ReplayBoard state.
BOOK_CX uint64_t computeHash(const ReplayBoard& b) {
  uint64_t h = 0;

  for (int sq = 0; sq < 64; ++sq) {
    if (b.squares[sq] == 0) continue;
    Piece p = static_cast<Piece>(b.squares[sq]);
    int idx = piece::pieceIndex(p);
    h ^= BOOK_KEYS.pieces[idx][sq];
  }

  h ^= BOOK_KEYS.castling[b.castling];

  // EP key is only included when a legal EP capture exists (matching
  // Position::hash() semantics).
  if (b.epFile != 0xFF) {
    int epRank = b.whiteToMove ? 5 : 2;  // EP target rank (rank 6 or rank 3)
    int epSq = epRank * 8 + b.epFile;
    if (hasAdjacentEnemyPawn(b, epSq))
      h ^= BOOK_KEYS.enPassant[b.epFile];
  }

  if (!b.whiteToMove) h ^= BOOK_KEYS.sideToMove;

  return h;
}

// Castling rights update table — maps square index to rights bits to clear.
// When a piece moves from or to a square, the corresponding bits are removed.
constexpr uint8_t castlingMask(int sq) {
  switch (sq) {
    case  0: return ~0x02u & 0xFF;  // a1 — remove WQ
    case  4: return ~0x03u & 0xFF;  // e1 — remove WK+WQ
    case  7: return ~0x01u & 0xFF;  // h1 — remove WK
    case 56: return ~0x08u & 0xFF;  // a8 — remove BQ
    case 60: return ~0x0Cu & 0xFF;  // e8 — remove BK+BQ
    case 63: return ~0x04u & 0xFF;  // h8 — remove BK
    default: return 0xFF;           // no change
  }
}

// Apply a move on the ReplayBoard.  Handles quiet moves, captures, double
// pawn pushes, en passant captures, and castling.
constexpr void applyMove(ReplayBoard& b, int from, int to) {
  uint8_t piece = b.squares[from];
  uint8_t captured = b.squares[to];

  // Reset EP
  b.epFile = 0xFF;

  // --- En passant capture ---
  PieceType pt = static_cast<PieceType>(piece & 0x07);
  if (pt == PieceType::PAWN) {
    int diff = to - from;
    // Double pawn push → set EP file
    if (diff == 16 || diff == -16) {
      b.epFile = static_cast<uint8_t>(fileOf(from));
    }
    // Diagonal move to an empty square → en passant capture
    if ((diff == 7 || diff == 9 || diff == -7 || diff == -9) && captured == 0) {
      // Remove the captured pawn
      int capturedSq = b.whiteToMove ? (to - 8) : (to + 8);
      b.squares[capturedSq] = 0;
    }
  }

  // --- Castling ---
  if (pt == PieceType::KING) {
    int diff = to - from;
    if (diff == 2) {
      // Kingside castling — move the rook
      b.squares[from + 3] = 0;
      b.squares[from + 1] = b.whiteToMove ? raw(Piece::W_ROOK)
                                           : raw(Piece::B_ROOK);
    } else if (diff == -2) {
      // Queenside castling — move the rook
      b.squares[from - 4] = 0;
      b.squares[from - 1] = b.whiteToMove ? raw(Piece::W_ROOK)
                                           : raw(Piece::B_ROOK);
    }
  }

  // --- Move the piece ---
  b.squares[to] = piece;
  b.squares[from] = 0;

  // --- Update castling rights ---
  b.castling &= castlingMask(from);
  b.castling &= castlingMask(to);

  // --- Flip side to move ---
  b.whiteToMove = !b.whiteToMove;
}

// ---------------------------------------------------------------------------
// Constexpr book data builder
// ---------------------------------------------------------------------------

// Maximum entries across all lines.  48 lines × ~12 half-moves = ~576 max,
// but deduplication reduces this.  768 provides comfortable headroom.
static constexpr int MAX_BOOK_ENTRIES = 768;

struct BookData {
  BookEntry entries[MAX_BOOK_ENTRIES]{};
  int count = 0;
};

// Parse one opening line (space-separated coordinate moves like "e2e4 e7e5 ...")
// and append one BookEntry per half-move to `data`.
BOOK_CX void parseLine(BookData& data, const char* line) {
  ReplayBoard board;  // fresh starting position

  int i = 0;
  while (line[i] != '\0' && data.count < MAX_BOOK_ENTRIES) {
    // Skip whitespace
    while (line[i] == ' ') ++i;
    if (line[i] == '\0') break;

    // Parse "e2e4" — exactly 4 characters
    int from = parseSquare(&line[i]);
    int to   = parseSquare(&line[i + 2]);
    if (from < 0 || to < 0) break;

    // Record the entry *before* the move is applied.
    uint64_t h = computeHash(board);
    data.entries[data.count].hash = h;
    data.entries[data.count].from = static_cast<uint8_t>(from);
    data.entries[data.count].to   = static_cast<uint8_t>(to);
    ++data.count;

    applyMove(board, from, to);

    i += 4;
    // Skip optional promotion char (shouldn't appear, but be safe)
    if (line[i] != '\0' && line[i] != ' ') ++i;
  }
}

// ---------------------------------------------------------------------------
// Opening lines — ~48 curated lines covering all major opening families.
//
// Each line is ~8–12 half-moves of coordinate notation.  Lines are selected
// for breadth (covering major opening systems) rather than depth.
// Mainline theory from established sources.
// ---------------------------------------------------------------------------

// clang-format off
static constexpr const char* LINES[] = {

  // ===== Open Games (1.e4 e5) =====

  // Italian Game — Giuoco Piano
  "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3",
  // Ruy Lopez — Closed (Chigorin)
  "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3",
  // Ruy Lopez — Marshall Attack
  "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 e8g8 c2c3 d7d5",
  // Scotch Game
  "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 d4c6 b7c6 e4e5 d8e7 d1e2 f6d5",
  // Petroff Defence
  "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5 f1d3 b8c6 e1g1 f8e7",
  // Four Knights Game
  "e2e4 e7e5 g1f3 b8c6 b1c3 g8f6 f1b5 f8b4 e1g1 e8g8 d2d3 d7d6",
  // Berlin Defence
  "e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 f6e4 d2d4 f8e7 d1e2 e4d6 b5c6 b7c6 d4e5 d6b7",
  // King's Gambit Accepted
  "e2e4 e7e5 f2f4 e5f4 g1f3 g7g5 h2h4 g5g4 f3e5 g8f6 d2d4 d7d6 e5d3",

  // ===== Sicilian Defence =====

  // Najdorf
  "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1g5 e7e6 f2f4",
  // Dragon
  "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 c1e3 f8g7 f2f3 e8g8",
  // Scheveningen
  "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e6 f1e2 a7a6 e1g1 f8e7",
  // Sveshnikov
  "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5 d4b5 d7d6 c1g5 a7a6 b5a3",
  // Sicilian c3 (Alapin)
  "e2e4 c7c5 c2c3 d7d5 e4d5 d8d5 d2d4 g8f6 g1f3 b8c6 f1e2 c5d4 c3d4 e7e6",
  // Open Sicilian — Classical
  "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 b8c6 f1c4 e7e6 c1e3",

  // ===== French Defence =====

  // Winawer
  "e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 c7c5 a2a3 b4c3 b2c3 g8e7 g1f3",
  // Classical
  "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7 e4e5 f6d7 g5e7 d8e7 f2f4 e8g8",
  // Tarrasch
  "e2e4 e7e6 d2d4 d7d5 b1d2 g8f6 e4e5 f6d7 f1d3 c7c5 c2c3 b8c6 g1e2 c5d4 c3d4",
  // Exchange
  "e2e4 e7e6 d2d4 d7d5 e4d5 e6d5 g1f3 g8f6 f1d3 f8d6 e1g1 e8g8 c1g5",

  // ===== Caro-Kann Defence =====

  // Classical
  "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 h2h4 h7h6 g1f3 b8d7",
  // Advance
  "e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 f1d3 f5d3 d1d3 e7e6 b1c3 d8b6",
  // Two Knights
  "e2e4 c7c6 b1c3 d7d5 g1f3 c8g4 h2h3 g4f3 d1f3 e7e6 d2d4 g8f6",

  // ===== Other Semi-Open =====

  // Pirc Defence
  "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 f1c4 f8g7 d1e2 e8g8 g1f3 b8d7",
  // Scandinavian
  "e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6 g1f3 c8f5 f1c4 e7e6",

  // ===== QGD / QGA =====

  // QGD — Orthodox
  "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3 b8d7 a1c1 c7c6",
  // QGD — Exchange
  "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c4d5 e6d5 c1g5 c7c6 e2e3 f8e7 f1d3",
  // QGA — Main Line
  "d2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 e7e6 f1c4 c7c5 e1g1 a7a6 d1e2",
  // Ragozin
  "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 f8b4 c1g5 b8d7 e2e3 c7c5",

  // ===== Slav / Semi-Slav / Catalan =====

  // Slav
  "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5 f3e5 e7e6 f2f3",
  // Semi-Slav
  "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 e7e6 e2e3 b8d7 f1d3 f8d6",
  // Catalan — Open
  "d2d4 g8f6 c2c4 e7e6 g2g3 d7d5 f1g2 d5c4 g1f3 f8e7 e1g1 e8g8 d1c2 a7a6 c2c4",

  // ===== Indian Systems =====

  // Nimzo-Indian — Rubinstein
  "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5 g1f3 c7c5 e1g1 b8c6",
  // Nimzo-Indian — Classical
  "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 d1c2 e8g8 a2a3 b4c3 c2c3 b7b6 c1g5 c8b7",
  // Queen's Indian
  "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7 f1g2 f8e7 e1g1 e8g8 b1c3 f6e4 d1c2",
  // KID — Classical
  "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 e1g1 b8c6 d4d5 c6e7",
  // KID — Sämisch
  "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f3 e8g8 c1e3 e7e5 d4d5 f6h5",
  // Grünfeld — Exchange
  "d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5 e2e4 d5c3 b2c3 c7c5 f1c4 f8g7 g1e2",
  // Grünfeld — Russian System
  "d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 g1f3 f8g7 d1b3 d5c4 b3c4 e8g8 e2e4 c8g4",
  // Benoni — Modern
  "d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6 e2e4 g7g6 f1d3 f8g7 g1e2",

  // ===== Flank / Other =====

  // English — Symmetrical
  "c2c4 c7c5 b1c3 b8c6 g1f3 g8f6 g2g3 g7g6 f1g2 f8g7 e1g1 e8g8 d2d4 c5d4 f3d4",
  // English — Reversed Sicilian
  "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 f8b4 f1g2 e8g8 e1g1 e5e4 f3e1 b4c3 d2c3",
  // Réti Opening
  "g1f3 d7d5 c2c4 e7e6 g2g3 g8f6 f1g2 f8e7 e1g1 e8g8 b2b3 c7c5 c1b2 b8c6",
  // London System
  "d2d4 d7d5 c1f4 g8f6 e2e3 e7e6 g1f3 f8d6 f4g3 e8g8 f1d3 c7c5 c2c3 b8c6",
  // Dutch Defence — Leningrad
  "d2d4 f7f5 g2g3 g8f6 f1g2 g7g6 g1f3 f8g7 e1g1 e8g8 c2c4 d7d6 b1c3 b8c6",
};
// clang-format on

static constexpr int NUM_LINES = sizeof(LINES) / sizeof(LINES[0]);

// Build the complete book.
BOOK_CX BookData buildBook() {
  BookData data{};
  for (int i = 0; i < NUM_LINES; ++i) {
    parseLine(data, LINES[i]);
  }
  return data;
}

// ---------------------------------------------------------------------------
// GCC 5.x (MinGW, PlatformIO native tests) has constexpr evaluator bugs
// that produce incorrect Zobrist hashes.  GCC 6+ (ESP32 xtensa toolchain)
// evaluates correctly, placing the book in .rodata (flash, zero RAM).
// ---------------------------------------------------------------------------

#if __GNUC__ > 5
static constexpr BookData BOOK = buildBook();
#else
static const BookData BOOK = buildBook();
#endif

// ===========================================================================
// Runtime probe
// ===========================================================================

bool probe(uint64_t hash, uint8_t& from, uint8_t& to, uint64_t& rng) {
  // Linear scan collecting distinct matching (from, to) pairs.
  // ~500 entries — trivially fast for a once-per-move probe.
  struct Candidate { uint8_t from; uint8_t to; };
  Candidate candidates[16];  // no position has >16 book moves
  int count = 0;

  for (int i = 0; i < BOOK.count; ++i) {
    if (BOOK.entries[i].hash != hash) continue;

    // Deduplicate: skip if we already have this (from, to).
    bool dup = false;
    for (int j = 0; j < count; ++j) {
      if (candidates[j].from == BOOK.entries[i].from &&
          candidates[j].to   == BOOK.entries[i].to) {
        dup = true;
        break;
      }
    }
    if (!dup && count < 16) {
      candidates[count++] = {BOOK.entries[i].from, BOOK.entries[i].to};
    }
  }

  if (count == 0) return false;

  // Select one uniformly at random using xorshift64 PRNG.
  rng = zobrist::xorshift64(rng);
  int idx = static_cast<int>(rng % static_cast<uint64_t>(count));

  from = candidates[idx].from;
  to   = candidates[idx].to;
  return true;
}

int entryCount() {
  return BOOK.count;
}

}  // namespace book
}  // namespace LibreChess
