#include "evaluation.h"
#include "attacks.h"

namespace {

using namespace LibreChess;

// Material values indexed by piece type offset (P=0 N=1 B=2 R=3 Q=4 K=5).
// In centipawns to avoid mixing float/int arithmetic in the inner loop.
constexpr int MATERIAL[] = {100, 300, 300, 500, 900, 0};

// ---------------------------------------------------------------------------
// Piece-square tables — centipawns, LERF order (a1=0, h8=63).
// White's perspective; black mirrors via (sq ^ 56).
// Based on the simplified evaluation function (CPW / Tomasz Michniewski).
//
// Midgame (MG) tables: king should hide behind pawns, minor pieces want
// center control. Endgame (EG) tables: king should be active and central,
// passed pawns and advancement matter more. Non-king/pawn pieces share the
// same table for both phases initially — differentiate later if tuning shows
// a benefit.
// ---------------------------------------------------------------------------

// clang-format off

// --- Midgame PSTs --- (identical to the original single-phase tables)

constexpr int8_t PST_PAWN_MG[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,   // rank 1 (never occupied)
  50, 50, 50, 50, 50, 50, 50, 50,   // rank 2
  10, 10, 20, 30, 30, 20, 10, 10,   // rank 3
   5,  5, 10, 25, 25, 10,  5,  5,   // rank 4
   0,  0,  0, 20, 20,  0,  0,  0,   // rank 5
   5, -5,-10,  0,  0,-10, -5,  5,   // rank 6
   5, 10, 10,-20,-20, 10, 10,  5,   // rank 7
   0,  0,  0,  0,  0,  0,  0,  0    // rank 8 (never occupied)
};

constexpr int8_t PST_KNIGHT_MG[64] = {
 -50,-40,-30,-30,-30,-30,-40,-50,
 -40,-20,  0,  0,  0,  0,-20,-40,
 -30,  0, 10, 15, 15, 10,  0,-30,
 -30,  5, 15, 20, 20, 15,  5,-30,
 -30,  0, 15, 20, 20, 15,  0,-30,
 -30,  5, 10, 15, 15, 10,  5,-30,
 -40,-20,  0,  5,  5,  0,-20,-40,
 -50,-40,-30,-30,-30,-30,-40,-50
};

constexpr int8_t PST_BISHOP_MG[64] = {
 -20,-10,-10,-10,-10,-10,-10,-20,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -10,  0, 10, 10, 10, 10,  0,-10,
 -10,  5,  5, 10, 10,  5,  5,-10,
 -10,  0,  5, 10, 10,  5,  0,-10,
 -10, 10, 10, 10, 10, 10, 10,-10,
 -10,  5,  0,  0,  0,  0,  5,-10,
 -20,-10,-10,-10,-10,-10,-10,-20
};

constexpr int8_t PST_ROOK_MG[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,
   5, 10, 10, 10, 10, 10, 10,  5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
   0,  0,  0,  5,  5,  0,  0,  0
};

constexpr int8_t PST_QUEEN_MG[64] = {
 -20,-10,-10, -5, -5,-10,-10,-20,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -10,  0,  5,  5,  5,  5,  0,-10,
  -5,  0,  5,  5,  5,  5,  0, -5,
   0,  0,  5,  5,  5,  5,  0, -5,
 -10,  5,  5,  5,  5,  5,  0,-10,
 -10,  0,  5,  0,  0,  0,  0,-10,
 -20,-10,-10, -5, -5,-10,-10,-20
};

constexpr int8_t PST_KING_MG[64] = {
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

// Pawn EG: advancement is critical — passed pawns on high ranks are valuable.
constexpr int8_t PST_PAWN_EG[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,   // rank 1 (never occupied)
  70, 70, 70, 70, 70, 70, 70, 70,   // rank 7 — about to promote
  40, 40, 40, 40, 40, 40, 40, 40,   // rank 6
  20, 20, 20, 20, 20, 20, 20, 20,   // rank 5
  10, 10, 10, 10, 10, 10, 10, 10,   // rank 4
   5,  5,  5,  5,  5,  5,  5,  5,   // rank 3
   0,  0,  0,  0,  0,  0,  0,  0,   // rank 2
   0,  0,  0,  0,  0,  0,  0,  0    // rank 8 (never occupied)
};

// King EG: wants to be active and central (opposite of MG).
constexpr int8_t PST_KING_EG[64] = {
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
constexpr const int8_t* PST_MG[6] = {
    PST_PAWN_MG, PST_KNIGHT_MG, PST_BISHOP_MG,
    PST_ROOK_MG, PST_QUEEN_MG,  PST_KING_MG};

// EG tables — non-king/pawn pieces reuse MG tables.
constexpr const int8_t* PST_EG[6] = {
    PST_PAWN_EG, PST_KNIGHT_MG, PST_BISHOP_MG,
    PST_ROOK_MG, PST_QUEEN_MG,  PST_KING_EG};

// ---------------------------------------------------------------------------
// Game phase — non-pawn material determines how much the position resembles
// an opening (phase = MAX_PHASE) vs a pure endgame (phase = 0).
// Weights: N=1, B=1, R=2, Q=4 → max 4*(1+1+2+4) = 24.
// ---------------------------------------------------------------------------
constexpr int PHASE_KNIGHT = 1;
constexpr int PHASE_BISHOP = 1;
constexpr int PHASE_ROOK   = 2;
constexpr int PHASE_QUEEN  = 4;
constexpr int MAX_PHASE     = 24;  // 2*(1+1+2*2+4) = 24

// ---------------------------------------------------------------------------
// Pawn-structure masks (absorbed from chess_pieces.cpp).
// File-local — initialized lazily.
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

// ---------------------------------------------------------------------------
// Pawn structure constants — centipawns.
// Values are initial estimates based on CPW's Simplified Evaluation Function.
// ---------------------------------------------------------------------------

}  // anonymous namespace

// ===========================================================================
// eval — public API + internal helpers
// ===========================================================================

namespace LibreChess {
namespace eval {

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Pawn-structure query functions (public for testing).
// Use the file-local mask arrays initialized by initPawnMasksImpl().
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
// ---------------------------------------------------------------------------

static constexpr int PASSED_PAWN_BASE    =  20;
static constexpr int PASSED_PAWN_BASE_EG =  30;
static constexpr int PASSED_PAWN_RANK    =   5;
static constexpr int CONNECTED_PASSED    =  10;
static constexpr int ISOLATED_PENALTY    = -15;
static constexpr int DOUBLED_PENALTY     = -10;
static constexpr int BACKWARD_PENALTY    = -10;

// ---------------------------------------------------------------------------
// Pawn structure scoring — centipawns, white-relative.
// ---------------------------------------------------------------------------

static void evalPawnStructure(const BitboardSet& bb,
                              int& mgScore, int& egScore) {
  Bitboard whitePawns = bb.byPiece[0];   // pieceZobristIndex(W_PAWN) = 0
  Bitboard blackPawns = bb.byPiece[6];   // pieceZobristIndex(B_PAWN) = 6

  if (!whitePawns && !blackPawns) return;

  Bitboard blackPawnAttacks = shiftSE(blackPawns) | shiftSW(blackPawns);
  Bitboard whitePawnAttacks = shiftNE(whitePawns) | shiftNW(whitePawns);

  int mg = 0, eg = 0;
  uint8_t whitePassedFiles = 0;
  uint8_t blackPassedFiles = 0;

  // --- White pawns ---
  Bitboard wp = whitePawns;
  while (wp) {
    Square sq = popLsb(wp);
    int rank = sq / 8;

    if (isPassed(sq, Color::WHITE, blackPawns)) {
      int rankBonus = rank > 3 ? (rank - 3) * PASSED_PAWN_RANK : 0;
      mg += PASSED_PAWN_BASE + rankBonus;
      eg += PASSED_PAWN_BASE_EG + rankBonus * 2;
      whitePassedFiles |= 1 << (sq & 7);
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
      int rankBonus = rank < 4 ? (4 - rank) * PASSED_PAWN_RANK : 0;
      mg -= PASSED_PAWN_BASE + rankBonus;
      eg -= PASSED_PAWN_BASE_EG + rankBonus * 2;
      blackPassedFiles |= 1 << (sq & 7);
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

static constexpr int BISHOP_PAIR_MG = 30;
static constexpr int BISHOP_PAIR_EG = 50;

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

static constexpr int ROOK_OPEN_FILE      = 20;
static constexpr int ROOK_SEMI_OPEN_FILE = 10;

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

static constexpr int ROOK_7TH_MG = 20;
static constexpr int ROOK_7TH_EG = 30;

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

static constexpr int MOBILITY_KNIGHT = 4;
static constexpr int MOBILITY_BISHOP = 4;
static constexpr int MOBILITY_ROOK   = 2;
static constexpr int MOBILITY_QUEEN  = 1;

static void evalMobility(const BitboardSet& bb,
                         const attacks::AttackInfo& info,
                         int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Bitboard friendly = bb.byColor[c];

    // Knight mobility (index 2 = PieceType::KNIGHT)
    int knightMob = popcount(info.byPiece[c][2] & ~friendly);
    // Bishop mobility (index 3 = PieceType::BISHOP)
    int bishopMob = popcount(info.byPiece[c][3] & ~friendly);
    // Rook mobility (index 4 = PieceType::ROOK)
    int rookMob   = popcount(info.byPiece[c][4] & ~friendly);
    // Queen mobility (index 5 = PieceType::QUEEN)
    int queenMob  = popcount(info.byPiece[c][5] & ~friendly);

    int bonus = knightMob * MOBILITY_KNIGHT
              + bishopMob * MOBILITY_BISHOP
              + rookMob   * MOBILITY_ROOK
              + queenMob  * MOBILITY_QUEEN;

    mgScore += sign * bonus;
    egScore += sign * bonus;
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

static constexpr int SHIELD_MISSING_PAWN  = -15;
static constexpr int SHIELD_ADVANCED_PAWN = -5;
static constexpr int SHIELD_OPEN_FILE     = -20;

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

static constexpr int OUTPOST_BONUS = 15;

static void evalKnightOutposts(const BitboardSet& bb,
                               int& mgScore, int& egScore) {
  // Central squares — d4/d5/e4/e5 (LERF)
  constexpr Square SQ_D4 = 27, SQ_D5 = 35, SQ_E4 = 28, SQ_E5 = 36;

  Bitboard whitePawns = bb.byPiece[0];
  Bitboard blackPawns = bb.byPiece[6];

  // White pawn attacks (protect white knights)
  Bitboard whitePawnAttacks = shiftNE(whitePawns) | shiftNW(whitePawns);
  // Black pawn attacks (protect black knights)
  Bitboard blackPawnAttacks = shiftSE(blackPawns) | shiftSW(blackPawns);

  // --- White knights (index 1) ---
  Bitboard wn = bb.byPiece[1];
  while (wn) {
    Square sq = popLsb(wn);

    // Must be protected by a friendly pawn
    if (!(squareBB(sq) & whitePawnAttacks)) continue;

    // Must not be attackable by enemy pawns on adjacent files ahead.
    // Use pawnPassedMask for the opposite color to check if any enemy pawn
    // can ever reach this square's adjacent files.
    int file = colOf(sq);
    Bitboard enemyCanAttack = 0;
    // Enemy pawns that could advance to attack this square are on adjacent
    // files, below this square's rank (for black pawns advancing upward, they
    // are actually on higher LERF ranks going down — tricky).
    // Simpler: check if there are any black pawns in the pawnPassedMask for
    // this square from black's perspective on adjacent files.
    // Actually the simplest check: no enemy pawn on adjacent files at all
    // (from the knight's rank forward).
    if (file > 0) enemyCanAttack |= fileBB(file - 1);
    if (file < 7) enemyCanAttack |= fileBB(file + 1);

    // Only consider enemy pawns that are "behind" the knight (can advance to
    // attack it). For white knight: black pawns on higher LERF ranks (toward
    // rank 1 in board terms).
    int rank = sq / 8;
    Bitboard forwardMask = 0;
    for (int r = rank; r < 8; ++r) forwardMask |= rankBB(r);
    // Actually for a white knight on sq, enemy BLACK pawns that could attack
    // it are those on ranks below (LERF ranks < sq's rank) since black pawns
    // move downward in LERF. But pawnPassedMask already handles this.
    // Simplify: just check if there's any enemy pawn on adjacent files from
    // this rank upward (in LERF terms). Black pawns move down (decreasing rank),
    // so a black pawn on rank > sq's rank cannot attack sq.
    // Pawns below sq in LERF could advance to attack.
    Bitboard belowMask = 0;
    for (int r = 0; r <= rank; ++r) belowMask |= rankBB(r);
    if (blackPawns & enemyCanAttack & belowMask) continue;

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

    // Must be protected by a friendly pawn
    if (!(squareBB(sq) & blackPawnAttacks)) continue;

    // No enemy (white) pawns on adjacent files that can advance to attack
    int file = colOf(sq);
    Bitboard enemyCanAttack = 0;
    if (file > 0) enemyCanAttack |= fileBB(file - 1);
    if (file < 7) enemyCanAttack |= fileBB(file + 1);

    int rank = sq / 8;
    // White pawns move up (increasing LERF rank), so a white pawn on rank <
    // sq's rank could advance to attack. Pawns above sq cannot.
    Bitboard aboveMask = 0;
    for (int r = rank; r < 8; ++r) aboveMask |= rankBB(r);
    if (whitePawns & enemyCanAttack & aboveMask) continue;

    int bonus = OUTPOST_BONUS;
    // Mirror central squares for black: d5/d4/e5/e4 are the same squares
    if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5)
      bonus *= 2;

    mgScore -= bonus;
    egScore -= bonus;
  }
}

// ---------------------------------------------------------------------------
// Main evaluation entry point
// ---------------------------------------------------------------------------

int evaluatePosition(const BitboardSet& bb) {
  // Lazy-init pawn structure masks on first call.
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

  evalPawnStructure(bb, mgScore, egScore);
  evalBishopPair(bb, mgScore, egScore);
  evalRookFiles(bb, mgScore, egScore);
  evalRookOnSeventh(bb, mgScore, egScore);
  evalKnightOutposts(bb, mgScore, egScore);
  evalKingSafety(bb, mgScore, egScore);

  // Mobility requires full attack computation — one call per evaluation.
  attacks::init();
  attacks::AttackInfo info = attacks::computeAll(bb);
  evalMobility(bb, info, mgScore, egScore);

  int phase = popcount(bb.byPiece[1] | bb.byPiece[7])  * PHASE_KNIGHT
            + popcount(bb.byPiece[2] | bb.byPiece[8])  * PHASE_BISHOP
            + popcount(bb.byPiece[3] | bb.byPiece[9])  * PHASE_ROOK
            + popcount(bb.byPiece[4] | bb.byPiece[10]) * PHASE_QUEEN;

  if (phase > MAX_PHASE) phase = MAX_PHASE;

  return (mgScore * phase + egScore * (MAX_PHASE - phase)) / MAX_PHASE;
}

}  // namespace eval
}  // namespace LibreChess
