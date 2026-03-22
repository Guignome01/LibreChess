#include "evaluation.h"
#include "attacks.h"
#include "zobrist.h"

#include <cstring>       // memset

// Under TUNING, eval constants are mutable with external linkage so the
// tuner (trace.cpp) can read/write them at runtime.  Production builds
// keep full constexpr optimisation with file-local linkage.
//
//   EVAL_CONST  — tunable parameters (mutable in tuning builds).
//   EVAL_FIXED  — non-tunable constants that trace.cpp must see.
#ifdef TUNING
#define EVAL_CONST              // mutable, external linkage
#define EVAL_FIXED const        // immutable, external linkage
#else
#define EVAL_CONST static constexpr   // immutable, file-local
#define EVAL_FIXED static constexpr   // immutable, file-local
#endif

namespace {

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Pawn-structure masks — file-local, initialized lazily.
// ---------------------------------------------------------------------------

Bitboard pawnPassedMask[2][64] = {};
Bitboard pawnIsolatedMask[8] = {};
Bitboard pawnForwardMask[2][64] = {};

Bitboard adjacentFilesMask(int file) {
  Bitboard mask = 0;
  if (file > 0) mask |= fileBB(file - 1);
  if (file < 7) mask |= fileBB(file + 1);
  return mask;
}

void initPawnMasksImpl() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  for (int file = 0; file < 8; ++file)
    pawnIsolatedMask[file] = adjacentFilesMask(file);

  for (Square sq = 0; sq < 64; ++sq) {
    const int rank = sq / 8;  // 0=rank1, 7=rank8 (LERF)
    const int file = sq & 7;

    Bitboard sameAndAdjacentFiles = fileBB(file);
    if (file > 0) sameAndAdjacentFiles |= fileBB(file - 1);
    if (file < 7) sameAndAdjacentFiles |= fileBB(file + 1);

    Bitboard whitePassed = 0, blackPassed = 0;
    Bitboard whiteForward = 0, blackForward = 0;

    for (int r = rank + 1; r < 8; ++r) {
      Bitboard rankMask = rankBB(r);
      whitePassed |= sameAndAdjacentFiles & rankMask;
      whiteForward |= fileBB(file) & rankMask;
    }
    for (int r = rank - 1; r >= 0; --r) {
      Bitboard rankMask = rankBB(r);
      blackPassed |= sameAndAdjacentFiles & rankMask;
      blackForward |= fileBB(file) & rankMask;
    }

    pawnPassedMask[raw(Color::WHITE)][sq] = whitePassed;
    pawnPassedMask[raw(Color::BLACK)][sq] = blackPassed;
    pawnForwardMask[raw(Color::WHITE)][sq] = whiteForward;
    pawnForwardMask[raw(Color::BLACK)][sq] = blackForward;
  }
}

}  // anonymous namespace

// ===========================================================================
// eval — evaluation constants, helpers, and public API
// ===========================================================================

namespace LibreChess {
namespace eval {

// ---------------------------------------------------------------------------
// Material values indexed by piece type offset (P=0 N=1 B=2 R=3 Q=4 K=5).
// In centipawns to avoid mixing float/int arithmetic in the inner loop.
// ---------------------------------------------------------------------------
EVAL_CONST int MATERIAL[] = {100, 300, 300, 500, 900, 0};

// ---------------------------------------------------------------------------
// Piece-square tables — centipawns, LERF order (a1=0, h8=63).
// White's perspective; black mirrors via (sq ^ 56).
// Based on the simplified evaluation function (CPW / Tomasz Michniewski).
//
// Midgame (MG) tables: king should hide behind pawns, minor pieces want
// center control. Endgame (EG) tables: king should be active and central,
// passed pawns and advancement matter more.
// ---------------------------------------------------------------------------

// clang-format off

// --- Midgame PSTs ---

EVAL_CONST int PST_PAWN_MG[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,   // rank 1 (never occupied)
  50, 50, 50, 50, 50, 50, 50, 50,   // rank 2
  10, 10, 20, 30, 30, 20, 10, 10,   // rank 3
   5,  5, 10, 25, 25, 10,  5,  5,   // rank 4
   0,  0,  0, 20, 20,  0,  0,  0,   // rank 5
   5, -5,-10,  0,  0,-10, -5,  5,   // rank 6
   5, 10, 10,-20,-20, 10, 10,  5,   // rank 7
   0,  0,  0,  0,  0,  0,  0,  0    // rank 8 (never occupied)
};

EVAL_CONST int PST_KNIGHT_MG[64] = {
 -50,-40,-30,-30,-30,-30,-40,-50,
 -40,-20,  0,  0,  0,  0,-20,-40,
 -30,  0, 10, 15, 15, 10,  0,-30,
 -30,  5, 15, 20, 20, 15,  5,-30,
 -30,  0, 15, 20, 20, 15,  0,-30,
 -30,  5, 10, 15, 15, 10,  5,-30,
 -40,-20,  0,  5,  5,  0,-20,-40,
 -50,-40,-30,-30,-30,-30,-40,-50
};

EVAL_CONST int PST_BISHOP_MG[64] = {
 -20,-10,-10,-10,-10,-10,-10,-20,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -10,  0, 10, 10, 10, 10,  0,-10,
 -10,  5,  5, 10, 10,  5,  5,-10,
 -10,  0,  5, 10, 10,  5,  0,-10,
 -10, 10, 10, 10, 10, 10, 10,-10,
 -10,  5,  0,  0,  0,  0,  5,-10,
 -20,-10,-10,-10,-10,-10,-10,-20
};

EVAL_CONST int PST_ROOK_MG[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,
   5, 10, 10, 10, 10, 10, 10,  5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
   0,  0,  0,  5,  5,  0,  0,  0
};

EVAL_CONST int PST_QUEEN_MG[64] = {
 -20,-10,-10, -5, -5,-10,-10,-20,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -10,  0,  5,  5,  5,  5,  0,-10,
  -5,  0,  5,  5,  5,  5,  0, -5,
   0,  0,  5,  5,  5,  5,  0, -5,
 -10,  5,  5,  5,  5,  5,  0,-10,
 -10,  0,  5,  0,  0,  0,  0,-10,
 -20,-10,-10, -5, -5,-10,-10,-20
};

EVAL_CONST int PST_KING_MG[64] = {
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -20,-30,-30,-40,-40,-30,-30,-20,
 -10,-20,-20,-20,-20,-20,-20,-10,
  20, 20,  0,  0,  0,  0, 20, 20,
  20, 30, 10,  0,  0, 10, 30, 20
};

// --- Endgame PSTs ---

EVAL_CONST int PST_PAWN_EG[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,
  70, 70, 70, 70, 70, 70, 70, 70,
  40, 40, 40, 40, 40, 40, 40, 40,
  20, 20, 20, 20, 20, 20, 20, 20,
  10, 10, 10, 10, 10, 10, 10, 10,
   5,  5,  5,  5,  5,  5,  5,  5,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0 
};

EVAL_CONST int PST_KNIGHT_EG[64] = {
 -40,-20,-15,-15,-15,-15,-20,-40,
 -20, -5,  5,  5,  5,  5, -5,-20,
 -15,  5, 15, 15, 15, 15,  5,-15,
 -15,  5, 15, 20, 20, 15,  5,-15,
 -15,  5, 15, 20, 20, 15,  5,-15,
 -15,  5, 15, 15, 15, 15,  5,-15,
 -20, -5,  5,  5,  5,  5, -5,-20,
 -40,-20,-15,-15,-15,-15,-20,-40
};

EVAL_CONST int PST_BISHOP_EG[64] = {
 -15, -5, -5, -5, -5, -5, -5,-15,
  -5,  5,  5,  5,  5,  5,  5, -5,
  -5,  5, 10, 10, 10, 10,  5, -5,
  -5,  5, 10, 15, 15, 10,  5, -5,
  -5,  5, 10, 15, 15, 10,  5, -5,
  -5,  5, 10, 10, 10, 10,  5, -5,
  -5,  5,  5,  5,  5,  5,  5, -5,
 -15, -5, -5, -5, -5, -5, -5,-15
};

EVAL_CONST int PST_ROOK_EG[64] = {
   5,  5,  5,  5,  5,  5,  5,  5,
  15, 15, 15, 15, 15, 15, 15, 15,
   0,  0,  5,  5,  5,  5,  0,  0,
   0,  0,  5,  5,  5,  5,  0,  0,
   0,  0,  5,  5,  5,  5,  0,  0,
   0,  0,  5,  5,  5,  5,  0,  0,
   0,  0,  5,  5,  5,  5,  0,  0,
   5,  5,  5, 10, 10,  5,  5,  5
};

EVAL_CONST int PST_QUEEN_EG[64] = {
 -15,-10, -5, -5, -5, -5,-10,-15,
 -10,  0,  5,  5,  5,  5,  0,-10,
  -5,  5, 10, 10, 10, 10,  5, -5,
  -5,  5, 10, 15, 15, 10,  5, -5,
  -5,  5, 10, 15, 15, 10,  5, -5,
  -5,  5, 10, 10, 10, 10,  5, -5,
 -10,  0,  5,  5,  5,  5,  0,-10,
 -15,-10, -5, -5, -5, -5,-10,-15
};

EVAL_CONST int PST_KING_EG[64] = {
 -20,-10,-10,-10,-10,-10,-10,-20,
 -10,  0, 10, 10, 10, 10,  0,-10,
 -10, 10, 20, 20, 20, 20, 10,-10,
 -10, 10, 20, 30, 30, 20, 10,-10,
 -10, 10, 20, 30, 30, 20, 10,-10,
 -10, 10, 20, 20, 20, 20, 10,-10,
 -10,  0, 10, 10, 10, 10,  0,-10,
 -20,-10,-10,-10,-10,-10,-10,-20
};

// clang-format on

// Indexed by piece type offset (PAWN=0 .. KING=5).
// File-local PST pointer lookup tables.
static const int* const PST_MG[6] = {
    PST_PAWN_MG, PST_KNIGHT_MG, PST_BISHOP_MG,
    PST_ROOK_MG, PST_QUEEN_MG,  PST_KING_MG};

static const int* const PST_EG[6] = {
    PST_PAWN_EG, PST_KNIGHT_EG, PST_BISHOP_EG,
    PST_ROOK_EG, PST_QUEEN_EG,  PST_KING_EG};

// ---------------------------------------------------------------------------
// Pawn-structure query functions (public for testing).
// Use the file-local mask arrays initialized by initPawnMasksImpl().
// ---------------------------------------------------------------------------

int materialValue(PieceType pt) {
  int idx = static_cast<int>(pt);
  if (idx < 1 || idx > 5) return 0;
  return MATERIAL[idx - 1];
}

// ---------------------------------------------------------------------------
// Incremental material+PST helpers
//
// Precondition: pieceIdx must be in [0, 11] (a valid pieceZobristIndex).
// Callers obtaining pieceIdx from pieceZobristIndex() must ensure the
// piece is not NONE before calling — ZOBRIST_IDX_NONE (-1) would cause
// OOB access into MATERIAL[] and PST arrays.
// ---------------------------------------------------------------------------

int pieceSquareMG(int pieceIdx, Square sq) {
  int type = pieceIdx < 6 ? pieceIdx : pieceIdx - 6;
  int val = MATERIAL[type] + PST_MG[type][pieceIdx < 6 ? sq : (sq ^ 56)];
  return pieceIdx < 6 ? val : -val;
}

int pieceSquareEG(int pieceIdx, Square sq) {
  int type = pieceIdx < 6 ? pieceIdx : pieceIdx - 6;
  int val = MATERIAL[type] + PST_EG[type][pieceIdx < 6 ? sq : (sq ^ 56)];
  return pieceIdx < 6 ? val : -val;
}

int computeMaterialPST_MG(const BitboardSet& bb) {
  int score = 0;
  for (int i = 0; i < 6; ++i) {
    Bitboard white = bb.byPiece[i];
    while (white) { score += MATERIAL[i] + PST_MG[i][popLsb(white)]; }
    Bitboard black = bb.byPiece[i + 6];
    while (black) { score -= MATERIAL[i] + PST_MG[i][popLsb(black) ^ 56]; }
  }
  return score;
}

int computeMaterialPST_EG(const BitboardSet& bb) {
  int score = 0;
  for (int i = 0; i < 6; ++i) {
    Bitboard white = bb.byPiece[i];
    while (white) { score += MATERIAL[i] + PST_EG[i][popLsb(white)]; }
    Bitboard black = bb.byPiece[i + 6];
    while (black) { score -= MATERIAL[i] + PST_EG[i][popLsb(black) ^ 56]; }
  }
  return score;
}

// ---------------------------------------------------------------------------

void initPawnMasks() { initPawnMasksImpl(); }

bool isPassed(Square sq, Color color, Bitboard enemyPawns) {
  return (enemyPawns & pawnPassedMask[raw(color)][sq]) == 0;
}

bool isIsolated(Square sq, Bitboard friendlyPawns) {
  return (friendlyPawns & pawnIsolatedMask[colOf(sq)]) == 0;
}

bool isDoubled(Square sq, Color color, Bitboard friendlyPawns) {
  return (friendlyPawns & pawnForwardMask[raw(color)][sq]) != 0;
}

bool isBackward(Square sq, Color color, Bitboard friendlyPawns, Bitboard enemyPawnAttacks) {
  const int file = colOf(sq);
  Bitboard adjacentPawns = friendlyPawns & pawnIsolatedMask[file];
  if (!adjacentPawns) return false;

  Square nextSq = SQ_NONE;
  if (color == Color::WHITE) {
    if (sq >= 56) return false;
    nextSq = sq + 8;
  } else {
    if (sq < 8) return false;
    nextSq = sq - 8;
  }

  Bitboard nextSquare = squareBB(nextSq);
  if ((enemyPawnAttacks & nextSquare) == 0) return false;

  Bitboard adjacentAttacks = (color == Color::WHITE)
      ? (shiftNE(adjacentPawns) | shiftNW(adjacentPawns))
      : (shiftSE(adjacentPawns) | shiftSW(adjacentPawns));

  return (adjacentAttacks & nextSquare) == 0;
}

// ---------------------------------------------------------------------------
// Pawn structure constants — centipawns.
//
// Passed pawn bonuses use exponential rank-based scaling: advanced passers
// are worth dramatically more than newly created ones.  Indexed by LERF
// rank (0=rank1, 7=rank8).  Indices 0 and 7 are unused (pawns can't
// occupy rank 1 or rank 8).
//
// Reference: https://www.chessprogramming.org/Passed_Pawn
// ---------------------------------------------------------------------------

EVAL_CONST int PASSED_RANK_BONUS_MG[] = {0, 18, 8, 4, 20, 24, 20, 0};
EVAL_CONST int PASSED_RANK_BONUS_EG[] = {0, 0, 5, 33, 69, 138, 199, 0};
EVAL_CONST int CONNECTED_PASSED    =  0;
EVAL_CONST int ISOLATED_PENALTY    = -10;
EVAL_CONST int DOUBLED_PENALTY     =  0;
EVAL_CONST int BACKWARD_PENALTY    =  0;

// Protected passed pawn — extra bonus when a passer is defended by a pawn.
// Reference: https://www.chessprogramming.org/Passed_Pawn#Protected
EVAL_CONST int PROTECTED_PASSER_MG = 17;
EVAL_CONST int PROTECTED_PASSER_EG = 20;

// Candidate passed pawn — bonus for a pawn that could become passed with
// one favorable exchange (only one blocking enemy pawn).
// Reference: https://www.chessprogramming.org/Candidate_Passed_Pawn
EVAL_CONST int CANDIDATE_PASSER_MG = 14;
EVAL_CONST int CANDIDATE_PASSER_EG = 41;

// ---------------------------------------------------------------------------
// Pawn structure scoring — centipawns, white-relative.
// ---------------------------------------------------------------------------

static void evalPawnStructure(const BitboardSet& bb,
                              Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                              int& mgScore, int& egScore,
                              PawnHashTable* pawnHash) {
  Bitboard whitePawns = bb.byPiece[0];   // pieceZobristIndex(W_PAWN) = 0
  Bitboard blackPawns = bb.byPiece[6];   // pieceZobristIndex(B_PAWN) = 6

  if (!whitePawns && !blackPawns) return;

  // --- Pawn hash probe ---
  uint64_t pHash = 0;
  if (pawnHash) {
    pHash = zobrist::computePawnHash(bb);
    const PawnEntry* cached = pawnHash->probe(pHash);
    if (cached) {
      mgScore += cached->mgScore;
      egScore += cached->egScore;
      return;
    }
  }

  Bitboard blackPawnAttacks = blackPawnAtk;
  Bitboard whitePawnAttacks = whitePawnAtk;

  int mg = 0, eg = 0;
  uint8_t whitePassedFiles = 0;
  uint8_t blackPassedFiles = 0;

  // --- White pawns ---
  Bitboard wp = whitePawns;
  while (wp) {
    Square sq = popLsb(wp);
    int rank = sq / 8;

    if (isPassed(sq, Color::WHITE, blackPawns)) {
      mg += PASSED_RANK_BONUS_MG[rank];
      eg += PASSED_RANK_BONUS_EG[rank];
      whitePassedFiles |= 1 << (sq & 7);
      // Protected passer — defended by another pawn
      if (squareBB(sq) & whitePawnAttacks) {
        mg += PROTECTED_PASSER_MG;
        eg += PROTECTED_PASSER_EG;
      }
    } else {
      // Candidate passer — only one enemy pawn blocks it
      Bitboard blockers = blackPawns & pawnPassedMask[raw(Color::WHITE)][sq];
      if (popcount(blockers) == 1) {
        mg += CANDIDATE_PASSER_MG;
        eg += CANDIDATE_PASSER_EG;
      }
    }
    if (isIsolated(sq, whitePawns)) {
      mg += ISOLATED_PENALTY;
      eg += ISOLATED_PENALTY;
    }
    if (isDoubled(sq, Color::WHITE, whitePawns)) {
      mg += DOUBLED_PENALTY;
      eg += DOUBLED_PENALTY;
    }
    if (isBackward(sq, Color::WHITE, whitePawns, blackPawnAttacks)) {
      mg += BACKWARD_PENALTY;
      eg += BACKWARD_PENALTY;
    }
  }

  // --- Black pawns ---
  Bitboard bp = blackPawns;
  while (bp) {
    Square sq = popLsb(bp);
    int rank = sq / 8;

    if (isPassed(sq, Color::BLACK, whitePawns)) {
      mg -= PASSED_RANK_BONUS_MG[7 - rank];
      eg -= PASSED_RANK_BONUS_EG[7 - rank];
      blackPassedFiles |= 1 << (sq & 7);
      // Protected passer — defended by another pawn
      if (squareBB(sq) & blackPawnAttacks) {
        mg -= PROTECTED_PASSER_MG;
        eg -= PROTECTED_PASSER_EG;
      }
    } else {
      // Candidate passer — only one enemy pawn blocks it
      Bitboard blockers = whitePawns & pawnPassedMask[raw(Color::BLACK)][sq];
      if (popcount(blockers) == 1) {
        mg -= CANDIDATE_PASSER_MG;
        eg -= CANDIDATE_PASSER_EG;
      }
    }
    if (isIsolated(sq, blackPawns)) {
      mg -= ISOLATED_PENALTY;
      eg -= ISOLATED_PENALTY;
    }
    if (isDoubled(sq, Color::BLACK, blackPawns)) {
      mg -= DOUBLED_PENALTY;
      eg -= DOUBLED_PENALTY;
    }
    if (isBackward(sq, Color::BLACK, blackPawns, whitePawnAttacks)) {
      mg -= BACKWARD_PENALTY;
      eg -= BACKWARD_PENALTY;
    }
  }

  // Connected passed pawns — bonus per adjacent-file pair of passed pawns.
  for (int f = 0; f < 7; ++f) {
    if ((whitePassedFiles >> f & 1) && (whitePassedFiles >> (f + 1) & 1)) {
      mg += CONNECTED_PASSED;
      eg += CONNECTED_PASSED;
    }
    if ((blackPassedFiles >> f & 1) && (blackPassedFiles >> (f + 1) & 1)) {
      mg -= CONNECTED_PASSED;
      eg -= CONNECTED_PASSED;
    }
  }

  mgScore += mg;
  egScore += eg;

  // --- Pawn hash store ---
  if (pawnHash) {
    pawnHash->store(pHash, static_cast<int16_t>(mg), static_cast<int16_t>(eg));
  }
}

// ---------------------------------------------------------------------------
// Bishop pair — bonus when a side has both bishops.
// The bishop pair is one of the most consistently impactful evaluation terms
// because two bishops complement each other's color coverage.
//
// Values: +30cp MG, +50cp EG (EG bonus is higher because open board favors
// the long-range diagonal pair).
//
// Reference: https://www.chessprogramming.org/Bishop_Pair
// ---------------------------------------------------------------------------

EVAL_CONST int BISHOP_PAIR_MG = 76;
EVAL_CONST int BISHOP_PAIR_EG = 17;

static void evalBishopPair(const BitboardSet& bb,
                           int& mgScore, int& egScore) {
  // bb.byPiece[2] = W_BISHOP, bb.byPiece[8] = B_BISHOP
  if (popcount(bb.byPiece[2]) >= 2) {
    mgScore += BISHOP_PAIR_MG;
    egScore += BISHOP_PAIR_EG;
  }
  if (popcount(bb.byPiece[8]) >= 2) {
    mgScore -= BISHOP_PAIR_MG;
    egScore -= BISHOP_PAIR_EG;
  }
}

// ---------------------------------------------------------------------------
// Rook on open/semi-open file — bonus for rooks not blocked by own pawns.
//
// Open file (no pawns at all):       +20cp
// Semi-open file (no friendly pawns): +10cp
//
// Reference: https://www.chessprogramming.org/Rook_on_Open_File
// ---------------------------------------------------------------------------

EVAL_CONST int ROOK_OPEN_FILE      = 3;
EVAL_CONST int ROOK_SEMI_OPEN_FILE = 16;

static void evalRookFiles(const BitboardSet& bb,
                          int& mgScore, int& egScore) {
  Bitboard whitePawns = bb.byPiece[0];
  Bitboard blackPawns = bb.byPiece[6];
  Bitboard allPawns   = whitePawns | blackPawns;

  // White rooks (index 3)
  Bitboard wr = bb.byPiece[3];
  while (wr) {
    Square sq = popLsb(wr);
    Bitboard file = fileBB(colOf(sq));
    if (!(file & allPawns)) {
      mgScore += ROOK_OPEN_FILE;
      egScore += ROOK_OPEN_FILE;
    } else if (!(file & whitePawns)) {
      mgScore += ROOK_SEMI_OPEN_FILE;
      egScore += ROOK_SEMI_OPEN_FILE;
    }
  }

  // Black rooks (index 9)
  Bitboard br = bb.byPiece[9];
  while (br) {
    Square sq = popLsb(br);
    Bitboard file = fileBB(colOf(sq));
    if (!(file & allPawns)) {
      mgScore -= ROOK_OPEN_FILE;
      egScore -= ROOK_OPEN_FILE;
    } else if (!(file & blackPawns)) {
      mgScore -= ROOK_SEMI_OPEN_FILE;
      egScore -= ROOK_SEMI_OPEN_FILE;
    }
  }
}

// ---------------------------------------------------------------------------
// Rook on 7th rank — bonus when a rook is on the opponent's second rank
// (rank 7 for white, rank 2 for black) and the enemy king is on the back
// rank. Rooks on the 7th cut off the king and attack pawns from behind.
//
// Values: +20cp MG, +30cp EG.
//
// Reference: https://www.chessprogramming.org/Rook_on_Seventh
// ---------------------------------------------------------------------------

EVAL_CONST int ROOK_7TH_MG = 20;
EVAL_CONST int ROOK_7TH_EG = 38;

static void evalRookOnSeventh(const BitboardSet& bb,
                              int& mgScore, int& egScore) {
  // LERF: rank 7 = ranks with bits 48-55, rank 2 = bits 8-15.
  // rank 7 mask: rankBB(6), rank 8 mask: rankBB(7).
  // rank 2 mask: rankBB(1), rank 1 mask: rankBB(0).
  Bitboard rank7 = rankBB(6);
  Bitboard rank8 = rankBB(7);
  Bitboard rank2 = rankBB(1);
  Bitboard rank1 = rankBB(0);

  // White rooks on rank 7 with black king on rank 8
  if ((bb.byPiece[3] & rank7) && (bb.byPiece[11] & rank8)) {
    int count = popcount(bb.byPiece[3] & rank7);
    mgScore += ROOK_7TH_MG * count;
    egScore += ROOK_7TH_EG * count;
  }

  // Black rooks on rank 2 with white king on rank 1
  if ((bb.byPiece[9] & rank2) && (bb.byPiece[5] & rank1)) {
    int count = popcount(bb.byPiece[9] & rank2);
    mgScore -= ROOK_7TH_MG * count;
    egScore -= ROOK_7TH_EG * count;
  }
}

// ---------------------------------------------------------------------------
// Mobility — per-piece attack count bonus.
//
// Pieces that control more squares are worth more. We count the number of
// squares each piece attacks (excluding squares occupied by friendly pieces)
// and apply a small per-square bonus. Weight varies by piece type:
//   Knight/Bishop: +4cp per square
//   Rook:          +2cp per square
//   Queen:         +1cp per square
//
// Queen gets a lower weight because it always has high mobility, so
// differentials are less meaningful (also avoids over-rewarding queen
// centralization at the expense of development).
//
// Reference: https://www.chessprogramming.org/Mobility
// ---------------------------------------------------------------------------

EVAL_CONST int MOBILITY_KNIGHT_MG = 15;
EVAL_CONST int MOBILITY_KNIGHT_EG = 12;
EVAL_CONST int MOBILITY_BISHOP_MG = 3;
EVAL_CONST int MOBILITY_BISHOP_EG = 12;
EVAL_CONST int MOBILITY_ROOK_MG   = 0;
EVAL_CONST int MOBILITY_ROOK_EG   = 12;
EVAL_CONST int MOBILITY_QUEEN_MG  = 4;
EVAL_CONST int MOBILITY_QUEEN_EG  = 16;

static void evalMobility(const BitboardSet& bb,
                         const attacks::AttackInfo& info,
                         int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Bitboard friendly = bb.byColor[c];

    int knightMob = popcount(info.byPiece[c][2] & ~friendly);
    int bishopMob = popcount(info.byPiece[c][3] & ~friendly);
    int rookMob   = popcount(info.byPiece[c][4] & ~friendly);
    int queenMob  = popcount(info.byPiece[c][5] & ~friendly);

    int mgBonus = knightMob * MOBILITY_KNIGHT_MG
                + bishopMob * MOBILITY_BISHOP_MG
                + rookMob   * MOBILITY_ROOK_MG
                + queenMob  * MOBILITY_QUEEN_MG;

    int egBonus = knightMob * MOBILITY_KNIGHT_EG
                + bishopMob * MOBILITY_BISHOP_EG
                + rookMob   * MOBILITY_ROOK_EG
                + queenMob  * MOBILITY_QUEEN_EG;

    mgScore += sign * mgBonus;
    egScore += sign * egBonus;
  }
}

// ---------------------------------------------------------------------------
// King safety / pawn shield — midgame only.
//
// When the king is castled (files a-c or f-h), a bonus/penalty is given
// based on the integrity of the pawn shield in front of the king:
//   - Missing shield pawn:                -15cp per pawn
//   - Shield pawn advanced beyond rank 3: -5cp per pawn
//   - Open file directly in front of king: -20cp (additional)
//
// If the king is in the center (files d-e), no shield evaluation applies
// (the penalty from the MG king PST already discourages a central king).
//
// This score is applied to MG only — in endgames, king safety is irrelevant
// and king centralization (via EG king PST) takes over.
//
// Reference: https://www.chessprogramming.org/King_Safety
// ---------------------------------------------------------------------------

EVAL_CONST int SHIELD_MISSING_PAWN  = -14;
EVAL_CONST int SHIELD_ADVANCED_PAWN = -14;
EVAL_CONST int SHIELD_OPEN_FILE     = -29;

// Evaluate pawn shield for one side. Returns MG bonus (positive = good).
static int evalShieldOneSide(const BitboardSet& bb, Color color) {
  uint8_t c = raw(color);
  Square kingSq = 0;

  // Find king square
  Bitboard king = bb.byPiece[c == 0 ? 5 : 11];
  if (!king) return 0;
  kingSq = lsb(king);

  int kingFile = colOf(kingSq);
  int kingRank = rowOf(kingSq);  // 0-based from white's perspective

  // Only apply if king is on a flank (files 0-2 or 5-7)
  if (kingFile >= 3 && kingFile <= 4) return 0;

  // Determine the 3 shield files
  int shieldFiles[3];
  if (kingFile <= 2) {
    shieldFiles[0] = 0; shieldFiles[1] = 1; shieldFiles[2] = 2;
  } else {
    shieldFiles[0] = 5; shieldFiles[1] = 6; shieldFiles[2] = 7;
  }

  // Friendly pawns bitboard
  Bitboard friendlyPawns = bb.byPiece[c == 0 ? 0 : 6];
  Bitboard allPawns = bb.byPiece[0] | bb.byPiece[6];

  int score = 0;

  for (int i = 0; i < 3; ++i) {
    int f = shieldFiles[i];
    Bitboard fileMask = fileBB(f);
    Bitboard shieldPawns = friendlyPawns & fileMask;

    if (!shieldPawns) {
      // Missing pawn in the shield
      score += SHIELD_MISSING_PAWN;
    } else {
      // Check if the pawn is advanced (beyond rank 3 for white, rank 6 for black)
      // In LERF: white rank 3 = index 2, black rank 6 = index 5
      if (color == Color::WHITE) {
        // White pawns should be on ranks 1-2 (LERF rank 0-1). If on rank >= 2
        // (i.e. rank 3+), it's advanced. Check the frontmost pawn.
        // Rank in LERF: sq/8. Rank 2 = 16-23 → rank index 2 → board rank 3.
        Bitboard copy = shieldPawns;
        while (copy) {
          Square sq = popLsb(copy);
          int rank = sq / 8;
          if (rank >= 2) score += SHIELD_ADVANCED_PAWN;
        }
      } else {
        // Black pawns should be on ranks 6-7 (LERF rank 5-6). If on rank <= 5
        // (i.e. rank 6+), it's advanced.
        Bitboard copy = shieldPawns;
        while (copy) {
          Square sq = popLsb(copy);
          int rank = sq / 8;
          if (rank <= 5) score += SHIELD_ADVANCED_PAWN;
        }
      }
    }

    // Additional penalty for open file (no pawns at all)
    if (f == kingFile && !(allPawns & fileMask)) {
      score += SHIELD_OPEN_FILE;
    }
  }

  return score;
}

static void evalKingSafety(const BitboardSet& bb,
                           int& mgScore, int& /* egScore */) {
  mgScore += evalShieldOneSide(bb, Color::WHITE);
  mgScore -= evalShieldOneSide(bb, Color::BLACK);
}

// ---------------------------------------------------------------------------
// Central square constants (LERF) — used by center control and outposts.
// ---------------------------------------------------------------------------

constexpr Square SQ_D4 = 27, SQ_D5 = 35, SQ_E4 = 28, SQ_E5 = 36;

// Center mask: the four central squares {d4, d5, e4, e5}.
EVAL_FIXED Bitboard CENTER_MASK =
    squareBB(SQ_D4) | squareBB(SQ_D5) | squareBB(SQ_E4) | squareBB(SQ_E5);

// ---------------------------------------------------------------------------
// Center Control — bonus for pawns occupying or attacking centre squares.
//
// Controlling the centre (d4/d5/e4/e5) is a fundamental positional goal.
// Pawns in the centre provide a stable presence, while pawns that attack
// centre squares exert influence without committing.
//
// Values: +15cp per pawn occupying a centre square,
//         +5cp  per centre square attacked by a friendly pawn.
// Applied to both MG and EG.
//
// Reference: https://www.chessprogramming.org/Center_Control
// ---------------------------------------------------------------------------

EVAL_CONST int CENTER_OCCUPATION_BONUS = 3;
EVAL_CONST int CENTER_ATTACK_BONUS     = 6;

static void evalCenterControl(const BitboardSet& bb,
                              Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                              int& mgScore, int& egScore) {
  Bitboard whitePawns = bb.byPiece[0];
  Bitboard blackPawns = bb.byPiece[6];

  // Occupation bonus: pawns standing on centre squares.
  int whiteOccupation = popcount(whitePawns & CENTER_MASK);
  int blackOccupation = popcount(blackPawns & CENTER_MASK);

  // Attack bonus: centre squares attacked by friendly pawns.
  int whiteAttacks = popcount(whitePawnAtk & CENTER_MASK);
  int blackAttacks = popcount(blackPawnAtk & CENTER_MASK);

  int bonus = (whiteOccupation - blackOccupation) * CENTER_OCCUPATION_BONUS
            + (whiteAttacks - blackAttacks)       * CENTER_ATTACK_BONUS;
  mgScore += bonus;
  egScore += bonus;
}

// ---------------------------------------------------------------------------
// Distance helper — Chebyshev distance between two LERF squares.
// Used by evalPassedPawnKingDist (endgame) and evalKingDanger (midgame).
// ---------------------------------------------------------------------------

// Chebyshev distance between two LERF squares.
#ifdef TUNING
int chebyshevDist(Square a, Square b) {
#else
static int chebyshevDist(Square a, Square b) {
#endif
  int dr = (a >> 3) - (b >> 3);  // rank difference
  int df = (a & 7)  - (b & 7);   // file difference
  if (dr < 0) dr = -dr;
  if (df < 0) df = -df;
  return dr > df ? dr : df;
}

// ---------------------------------------------------------------------------
// Passed pawn king distance — endgame bonus/penalty based on both kings'
// proximity to passed pawns.
//
// In endgames, a passed pawn's value depends heavily on whether its own
// king can escort it to promotion and whether the enemy king can intercept.
// Each passer receives:
//   - Bonus for own king proximity:   PASSER_OWN_KING × (7 − distance)
//   - Bonus for enemy king distance:  PASSER_ENEMY_KING × distance
//
// Applied to the EG score only (in the midgame, king safety and PSTs
// already govern king placement).  Not cached in the pawn hash because
// it depends on king positions, which change independently of pawn
// structure.
//
// Reference: https://www.chessprogramming.org/King_Distance#Passed_Pawn
// ---------------------------------------------------------------------------

EVAL_CONST int PASSER_OWN_KING   = 7;
EVAL_CONST int PASSER_ENEMY_KING = 17;

static void evalPassedPawnKingDist(const BitboardSet& bb, int& egScore) {
  Bitboard whitePawns = bb.byPiece[0];
  Bitboard blackPawns = bb.byPiece[6];

  Bitboard wkBB = bb.byPiece[5];
  Bitboard bkBB = bb.byPiece[11];
  if (!wkBB || !bkBB) return;
  Square wkSq = lsb(wkBB);
  Square bkSq = lsb(bkBB);

  // White passed pawns — bonus for white.
  Bitboard wp = whitePawns;
  while (wp) {
    Square sq = popLsb(wp);
    if (!isPassed(sq, Color::WHITE, blackPawns)) continue;
    int ownDist   = chebyshevDist(sq, wkSq);
    int enemyDist = chebyshevDist(sq, bkSq);
    egScore += PASSER_OWN_KING   * (7 - ownDist);
    egScore += PASSER_ENEMY_KING * enemyDist;
  }

  // Black passed pawns — bonus for black (subtract in white-relative score).
  Bitboard bp = blackPawns;
  while (bp) {
    Square sq = popLsb(bp);
    if (!isPassed(sq, Color::BLACK, whitePawns)) continue;
    int ownDist   = chebyshevDist(sq, bkSq);
    int enemyDist = chebyshevDist(sq, wkSq);
    egScore -= PASSER_OWN_KING   * (7 - ownDist);
    egScore -= PASSER_ENEMY_KING * enemyDist;
  }
}

// ---------------------------------------------------------------------------
// Space — bonus for territory behind own pawns in the centre files.
//
// Controlling space behind the pawn chain gives pieces room to manoeuvre.
// For each side, count safe squares on files c–f, ranks 2–4 (for White)
// or ranks 5–7 (for Black) that are not attacked by enemy pawns.
//
// Value: +1cp per safe square. Applied to both MG and EG.
//
// Reference: https://www.chessprogramming.org/Space
// ---------------------------------------------------------------------------

EVAL_CONST int SPACE_BONUS = 0;

// Files c–f, ranks 2–4 (LERF ranks 1–3) for White.
EVAL_FIXED Bitboard WHITE_SPACE_ZONE =
    (FILE_C | FILE_D | FILE_E | FILE_F) & (RANK_2 | RANK_3 | RANK_4);

// Files c–f, ranks 5–7 (LERF ranks 4–6) for Black.
EVAL_FIXED Bitboard BLACK_SPACE_ZONE =
    (FILE_C | FILE_D | FILE_E | FILE_F) & (RANK_5 | RANK_6 | RANK_7);

static void evalSpace(const BitboardSet& bb,
                      Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                      int& mgScore, int& egScore) {

  int whiteSpace = popcount(WHITE_SPACE_ZONE & ~blackPawnAtk);
  int blackSpace = popcount(BLACK_SPACE_ZONE & ~whitePawnAtk);

  int bonus = (whiteSpace - blackSpace) * SPACE_BONUS;
  mgScore += bonus;
  egScore += bonus;
}

// ---------------------------------------------------------------------------
// Knight outposts — bonus for knights on squares that are:
//   (a) protected by a friendly pawn, and
//   (b) not attackable by any enemy pawn (no enemy pawn on adjacent files
//       that could advance to attack the square).
//
// Central outposts (d4/d5/e4/e5) receive double the bonus.
//
// Bonus: +15cp (base), +30cp (central). Both MG and EG.
//
// Reference: https://www.chessprogramming.org/Outposts
// ---------------------------------------------------------------------------

EVAL_CONST int OUTPOST_BONUS = 0;

static void evalKnightOutposts(const BitboardSet& bb,
                               Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                               int& mgScore, int& egScore) {

  Bitboard whitePawns = bb.byPiece[0];
  Bitboard blackPawns = bb.byPiece[6];

  Bitboard whitePawnAttacks = whitePawnAtk;
  Bitboard blackPawnAttacks = blackPawnAtk;

  // --- White knights (index 1) ---
  Bitboard wn = bb.byPiece[1];
  while (wn) {
    Square sq = popLsb(wn);

    // Must be protected by a friendly pawn.
    if (!(squareBB(sq) & whitePawnAttacks)) continue;

    // Must not be attackable by enemy pawns on adjacent files that can
    // advance to reach this square.  Black pawns move downward in LERF
    // (decreasing rank), so only black pawns on ranks ABOVE the knight
    // (rank+1 .. 7) can advance down to attack it.
    int file = colOf(sq);
    Bitboard adjFiles = 0;
    if (file > 0) adjFiles |= fileBB(file - 1);
    if (file < 7) adjFiles |= fileBB(file + 1);

    int rank = sq / 8;
    // Ranks strictly above: clear the lower (rank+1)*8 bits.
    Bitboard aboveMask = ~((static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1);
    if (blackPawns & adjFiles & aboveMask) continue;

    int bonus = OUTPOST_BONUS;
    if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5)
      bonus *= 2;

    mgScore += bonus;
    egScore += bonus;
  }

  // --- Black knights (index 7) ---
  Bitboard bn = bb.byPiece[7];
  while (bn) {
    Square sq = popLsb(bn);

    // Must be protected by a friendly pawn.
    if (!(squareBB(sq) & blackPawnAttacks)) continue;

    // White pawns move upward in LERF (increasing rank), so only white
    // pawns on ranks BELOW the knight (0 .. rank-1) can advance up to
    // attack it.
    int file = colOf(sq);
    Bitboard adjFiles = 0;
    if (file > 0) adjFiles |= fileBB(file - 1);
    if (file < 7) adjFiles |= fileBB(file + 1);

    int rank = sq / 8;
    // Ranks strictly below: keep only the lower rank*8 bits.
    Bitboard belowMask = (rank > 0)
        ? (static_cast<Bitboard>(1) << (8 * rank)) - 1
        : 0;
    if (whitePawns & adjFiles & belowMask) continue;

    int bonus = OUTPOST_BONUS;
    if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5)
      bonus *= 2;

    mgScore -= bonus;
    egScore -= bonus;
  }
}

// ---------------------------------------------------------------------------
// Trapped pieces — penalty for pieces stuck in corners/edges where pawns
// block their retreat.
//
// Detects common trapped piece patterns:
//   • Bishop on a7/b8 (or mirrored h7/g8) with enemy pawn blocking escape
//   • Bishop on a2/b1 (or mirrored h2/g1) with enemy pawn blocking escape
//   • Rook trapped by own uncastled king on the same side
//
// Values: −50cp per trapped bishop, −40cp per trapped rook (MG only, since
// in endgames piece mobility increases and trapping is less permanent).
//
// Reference: https://www.chessprogramming.org/Trapped_Pieces
// ---------------------------------------------------------------------------

EVAL_CONST int TRAPPED_BISHOP_PENALTY = -26;
EVAL_CONST int TRAPPED_ROOK_PENALTY   = 0;

static void evalTrappedPieces(const BitboardSet& bb,
                              int& mgScore, int& /* egScore */) {
  Bitboard whiteBishops = bb.byPiece[2];   // W_BISHOP
  Bitboard blackBishops = bb.byPiece[8];   // B_BISHOP
  Bitboard whitePawns   = bb.byPiece[0];   // W_PAWN
  Bitboard blackPawns   = bb.byPiece[6];   // B_PAWN
  Bitboard whiteRooks   = bb.byPiece[3];   // W_ROOK
  Bitboard blackRooks   = bb.byPiece[9];   // B_ROOK
  Bitboard whiteKing    = bb.byPiece[5];   // W_KING
  Bitboard blackKing    = bb.byPiece[11];  // B_KING

  // LERF squares for the trapped bishop patterns.
  constexpr Square A7 = 48, B6 = 41, B8 = 57;
  constexpr Square H7 = 55, G6 = 46, G8 = 62;
  constexpr Square A2 = 8,  B3 = 17, B1 = 1;
  constexpr Square H2 = 15, G3 = 22, G1 = 6;

  // White bishop trapped on a7 by black pawn on b6.
  if ((whiteBishops & squareBB(A7)) && (blackPawns & squareBB(B6)))
    mgScore += TRAPPED_BISHOP_PENALTY;
  // White bishop trapped on b8 by black pawn on b6 (deeper trap).
  if ((whiteBishops & squareBB(B8)) && (blackPawns & squareBB(B6)))
    mgScore += TRAPPED_BISHOP_PENALTY;
  // White bishop trapped on h7 by black pawn on g6.
  if ((whiteBishops & squareBB(H7)) && (blackPawns & squareBB(G6)))
    mgScore += TRAPPED_BISHOP_PENALTY;
  // White bishop trapped on g8 by black pawn on g6 (deeper trap).
  if ((whiteBishops & squareBB(G8)) && (blackPawns & squareBB(G6)))
    mgScore += TRAPPED_BISHOP_PENALTY;

  // Black bishop trapped on a2 by white pawn on b3.
  if ((blackBishops & squareBB(A2)) && (whitePawns & squareBB(B3)))
    mgScore -= TRAPPED_BISHOP_PENALTY;
  // Black bishop trapped on b1 by white pawn on b3 (deeper trap).
  if ((blackBishops & squareBB(B1)) && (whitePawns & squareBB(B3)))
    mgScore -= TRAPPED_BISHOP_PENALTY;
  // Black bishop trapped on h2 by white pawn on g3.
  if ((blackBishops & squareBB(H2)) && (whitePawns & squareBB(G3)))
    mgScore -= TRAPPED_BISHOP_PENALTY;
  // Black bishop trapped on g1 by white pawn on g3 (deeper trap).
  if ((blackBishops & squareBB(G1)) && (whitePawns & squareBB(G3)))
    mgScore -= TRAPPED_BISHOP_PENALTY;

  // Rook trapped by own uncastled king — king on f1/g1 traps rook on h1,
  // king on b1/c1 traps rook on a1 (and mirrored for black).
  constexpr Square A1 = 0, H1 = 7;
  constexpr Square F1 = 5;
  constexpr Square C1 = 2;
  constexpr Square A8 = 56, H8 = 63;
  constexpr Square F8 = 61;
  constexpr Square C8 = 58;

  // White rook trapped on h1 by king on f1 or g1.
  if ((whiteRooks & squareBB(H1)) &&
      (whiteKing & (squareBB(F1) | squareBB(G1))))
    mgScore += TRAPPED_ROOK_PENALTY;
  // White rook trapped on a1 by king on b1 or c1.
  if ((whiteRooks & squareBB(A1)) &&
      (whiteKing & (squareBB(B1) | squareBB(C1))))
    mgScore += TRAPPED_ROOK_PENALTY;

  // Black rook trapped on h8 by king on f8 or g8.
  if ((blackRooks & squareBB(H8)) &&
      (blackKing & (squareBB(F8) | squareBB(G8))))
    mgScore -= TRAPPED_ROOK_PENALTY;
  // Black rook trapped on a8 by king on b8 or c8.
  if ((blackRooks & squareBB(A8)) &&
      (blackKing & (squareBB(B8) | squareBB(C8))))
    mgScore -= TRAPPED_ROOK_PENALTY;
}

// ---------------------------------------------------------------------------
// Connectivity — bonus for pieces defended by friendly pieces.
//
// A piece that is defended (attacked by at least one friendly piece) is
// more resilient in exchanges.  This is a simple measure of piece
// coordination: count the number of non-pawn friendly pieces that are
// defended by any friendly piece (including pawns).
//
// Pawns are excluded as targets because their structure is already
// evaluated separately.  Kings are excluded because they are never
// captured.
//
// Value: +3cp per defended non-pawn piece (MG+EG).
//
// Reference: https://www.chessprogramming.org/Connectivity
// ---------------------------------------------------------------------------

EVAL_CONST int CONNECTIVITY_BONUS = 25;

static void evalConnectivity(const BitboardSet& bb,
                             const attacks::AttackInfo& info,
                             int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Bitboard friendly = bb.byColor[c];
    Bitboard friendlyAttacks = info.byColor[c];

    // Non-pawn, non-king friendly pieces.
    int pawnIdx  = c * 6;      // W_PAWN=0, B_PAWN=6
    int kingIdx  = c * 6 + 5;  // W_KING=5, B_KING=11
    Bitboard targets = friendly & ~bb.byPiece[pawnIdx] & ~bb.byPiece[kingIdx];

    // Count how many of those pieces are defended by any friendly piece.
    int defended = popcount(targets & friendlyAttacks);

    int bonus = defended * CONNECTIVITY_BONUS;
    mgScore += sign * bonus;
    egScore += sign * bonus;
  }
}

// ---------------------------------------------------------------------------
// Threats — bonus for lower-value pieces attacking higher-value enemy pieces.
//
// Uses the pre-computed AttackInfo to detect piece-level pressure: pawns
// threatening minors/rooks/queens, minors threatening rooks/queens, and
// rooks threatening queens.  Each attacker-victim pair has separate MG/EG
// weights — threats are most valuable in the midgame when piece safety
// dictates strategy.
//
// Reference: https://www.chessprogramming.org/Evaluation#Threats
// ---------------------------------------------------------------------------

EVAL_CONST int THREAT_PAWN_VS_MINOR_MG  =  8;
EVAL_CONST int THREAT_PAWN_VS_MINOR_EG  =  6;
EVAL_CONST int THREAT_PAWN_VS_ROOK_MG   = 12;
EVAL_CONST int THREAT_PAWN_VS_ROOK_EG   =  8;
EVAL_CONST int THREAT_PAWN_VS_QUEEN_MG  = 15;
EVAL_CONST int THREAT_PAWN_VS_QUEEN_EG  = 10;
EVAL_CONST int THREAT_MINOR_VS_ROOK_MG  = 45;
EVAL_CONST int THREAT_MINOR_VS_ROOK_EG  =  0;
EVAL_CONST int THREAT_MINOR_VS_QUEEN_MG = 52;
EVAL_CONST int THREAT_MINOR_VS_QUEEN_EG =  0;
EVAL_CONST int THREAT_ROOK_VS_QUEEN_MG  = 28;
EVAL_CONST int THREAT_ROOK_VS_QUEEN_EG  =  0;

static void evalThreats(const BitboardSet& bb,
                        const attacks::AttackInfo& info,
                        int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    int enemy = 1 - c;

    // Enemy piece bitboards (indexed by Zobrist offset: pawn=0..king=5).
    int eo = enemy * 6;  // enemy offset into byPiece[]
    Bitboard enemyMinors = bb.byPiece[eo + 1] | bb.byPiece[eo + 2];  // N + B
    Bitboard enemyRooks  = bb.byPiece[eo + 3];
    Bitboard enemyQueens = bb.byPiece[eo + 4];

    // Friendly pawn attacks.
    Bitboard pawnAtk = info.byPiece[c][0];
    int mg = 0, eg = 0;

    mg += popcount(pawnAtk & enemyMinors) * THREAT_PAWN_VS_MINOR_MG;
    eg += popcount(pawnAtk & enemyMinors) * THREAT_PAWN_VS_MINOR_EG;
    mg += popcount(pawnAtk & enemyRooks)  * THREAT_PAWN_VS_ROOK_MG;
    eg += popcount(pawnAtk & enemyRooks)  * THREAT_PAWN_VS_ROOK_EG;
    mg += popcount(pawnAtk & enemyQueens) * THREAT_PAWN_VS_QUEEN_MG;
    eg += popcount(pawnAtk & enemyQueens) * THREAT_PAWN_VS_QUEEN_EG;

    // Friendly minor attacks (N + B).
    Bitboard minorAtk = info.byPiece[c][1] | info.byPiece[c][2];
    mg += popcount(minorAtk & enemyRooks)  * THREAT_MINOR_VS_ROOK_MG;
    eg += popcount(minorAtk & enemyRooks)  * THREAT_MINOR_VS_ROOK_EG;
    mg += popcount(minorAtk & enemyQueens) * THREAT_MINOR_VS_QUEEN_MG;
    eg += popcount(minorAtk & enemyQueens) * THREAT_MINOR_VS_QUEEN_EG;

    // Friendly rook attacks.
    Bitboard rookAtk = info.byPiece[c][3];
    mg += popcount(rookAtk & enemyQueens) * THREAT_ROOK_VS_QUEEN_MG;
    eg += popcount(rookAtk & enemyQueens) * THREAT_ROOK_VS_QUEEN_EG;

    mgScore += sign * mg;
    egScore += sign * eg;
  }
}

// ---------------------------------------------------------------------------
// King danger — unified king safety evaluation combining zone attacks and
// piece proximity.
//
// Counts enemy piece types attacking the king ring (king square + 8
// surrounding squares) using the AttackInfo already computed for mobility.
// When at least one piece attacks the zone, nearby enemy pieces (Chebyshev
// distance ≤ 3) add proximity pressure to the attacker weight sum.  The
// combined weight is looked up in a nonlinear danger table, reflecting
// that multiple coordinated attackers create super-linear threat.
//
// Merges the concepts of "king tropism" (piece proximity) and "attacking
// king zone" (piece influence on king ring) into a single coherent signal.
// Proximity only matters when there's a genuine attack — distant pieces
// that happen to be nearby don't contribute.
//
// MG-only — in endgames, king exposure is less relevant and king
// centralization (via EG PSTs) takes over.
//
// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
// Reference: https://www.chessprogramming.org/King_Safety#King_Tropism
// ---------------------------------------------------------------------------

// Per-piece-type zone attack weight: how dangerous each attacker type is.
// Indexed by minor/major piece offset (N=0, B=1, R=2, Q=3).
// Fixed (not tunable) — tuning shifts danger via TABLE entries instead,
// keeping all parameters linear for analytical gradient computation.
EVAL_FIXED int KING_DANGER_WEIGHT[] = {2, 2, 3, 5};

// Nonlinear danger table — maps total attacker weight to MG penalty (cp).
// Index = sum of attacker weights (0..max).  Clamped at the last entry.
// TABLE[0] stays fixed at 0; entries 1..12 are tunable.
EVAL_CONST int KING_DANGER_TABLE[] = {0, 0, 12, 12, 48, 10, 37, 57, 60, 139, 243, 163, 251};

static void evalKingDanger(const BitboardSet& bb,
                           const attacks::AttackInfo& info,
                           int& mgScore) {
  for (int c = 0; c < 2; ++c) {
    // Penalty applies to the defending side: attacks on white king hurt
    // white (mgScore -=), attacks on black king hurt black (mgScore +=).
    int sign = (c == 0) ? -1 : 1;
    int enemy = 1 - c;

    // Locate king.
    int kingIdx = c * 6 + 5;  // W_KING=5, B_KING=11
    Bitboard kingBB = bb.byPiece[kingIdx];
    if (!kingBB) continue;
    Square kingSq = lsb(kingBB);

    // King zone: king square + 8 surrounding squares.
    Bitboard kingZone = attacks::KING[kingSq] | squareBB(kingSq);

    // Sum attacker weights for enemy pieces attacking the king zone.
    int totalWeight = 0;
    for (int pt = 0; pt < 4; ++pt) {
      // PieceType: KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5
      int pieceType = pt + 2;
      if (info.byPiece[enemy][pieceType] & kingZone)
        totalWeight += KING_DANGER_WEIGHT[pt];
    }

    // Proximity bonus: close enemy pieces amplify the zone attack.
    // Only applied when at least one piece attacks the zone — avoids
    // noise from pieces that happen to be nearby but aren't threatening.
    if (totalWeight > 0) {
      for (int pt = 0; pt < 4; ++pt) {
        int pieceIdx = enemy * 6 + pt + 1;  // N..Q index in byPiece
        Bitboard pieces = bb.byPiece[pieceIdx];
        while (pieces) {
          Square sq = popLsb(pieces);
          if (chebyshevDist(sq, kingSq) <= 3)
            ++totalWeight;
        }
      }
    }

    // Look up nonlinear penalty.
    int idx = totalWeight < KING_DANGER_TABLE_SIZE
            ? totalWeight
            : KING_DANGER_TABLE_SIZE - 1;
    mgScore += sign * KING_DANGER_TABLE[idx];
  }
}

// ===========================================================================
// Hash table implementations
// ===========================================================================

// ---------------------------------------------------------------------------
// Power-of-2 rounding helper (shared by both tables).
// ---------------------------------------------------------------------------

static int roundDownPow2(int n) {
  if (n <= 0) return 0;
  int v = 1;
  while (v * 2 <= n) v *= 2;
  return v;
}

// ---------------------------------------------------------------------------
// Pawn Hash Table
// ---------------------------------------------------------------------------

void PawnHashTable::resize(int numEntries) {
  free();
  size = roundDownPow2(numEntries);
  if (size == 0) return;
  mask = size - 1;
  entries = new PawnEntry[size];
  clear();
}

void PawnHashTable::free() {
  delete[] entries;
  entries = nullptr;
  size = 0;
  mask = 0;
}

void PawnHashTable::clear() {
  if (entries) std::memset(entries, 0, size * sizeof(PawnEntry));
}

const PawnEntry* PawnHashTable::probe(uint64_t hash) const {
  if (!entries) return nullptr;
  int idx = static_cast<int>(hash) & mask;
  uint32_t key32 = static_cast<uint32_t>(hash >> 32);
  const PawnEntry& e = entries[idx];
  return (e.key == key32) ? &e : nullptr;
}

void PawnHashTable::store(uint64_t hash, int16_t mg, int16_t eg) {
  if (!entries) return;
  int idx = static_cast<int>(hash) & mask;
  PawnEntry& e = entries[idx];
  e.key     = static_cast<uint32_t>(hash >> 32);
  e.mgScore = mg;
  e.egScore = eg;
}

// ---------------------------------------------------------------------------
// Evaluation Hash Table
// ---------------------------------------------------------------------------

void EvalHashTable::resize(int numEntries) {
  free();
  size = roundDownPow2(numEntries);
  if (size == 0) return;
  mask = size - 1;
  entries = new EvalEntry[size];
  clear();
}

void EvalHashTable::free() {
  delete[] entries;
  entries = nullptr;
  size = 0;
  mask = 0;
}

void EvalHashTable::clear() {
  if (entries) std::memset(entries, 0, size * sizeof(EvalEntry));
}

const EvalEntry* EvalHashTable::probe(uint64_t hash) const {
  if (!entries) return nullptr;
  int idx = static_cast<int>(hash) & mask;
  uint32_t key32 = static_cast<uint32_t>(hash >> 32);
  const EvalEntry& e = entries[idx];
  return (e.key == key32) ? &e : nullptr;
}

void EvalHashTable::store(uint64_t hash, int16_t s) {
  if (!entries) return;
  int idx = static_cast<int>(hash) & mask;
  EvalEntry& e = entries[idx];
  e.key   = static_cast<uint32_t>(hash >> 32);
  e.score = s;
  e.pad   = 0;
}

// ---------------------------------------------------------------------------
// Bad bishop — penalty for bishops blocked by own pawns on same color.
//
// A bishop loses effectiveness when many friendly pawns occupy squares
// of the same color complex, restricting its mobility.
//
// Reference: https://www.chessprogramming.org/Bad_Bishop
// ---------------------------------------------------------------------------

EVAL_CONST int BAD_BISHOP_MG = 0;
EVAL_CONST int BAD_BISHOP_EG = 0;

static void evalBadBishop(const BitboardSet& bb,
                          int& mgScore, int& egScore) {
  // White bishops
  Bitboard wb = bb.byPiece[2];   // W_BISHOP
  Bitboard wp = bb.byPiece[0];   // W_PAWN
  while (wb) {
    Square sq = popLsb(wb);
    Bitboard colorMask = (squareBB(sq) & DARK_SQUARES) ? DARK_SQUARES : LIGHT_SQUARES;
    int blocked = popcount(wp & colorMask);
    mgScore += blocked * BAD_BISHOP_MG;
    egScore += blocked * BAD_BISHOP_EG;
  }

  // Black bishops
  Bitboard bbishops = bb.byPiece[8];   // B_BISHOP
  Bitboard bp = bb.byPiece[6];          // B_PAWN
  while (bbishops) {
    Square sq = popLsb(bbishops);
    Bitboard colorMask = (squareBB(sq) & DARK_SQUARES) ? DARK_SQUARES : LIGHT_SQUARES;
    int blocked = popcount(bp & colorMask);
    mgScore -= blocked * BAD_BISHOP_MG;
    egScore -= blocked * BAD_BISHOP_EG;
  }
}

// ---------------------------------------------------------------------------
// Rook behind passed pawn (Tarrasch Rule) — EG only.
//
// A rook behind a friendly passed pawn supports its advance; behind an
// enemy passed pawn, it restricts the advance.
//
// Reference: https://www.chessprogramming.org/Tarrasch_Rule
// ---------------------------------------------------------------------------

EVAL_CONST int ROOK_BEHIND_OWN_PASSER_EG   =  50;
EVAL_CONST int ROOK_BEHIND_ENEMY_PASSER_EG  = -45;

static void evalRookBehindPasser(const BitboardSet& bb, int& egScore) {
  Bitboard whitePawns = bb.byPiece[0];
  Bitboard blackPawns = bb.byPiece[6];
  Bitboard whiteRooks = bb.byPiece[3];   // W_ROOK
  Bitboard blackRooks = bb.byPiece[9];   // B_ROOK

  // Check white passers with rooks on the same file
  Bitboard wp = whitePawns;
  while (wp) {
    Square sq = popLsb(wp);
    if (!isPassed(sq, Color::WHITE, blackPawns)) continue;
    int file = colOf(sq);
    Bitboard fileMask = FILE_A << file;

    // Own rook behind the passer (lower rank for white)
    Bitboard ownRooksOnFile = whiteRooks & fileMask;
    while (ownRooksOnFile) {
      Square rsq = popLsb(ownRooksOnFile);
      if (rsq < sq) egScore += ROOK_BEHIND_OWN_PASSER_EG;
    }
    // Enemy rook behind our passer (lower rank = behind from black's view)
    Bitboard enemyRooksOnFile = blackRooks & fileMask;
    while (enemyRooksOnFile) {
      Square rsq = popLsb(enemyRooksOnFile);
      if (rsq < sq) egScore += ROOK_BEHIND_ENEMY_PASSER_EG;
    }
  }

  // Check black passers with rooks on the same file
  Bitboard bpawns = blackPawns;
  while (bpawns) {
    Square sq = popLsb(bpawns);
    if (!isPassed(sq, Color::BLACK, whitePawns)) continue;
    int file = colOf(sq);
    Bitboard fileMask = FILE_A << file;

    // Own rook behind the passer (higher rank for black)
    Bitboard ownRooksOnFile = blackRooks & fileMask;
    while (ownRooksOnFile) {
      Square rsq = popLsb(ownRooksOnFile);
      if (rsq > sq) egScore -= ROOK_BEHIND_OWN_PASSER_EG;
    }
    // Enemy rook behind black's passer (higher rank = behind from white's view)
    Bitboard enemyRooksOnFile = whiteRooks & fileMask;
    while (enemyRooksOnFile) {
      Square rsq = popLsb(enemyRooksOnFile);
      if (rsq > sq) egScore -= ROOK_BEHIND_ENEMY_PASSER_EG;
    }
  }
}

// ---------------------------------------------------------------------------
// Main evaluation entry point
// ---------------------------------------------------------------------------

// Internal: shared evaluation body.  `mgScore` and `egScore` are initialized
// by the caller — either from the per-piece material+PST loop or from
// precomputed incremental accumulators.
static int evaluateImpl(const BitboardSet& bb, int mgScore, int egScore,
                        PawnHashTable* pawnHash) {
  // Pre-compute pawn attacks once — shared by evalPawnStructure,
  // evalKnightOutposts, evalCenterControl, and evalSpace.
  Bitboard whitePawnAtk = shiftNE(bb.byPiece[0]) | shiftNW(bb.byPiece[0]);
  Bitboard blackPawnAtk = shiftSE(bb.byPiece[6]) | shiftSW(bb.byPiece[6]);

  evalPawnStructure(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore, pawnHash);
  evalPassedPawnKingDist(bb, egScore);
  evalBishopPair(bb, mgScore, egScore);
  evalBadBishop(bb, mgScore, egScore);
  evalRookFiles(bb, mgScore, egScore);
  evalRookOnSeventh(bb, mgScore, egScore);
  evalRookBehindPasser(bb, egScore);
  evalKnightOutposts(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore);
  evalKingSafety(bb, mgScore, egScore);
  evalCenterControl(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore);
  evalSpace(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore);
  evalTrappedPieces(bb, mgScore, egScore);

  // Mobility, connectivity, threats, and king danger require full attack
  // computation — one call.  Precondition: attacks::init() must have been
  // called before first evaluation (guaranteed by Position constructor).
  attacks::AttackInfo info = attacks::computeAll(bb);
  evalMobility(bb, info, mgScore, egScore);
  evalConnectivity(bb, info, mgScore, egScore);
  evalThreats(bb, info, mgScore, egScore);
  evalKingDanger(bb, info, mgScore);

  int phase = popcount(bb.byPiece[1] | bb.byPiece[7])  * PHASE_KNIGHT
            + popcount(bb.byPiece[2] | bb.byPiece[8])  * PHASE_BISHOP
            + popcount(bb.byPiece[3] | bb.byPiece[9])  * PHASE_ROOK
            + popcount(bb.byPiece[4] | bb.byPiece[10]) * PHASE_QUEEN;

  if (phase > MAX_PHASE) phase = MAX_PHASE;

  int score = (mgScore * phase + egScore * (MAX_PHASE - phase)) / MAX_PHASE;

  // --- Opposite-color bishop scaling ---
  if (phase <= 6) {
    Bitboard wb = bb.byPiece[2];
    Bitboard bbish = bb.byPiece[8];
    if (popcount(wb) == 1 && popcount(bbish) == 1) {
      bool whiteDark  = (wb & DARK_SQUARES) != 0;
      bool blackDark  = (bbish & DARK_SQUARES) != 0;
      if (whiteDark != blackDark) {
        score = score * 3 / 4;
      }
    }
  }

  return score;
}

int evaluatePosition(const BitboardSet& bb,
                     PawnHashTable* pawnHash) {
  initPawnMasks();

  int mgScore = 0;
  int egScore = 0;

  for (int i = 0; i < 6; ++i) {
    Bitboard white = bb.byPiece[i];
    while (white) {
      Square sq = popLsb(white);
      mgScore += MATERIAL[i] + PST_MG[i][sq];
      egScore += MATERIAL[i] + PST_EG[i][sq];
    }

    Bitboard black = bb.byPiece[i + 6];
    while (black) {
      Square sq = popLsb(black);
      mgScore -= MATERIAL[i] + PST_MG[i][sq ^ 56];
      egScore -= MATERIAL[i] + PST_EG[i][sq ^ 56];
    }
  }

  return evaluateImpl(bb, mgScore, egScore, pawnHash);
}

int evaluatePosition(const BitboardSet& bb, int mgMatPST, int egMatPST,
                     PawnHashTable* pawnHash) {
  initPawnMasks();
  return evaluateImpl(bb, mgMatPST, egMatPST, pawnHash);
}

}  // namespace eval
}  // namespace LibreChess
