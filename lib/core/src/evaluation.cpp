#include "evaluation.h"
#include "eval_params.h"
#include "attacks.h"
#include "piece.h"
#include "utils.h"
#include "zobrist.h"

#include <cstring>       // memset
#include <new>           // nothrow

namespace {

using namespace LibreChess;
using piece::pieceIndex;

// ---------------------------------------------------------------------------
// Pawn-structure masks — constexpr, placed in .rodata (Flash on ESP32).
//
// Passed and forward masks are stored for White only; Black masks are
// derived at query time via vertical mirror (byteSwap64 + sq^56).  This
// halves the footprint (−1,024 bytes).  The derivation cost is
// negligible because pawn structure is cached in the pawn hash table
// (~95% hit rate).
//
// The isolated-pawn mask (adjacent files) is derived inline from
// adjacentFilesMask(), eliminating a dedicated 64-byte table.
// ---------------------------------------------------------------------------

struct PawnMasks {
  Bitboard passed[64] = {};
  Bitboard forward[64] = {};
  constexpr PawnMasks() {
    for (Square sq = 0; sq < 64; ++sq) {
      const int rank = rankOf(sq);
      const int file = fileOf(sq);
      Bitboard sameAndAdjacentFiles = fileBB(file);
      if (file > 0) sameAndAdjacentFiles |= fileBB(file - 1);
      if (file < 7) sameAndAdjacentFiles |= fileBB(file + 1);
      Bitboard wp = 0, wf = 0;
      for (int r = rank + 1; r < 8; ++r) {
        Bitboard rm = rankBB(r);
        wp |= sameAndAdjacentFiles & rm;
        wf |= fileBB(file) & rm;
      }
      passed[sq] = wp;
      forward[sq] = wf;
    }
  }
};

static constexpr PawnMasks PAWN_MASKS{};

// ---------------------------------------------------------------------------
// Color-loop helpers — constexpr lookup tables for bilateral evaluation.
//
// SIDE_SIGN: maps color index (0=WHITE, 1=BLACK) to the sign applied to
// white-relative scores (+1 for white, −1 for black).
// COLORS: maps color index to the Color enum, eliminating verbose
// static_casts in every loop iteration.
//
// Reference: "Lookup Tables over Branching" (project principle).
// ---------------------------------------------------------------------------

static constexpr int SIDE_SIGN[] = {1, -1};
static constexpr Color COLORS[] = {Color::WHITE, Color::BLACK};


Bitboard adjacentFilesMask(int file) {
  Bitboard mask = 0;
  if (file > 0) mask |= fileBB(file - 1);
  if (file < 7) mask |= fileBB(file + 1);
  return mask;
}

// Passed-pawn mask for either color.  White: direct lookup.
// Black: mirror the mask for the vertically reflected square.
inline Bitboard passedMask(Color c, Square sq) {
  if (c == Color::WHITE) return PAWN_MASKS.passed[sq];
  return byteSwap64(PAWN_MASKS.passed[sq ^ 56]);
}

// Forward-file mask for either color (same file, ranks ahead).
inline Bitboard forwardMask(Color c, Square sq) {
  if (c == Color::WHITE) return PAWN_MASKS.forward[sq];
  return byteSwap64(PAWN_MASKS.forward[sq ^ 56]);
}

}  // anonymous namespace

// ===========================================================================
// eval — evaluation constants, helpers, and public API
// ===========================================================================

namespace LibreChess {
namespace eval {

// ---------------------------------------------------------------------------
// Flat PSQT lookup tables — pre-combined material + PST + color sign.
//
// White pieces (indices 0–5): PSQT[idx][sq] = +MATERIAL[idx] + PST[idx][sq]
// Black pieces (indices 6–11): PSQT[idx][sq] = -(MATERIAL[idx-6] + PST[idx-6][sq ^ 56])
//
// Eliminates 3 conditional branches and pointer indirection per
// pieceSquareMG/EG call.
//
// Production builds: constexpr tables placed in .rodata (Flash on ESP32),
// freeing 3 KiB BSS (RAM).  pieceSquareMGEG() reads directly.
// TUNING builds: no cached tables — pieceSquareMGEG() computes from
// mutable eval params on each call, avoiding cache invalidation.
//
// Reference: https://www.chessprogramming.org/Piece-Square_Tables
// ---------------------------------------------------------------------------

#ifdef TUNING

// Direct-computation lookup tables — map piece type (0–5) to raw PST array.
// Used by pieceSquareMGEG() under TUNING to compute from the mutable params
// without a cached PSQT layer.  Eliminates buildPSQT/invalidatePSQT.
static int* const PST_MG_PTRS[6] = {
    PST_PAWN_MG, PST_KNIGHT_MG, PST_BISHOP_MG,
    PST_ROOK_MG, PST_QUEEN_MG,  PST_KING_MG};

static int* const PST_EG_PTRS[6] = {
    PST_PAWN_EG, PST_KNIGHT_EG, PST_BISHOP_EG,
    PST_ROOK_EG, PST_QUEEN_EG,  PST_KING_EG};

#else  // Production: constexpr PSQT in .rodata

// Macro-based aggregate initialization — each element is a simple constant
// expression (MATERIAL[t] ± PST_XX[sq]), avoiding constexpr function
// evaluation that hits GCC 5.x step limits with 12×64 loops.
#define PSQT_E_(m,p,s)  static_cast<int16_t>((m) + (p)[(s)])
#define PSQT_NE_(m,p,s) static_cast<int16_t>(-((m) + (p)[((s) ^ 56)]))
#define PSQT_R8_(M,p,b) \
  PSQT_E_(M,p,b+0),PSQT_E_(M,p,b+1),PSQT_E_(M,p,b+2),PSQT_E_(M,p,b+3), \
  PSQT_E_(M,p,b+4),PSQT_E_(M,p,b+5),PSQT_E_(M,p,b+6),PSQT_E_(M,p,b+7)
#define PSQT_R64_(M,p) \
  PSQT_R8_(M,p,0), PSQT_R8_(M,p,8), PSQT_R8_(M,p,16),PSQT_R8_(M,p,24), \
  PSQT_R8_(M,p,32),PSQT_R8_(M,p,40),PSQT_R8_(M,p,48),PSQT_R8_(M,p,56)
#define PSQT_N8_(M,p,b) \
  PSQT_NE_(M,p,b+0),PSQT_NE_(M,p,b+1),PSQT_NE_(M,p,b+2),PSQT_NE_(M,p,b+3), \
  PSQT_NE_(M,p,b+4),PSQT_NE_(M,p,b+5),PSQT_NE_(M,p,b+6),PSQT_NE_(M,p,b+7)
#define PSQT_N64_(M,p) \
  PSQT_N8_(M,p,0), PSQT_N8_(M,p,8), PSQT_N8_(M,p,16),PSQT_N8_(M,p,24), \
  PSQT_N8_(M,p,32),PSQT_N8_(M,p,40),PSQT_N8_(M,p,48),PSQT_N8_(M,p,56)

// clang-format off
static constexpr int16_t PSQT_MG[12][64] = {
  {PSQT_R64_(MATERIAL[0], PST_PAWN_MG)},   {PSQT_R64_(MATERIAL[1], PST_KNIGHT_MG)},
  {PSQT_R64_(MATERIAL[2], PST_BISHOP_MG)},  {PSQT_R64_(MATERIAL[3], PST_ROOK_MG)},
  {PSQT_R64_(MATERIAL[4], PST_QUEEN_MG)},   {PSQT_R64_(MATERIAL[5], PST_KING_MG)},
  {PSQT_N64_(MATERIAL[0], PST_PAWN_MG)},   {PSQT_N64_(MATERIAL[1], PST_KNIGHT_MG)},
  {PSQT_N64_(MATERIAL[2], PST_BISHOP_MG)},  {PSQT_N64_(MATERIAL[3], PST_ROOK_MG)},
  {PSQT_N64_(MATERIAL[4], PST_QUEEN_MG)},   {PSQT_N64_(MATERIAL[5], PST_KING_MG)},
};

static constexpr int16_t PSQT_EG[12][64] = {
  {PSQT_R64_(MATERIAL_EG[0], PST_PAWN_EG)},   {PSQT_R64_(MATERIAL_EG[1], PST_KNIGHT_EG)},
  {PSQT_R64_(MATERIAL_EG[2], PST_BISHOP_EG)},  {PSQT_R64_(MATERIAL_EG[3], PST_ROOK_EG)},
  {PSQT_R64_(MATERIAL_EG[4], PST_QUEEN_EG)},   {PSQT_R64_(MATERIAL_EG[5], PST_KING_EG)},
  {PSQT_N64_(MATERIAL_EG[0], PST_PAWN_EG)},   {PSQT_N64_(MATERIAL_EG[1], PST_KNIGHT_EG)},
  {PSQT_N64_(MATERIAL_EG[2], PST_BISHOP_EG)},  {PSQT_N64_(MATERIAL_EG[3], PST_ROOK_EG)},
  {PSQT_N64_(MATERIAL_EG[4], PST_QUEEN_EG)},   {PSQT_N64_(MATERIAL_EG[5], PST_KING_EG)},
};
// clang-format on

// Clean up internal macros (not needed outside this section).
#undef PSQT_E_
#undef PSQT_NE_
#undef PSQT_R8_
#undef PSQT_R64_
#undef PSQT_N8_
#undef PSQT_N64_

#endif  // TUNING

// ---------------------------------------------------------------------------
// Pawn-structure query functions (public for testing).
// Use the constexpr pawn mask arrays.
// ---------------------------------------------------------------------------

int materialValue(PieceType pt) {
  // Direct index into MATERIAL[]: subtract 1 from PieceType, guard with
  // unsigned comparison (NONE wraps to UINT_MAX, KING maps to MATERIAL[5]=0).
  auto idx = static_cast<unsigned>(pt) - 1;
  return (idx < 6) ? MATERIAL[idx] : 0;
}

// ---------------------------------------------------------------------------
// Incremental material+PST helpers
//
// Precondition: pieceIdx must be in [0, 11] (a valid pieceIndex).
// Callers obtaining pieceIdx from pieceIndex(Piece) must ensure the
// piece is not NONE before calling — PIECE_IDX_NONE (-1) would cause
// OOB access into MATERIAL[] and PST arrays.
// ---------------------------------------------------------------------------

PSQTPair pieceSquareMGEG(int pieceIdx, Square sq) {
#ifdef TUNING
  // Direct computation from raw mutable arrays — no cached PSQT.
  // Under TUNING, eval params change at runtime; recomputing per-call
  // avoids cache invalidation and keeps evaluation.cpp tuning-logic-free.
  bool isBlack = pieceIdx >= 6;
  int type = isBlack ? pieceIdx - 6 : pieceIdx;
  Square lookupSq = isBlack ? (sq ^ 56) : sq;
  int mg = MATERIAL[type]    + PST_MG_PTRS[type][lookupSq];
  int eg = MATERIAL_EG[type] + PST_EG_PTRS[type][lookupSq];
  return isBlack ? PSQTPair{-mg, -eg} : PSQTPair{mg, eg};
#else
  return {PSQT_MG[pieceIdx][sq], PSQT_EG[pieceIdx][sq]};
#endif
}

PSQTPair computeMaterialPST(const BitboardSet& bb) {
  int mg = 0, eg = 0;
  for (int i = 0; i < 12; ++i) {
    Bitboard pieces = bb.byPiece[i];
    while (pieces) {
      PSQTPair p = pieceSquareMGEG(i, popLsb(pieces));
      mg += p.mg;
      eg += p.eg;
    }
  }
  return {mg, eg};
}

int computeMaterial(const BitboardSet& bb) {
  int score = 0;
  for (int i = 0; i < 6; ++i) {
    int val = MATERIAL[i];
    score += popcount(bb.byPiece[i]) * val;
    score -= popcount(bb.byPiece[i + 6]) * val;
  }
  return score;
}

// ---------------------------------------------------------------------------

bool isPassed(Square sq, Color color, Bitboard enemyPawns) {
  return (enemyPawns & passedMask(color, sq)) == 0;
}

bool isIsolated(Square sq, Bitboard friendlyPawns) {
  return (friendlyPawns & adjacentFilesMask(fileOf(sq))) == 0;
}

bool isDoubled(Square sq, Color color, Bitboard friendlyPawns) {
  return (friendlyPawns & forwardMask(color, sq)) != 0;
}

bool isBackward(Square sq, Color color, Bitboard friendlyPawns, Bitboard enemyPawnAttacks) {
  const int file = fileOf(sq);
  Bitboard adjacentPawns = friendlyPawns & adjacentFilesMask(file);
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
// Pawn structure scoring — centipawns, white-relative.
// Pawn structure constants live in eval_params.h.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Pawn Hash Table — probe and store helpers.
//
// The pawn hash table caches pawn structure MG/EG scores keyed by a
// pawn-only Zobrist hash.  Since pawn structure changes infrequently
// (only on pawn moves/captures), hit rates exceed 95% in typical searches.
//
// Reference: https://www.chessprogramming.org/Pawn_Hash_Table
// ---------------------------------------------------------------------------

// Probe the pawn hash table.  On hit, adds cached scores to mgScore/egScore,
// fills passedPawns, and returns true.  On miss (or if pawnHash is null),
// sets pHash for later store and returns false.
static bool probePawnHash(const BitboardSet& bb, PawnHashTable* pawnHash,
                          uint64_t& pHash, int& mgScore, int& egScore,
                          Bitboard passedPawns[2]) {
  if (!pawnHash) return false;
  pHash = zobrist::computePawnHash(bb);
  const PawnEntry* cached = pawnHash->probe(pHash);
  if (cached) {
    mgScore += cached->mgScore;
    egScore += cached->egScore;
    passedPawns[0] = cached->passedPawns[0];
    passedPawns[1] = cached->passedPawns[1];
    return true;
  }
  return false;
}

// Store pawn structure scores and passed pawn bitboards in the pawn hash table.
static void storePawnHash(PawnHashTable* pawnHash, uint64_t pHash,
                          int mg, int eg,
                          const Bitboard passedPawns[2]) {
  if (pawnHash)
    pawnHash->store(pHash, static_cast<int16_t>(mg), static_cast<int16_t>(eg),
                    passedPawns[0], passedPawns[1]);
}

static void evalPawnStructure(const BitboardSet& bb,
                              Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                              int& mgScore, int& egScore,
                              Bitboard passedPawns[2],
                              PawnHashTable* pawnHash) {
  Bitboard whitePawns = bb.byPiece[pieceIndex('P')];
  Bitboard blackPawns = bb.byPiece[pieceIndex('p')];

  passedPawns[0] = passedPawns[1] = 0;

  if (!whitePawns && !blackPawns) return;

  // --- Pawn hash probe ---
  // Reference: https://www.chessprogramming.org/Pawn_Hash_Table
  uint64_t pHash = 0;
  if (probePawnHash(bb, pawnHash, pHash, mgScore, egScore, passedPawns))
    return;

  Bitboard pawns[2]      = {whitePawns, blackPawns};
  Bitboard pawnAtks[2]   = {whitePawnAtk, blackPawnAtk};

  int mg = 0, eg = 0;

  for (int c = 0; c < 2; ++c) {
    int sign       = SIDE_SIGN[c];
    Color color    = COLORS[c];
    Bitboard friendly   = pawns[c];
    Bitboard enemy      = pawns[1 - c];
    Bitboard friendAtk  = pawnAtks[c];
    Bitboard enemyAtk   = pawnAtks[1 - c];

    Bitboard p = friendly;
    while (p) {
      Square sq = popLsb(p);
      int rank = rankOf(sq);
      // Black rank mirrors: LERF rank 0 is rank 1 for white, rank 8 for black
      int passedRank = (c == 0) ? rank : (7 - rank);

      if (isPassed(sq, color, enemy)) {
        mg += sign * PASSED_RANK_BONUS_MG[passedRank];
        eg += sign * PASSED_RANK_BONUS_EG[passedRank];
        passedPawns[c] |= squareBB(sq);
        if (squareBB(sq) & friendAtk)
          mg += sign * PROTECTED_PASSER_MG;
      }
      if (isIsolated(sq, friendly)) {
        mg += sign * ISOLATED_PENALTY_MG;
        eg += sign * ISOLATED_PENALTY_EG;
      }
      if (isDoubled(sq, color, friendly)) {
        mg += sign * DOUBLED_PENALTY_MG;
        eg += sign * DOUBLED_PENALTY_EG;
      }
      if (isBackward(sq, color, friendly, enemyAtk)) {
        mg += sign * BACKWARD_PENALTY_MG;
        eg += sign * BACKWARD_PENALTY_EG;
      }
    }
  }

  // Connected passed pawns — bonus per adjacent-file pair of passed pawns.
  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    // Derive file bitmask from the passed pawn bitboard.
    uint8_t pf = 0;
    Bitboard tmp = passedPawns[c];
    while (tmp) pf |= 1 << fileOf(popLsb(tmp));
    for (int f = 0; f < 7; ++f) {
      if ((pf >> f & 1) && (pf >> (f + 1) & 1)) {
        mg += sign * CONNECTED_PASSED_MG;
        eg += sign * CONNECTED_PASSED_EG;
      }
    }
  }

  mgScore += mg;
  egScore += eg;

  // --- Pawn hash store ---
  // Reference: https://www.chessprogramming.org/Pawn_Hash_Table
  storePawnHash(pawnHash, pHash, mg, eg, passedPawns);
}

// ---------------------------------------------------------------------------
// Bishop pair — bonus when a side has both bishops.
// Reference: https://www.chessprogramming.org/Bishop_Pair
// ---------------------------------------------------------------------------

static void evalBishopPair(const BitboardSet& bb,
                           int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    Color color = COLORS[c];
    if (popcount(bb.byPiece[pieceIndex(color, PieceType::BISHOP)]) >= 2) {
      int sign = SIDE_SIGN[c];
      mgScore += sign * BISHOP_PAIR_MG;
      egScore += sign * BISHOP_PAIR_EG;
    }
  }
}

// ---------------------------------------------------------------------------
// Rook on open/semi-open file — bonus for rooks not blocked by own pawns.
// Reference: https://www.chessprogramming.org/Rook_on_Open_File
// ---------------------------------------------------------------------------

static void evalRookFiles(const BitboardSet& bb,
                          int& mgScore, int& egScore) {
  Bitboard whitePawns = bb.byPiece[pieceIndex('P')];
  Bitboard blackPawns = bb.byPiece[pieceIndex('p')];
  Bitboard allPawns   = whitePawns | blackPawns;
  Bitboard pawns[2]   = {whitePawns, blackPawns};

  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    Color color = COLORS[c];
    Bitboard rooks        = bb.byPiece[pieceIndex(color, PieceType::ROOK)];
    Bitboard friendlyPawns = pawns[c];
    while (rooks) {
      Square sq = popLsb(rooks);
      Bitboard file = fileBB(fileOf(sq));
      if (!(file & allPawns)) {
        mgScore += sign * ROOK_OPEN_FILE_MG;
        egScore += sign * ROOK_OPEN_FILE_EG;
      } else if (!(file & friendlyPawns)) {
        mgScore += sign * ROOK_SEMI_OPEN_FILE_MG;
        egScore += sign * ROOK_SEMI_OPEN_FILE_EG;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Rook on 7th rank — bonus when a rook is on the opponent's second rank
// and the enemy king is on the back rank.
// Reference: https://www.chessprogramming.org/Rook_on_Seventh
// ---------------------------------------------------------------------------

static void evalRookOnSeventh(const BitboardSet& bb,
                              int& mgScore, int& egScore) {
  // Per-color: the 7th rank for the attacker, the 8th for the enemy king.
  // White: rooks on rank 7 (LERF 6), enemy king on rank 8 (LERF 7).
  // Black: rooks on rank 2 (LERF 1), enemy king on rank 1 (LERF 0).
  static constexpr int SEVENTH_RANK[2] = {6, 1};
  static constexpr int EIGHTH_RANK[2]  = {7, 0};

  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    Color color = COLORS[c];
    Color enemy = COLORS[1 - c];
    Bitboard rooksOn7 = bb.byPiece[pieceIndex(color, PieceType::ROOK)] & rankBB(SEVENTH_RANK[c]);
    if (!rooksOn7) continue;
    Bitboard enemyKingBack = bb.byPiece[pieceIndex(enemy, PieceType::KING)] & rankBB(EIGHTH_RANK[c]);
    Bitboard enemyPawns7   = bb.byPiece[pieceIndex(enemy, PieceType::PAWN)] & rankBB(SEVENTH_RANK[c]);
    if (enemyKingBack || enemyPawns7) {
      int count = popcount(rooksOn7);
      mgScore += sign * ROOK_7TH_MG * count;
      egScore += sign * ROOK_7TH_EG * count;
    }
  }
}

// ---------------------------------------------------------------------------
// Mobility — nonlinear per-piece lookup table, safe squares only.
//
// Safe mobility counts attacked squares excluding friendly pieces and
// enemy pawn attacks.  Each piece type has a separate MG/EG table indexed
// by attack count, reflecting diminishing returns from additional mobility.
//
// Reference: https://www.chessprogramming.org/Mobility
// ---------------------------------------------------------------------------

static void evalMobility(const BitboardSet& bb,
                         const attacks::AttackInfo& info,
                         int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    Bitboard friendly    = bb.byColor[c];
    Bitboard enemyPawnAtk = info.byPiece[1 - c][raw(PieceType::PAWN)];
    Bitboard safeMask    = ~friendly & ~enemyPawnAtk;

    int nMob = popcount(info.byPiece[c][raw(PieceType::KNIGHT)] & safeMask);
    int bMob = popcount(info.byPiece[c][raw(PieceType::BISHOP)] & safeMask);
    int rMob = popcount(info.byPiece[c][raw(PieceType::ROOK)]   & safeMask);
    int qMob = popcount(info.byPiece[c][raw(PieceType::QUEEN)]  & safeMask);

    mgScore += sign * (MOBILITY_KNIGHT_MG[nMob] + MOBILITY_BISHOP_MG[bMob]
                     + MOBILITY_ROOK_MG[rMob]   + MOBILITY_QUEEN_MG[qMob]);
    egScore += sign * (MOBILITY_KNIGHT_EG[nMob] + MOBILITY_BISHOP_EG[bMob]
                     + MOBILITY_ROOK_EG[rMob]   + MOBILITY_QUEEN_EG[qMob]);
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

// ---------------------------------------------------------------------------
// Shield file selection — determines the three files relevant for pawn shield
// evaluation based on the king's file.
//
// Queenside castled (king on files a-c): shield files are a, b, c.
// Kingside castled (king on files f-h):  shield files are f, g, h.
// Center king (files d-e): no shield evaluation (returns false).
//
// Reference: https://www.chessprogramming.org/King_Safety#Pawn_Shield
// ---------------------------------------------------------------------------

static bool selectShieldFiles(int kingFile, int shieldFiles[3]) {
  if (kingFile >= 3 && kingFile <= 4) return false;  // center — no shield
  if (kingFile <= 2) {
    shieldFiles[0] = 0; shieldFiles[1] = 1; shieldFiles[2] = 2;
  } else {
    shieldFiles[0] = 5; shieldFiles[1] = 6; shieldFiles[2] = 7;
  }
  return true;
}

// Evaluate pawn shield for one side. Returns MG bonus (positive = good).
static int evalShieldOneSide(const BitboardSet& bb, Color color) {
  Square kingSq = 0;

  // Find king square
  Bitboard king = bb.byPiece[pieceIndex(color, PieceType::KING)];
  if (!king) return 0;
  kingSq = lsb(king);

  int kingFile = fileOf(kingSq);

  // Select shield files based on king position; skip if king is in the center.
  int shieldFiles[3];
  if (!selectShieldFiles(kingFile, shieldFiles)) return 0;

  // Friendly pawns bitboard
  Bitboard friendlyPawns = bb.byPiece[pieceIndex(color, PieceType::PAWN)];
  Bitboard allPawns = bb.byPiece[pieceIndex('P')]
                    | bb.byPiece[pieceIndex('p')];

  int score = 0;

  for (int i = 0; i < 3; ++i) {
    int f = shieldFiles[i];
    Bitboard fileMask = fileBB(f);
    Bitboard shieldPawns = friendlyPawns & fileMask;

    if (!shieldPawns) {
      // Missing pawn in the shield
      score += SHIELD_MISSING_PAWN;
    } else {
      // Check if the pawn is advanced — rank-indexed penalty.
      // Pawns one square forward (rank 3 for white / rank 6 for black) get a
      // mild penalty; pawns two or more squares forward get a heavier one.
      // Normalize rank to white-relative (0=home rank) for both colors.
      // Reference: https://www.chessprogramming.org/King_Safety
      Bitboard copy = shieldPawns;
      while (copy) {
        Square sq = popLsb(copy);
        int rank = rankOf(sq);
        int relRank = (color == Color::WHITE) ? rank : (7 - rank);
        if (relRank == 2)       score += SHIELD_ADV_RANK3;
        else if (relRank >= 3)  score += SHIELD_ADV_RANK4PLUS;
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
// Distance helper — Chebyshev distance between two LERF squares.
// Used by evalPassedPawnKingDist (endgame) and evalKingDanger (midgame).
// ---------------------------------------------------------------------------

// Chebyshev distance between two LERF squares.
// Non-static: also called by the tuner's trace extraction (trace.cpp).
int chebyshevDist(Square a, Square b) {
  int dr = rankOf(a) - rankOf(b);
  int df = fileOf(a) - fileOf(b);
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

static void evalPassedPawnKingDist(const BitboardSet& bb,
                                  const Bitboard passedPawns[2],
                                  int& egScore) {
  Bitboard wkBB = bb.byPiece[pieceIndex('K')];
  Bitboard bkBB = bb.byPiece[pieceIndex('k')];
  if (!wkBB || !bkBB) return;
  Square kingSq[2] = {lsb(wkBB), lsb(bkBB)};

  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    Bitboard p = passedPawns[c];
    while (p) {
      Square sq = popLsb(p);
      int ownDist   = chebyshevDist(sq, kingSq[c]);
      int enemyDist = chebyshevDist(sq, kingSq[1 - c]);
      egScore += sign * (PASSER_OWN_KING * (7 - ownDist)
                       + PASSER_ENEMY_KING * enemyDist);
    }
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

static void evalSpace(const BitboardSet& /* bb */,
                      Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                      int& mgScore, int& /* egScore */) {

  int whiteSpace = popcount(WHITE_SPACE_ZONE & ~blackPawnAtk);
  int blackSpace = popcount(BLACK_SPACE_ZONE & ~whitePawnAtk);

  int diff = whiteSpace - blackSpace;
  mgScore += diff * SPACE_BONUS_MG;
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

// ---------------------------------------------------------------------------
// Outpost detection — tests whether a knight on `sq` occupies an outpost.
//
// An outpost is a square that:
//   1. Is protected by a friendly pawn.
//   2. Cannot be attacked by enemy pawns (no enemy pawns on adjacent files
//      that could advance to reach the square).
//
// Reference: https://www.chessprogramming.org/Outposts
// ---------------------------------------------------------------------------

static bool isOutpostSquare(Square sq, int colorIdx,
                            Bitboard friendlyPawnAtk, Bitboard enemyPawns) {
  // Must be protected by a friendly pawn.
  if (!(squareBB(sq) & friendlyPawnAtk)) return false;

  // Must not be attackable by enemy pawns on adjacent files.
  Bitboard adjFiles = adjacentFilesMask(fileOf(sq));

  int rank = rankOf(sq);
  // White: enemy (black) pawns above (rank+1..7) advance down to attack.
  // Black: enemy (white) pawns below (0..rank-1) advance up to attack.
  Bitboard dangerMask = (colorIdx == 0)
      ? (rank < 7) ? ~((static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1)
                   : static_cast<Bitboard>(0)
      : (rank > 0) ? (static_cast<Bitboard>(1) << (8 * rank)) - 1
                   : static_cast<Bitboard>(0);
  return !(enemyPawns & adjFiles & dangerMask);
}

static void evalKnightOutposts(const BitboardSet& bb,
                               Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                               int& mgScore, int& egScore) {

  // Central square constants (LERF) — used by outpost evaluation.
  constexpr Square SQ_D4 = 27, SQ_D5 = 35, SQ_E4 = 28, SQ_E5 = 36;

  Bitboard pawnAtks[2] = {whitePawnAtk, blackPawnAtk};

  // Unified loop — evaluate outposts for both colors with parameterized
  // pawn directions.  Enemy pawns that can advance to attack the knight
  // come from ranks "above" (for white) or "below" (for black) in LERF.
  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    Color color            = COLORS[c];
    Color enemy            = COLORS[1 - c];
    Bitboard knights       = bb.byPiece[pieceIndex(color, PieceType::KNIGHT)];
    Bitboard friendlyPAtk  = pawnAtks[c];
    Bitboard enemyPawns    = bb.byPiece[pieceIndex(enemy, PieceType::PAWN)];

    while (knights) {
      Square sq = popLsb(knights);

      if (!isOutpostSquare(sq, c, friendlyPAtk, enemyPawns)) continue;

      int bonusMg = OUTPOST_BONUS_MG;
      int bonusEg = OUTPOST_BONUS_EG;
      if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5) {
        bonusMg *= 2;
        bonusEg *= 2;
      }

      mgScore += sign * bonusMg;
      egScore += sign * bonusEg;
    }
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

static void evalTrappedPieces(const BitboardSet& bb,
                              int& mgScore, int& /* egScore */) {
  // Trapped bishop lookup table — each entry maps a bishop square to the
  // blocking pawn square.  Indexed by color: own bishops blocked by enemy pawns.
  // Reference: https://www.chessprogramming.org/Trapped_Pieces
  struct BishopTrap { Square bishop; Square blocker; };
  static constexpr BishopTrap BISHOP_TRAPS[2][4] = {
    {{48, 41}, {57, 41}, {55, 46}, {62, 46}},  // White: a7/b8 × b6, h7/g8 × g6
    {{ 8, 17}, { 1, 17}, {15, 22}, { 6, 22}},  // Black: a2/b1 × b3, h2/g1 × g3
  };

  // Trapped rook — rook hemmed in by own uncastled king.
  // Each entry maps a rook square to the king squares that trap it.
  struct RookTrap { Square rook; Bitboard kingMask; };
  static constexpr RookTrap ROOK_TRAPS[2][2] = {
    {{7, squareBB(5) | squareBB(6)},      // h1 by f1/g1
     {0, squareBB(1) | squareBB(2)}},      // a1 by b1/c1
    {{63, squareBB(61) | squareBB(62)},    // h8 by f8/g8
     {56, squareBB(57) | squareBB(58)}},    // a8 by b8/c8
  };

  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    Color color = COLORS[c];
    Color enemy = COLORS[1 - c];

    Bitboard bishops    = bb.byPiece[pieceIndex(color, PieceType::BISHOP)];
    Bitboard enemyPawns = bb.byPiece[pieceIndex(enemy, PieceType::PAWN)];
    for (const auto& t : BISHOP_TRAPS[c]) {
      if ((bishops & squareBB(t.bishop)) && (enemyPawns & squareBB(t.blocker)))
        mgScore += sign * TRAPPED_BISHOP_PENALTY;
    }

    Bitboard rooks = bb.byPiece[pieceIndex(color, PieceType::ROOK)];
    Bitboard king  = bb.byPiece[pieceIndex(color, PieceType::KING)];
    for (const auto& t : ROOK_TRAPS[c]) {
      if ((rooks & squareBB(t.rook)) && (king & t.kingMask))
        mgScore += sign * TRAPPED_ROOK_PENALTY;
    }
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

// ---------------------------------------------------------------------------
// King danger weight computation — sums per-piece-type zone attack weights
// and adds proximity bonuses for enemy pieces near the king.
//
// Each minor/major piece type has a fixed danger weight (KING_DANGER_WEIGHT).
// If any piece attacks the king zone, nearby pieces (Chebyshev distance ≤ 3)
// add incremental weight regardless of whether they directly attack the zone.
//
// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
// ---------------------------------------------------------------------------

static int computeKingDangerWeight(const BitboardSet& bb,
                                   const attacks::AttackInfo& info,
                                   int enemy, Bitboard kingZone,
                                   Square kingSq) {
  // Sum attacker weights for enemy pieces attacking the king zone.
  int totalWeight = 0;
  Color enemyColor = COLORS[enemy];
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
      int pieceIdx = pieceIndex(enemyColor, static_cast<PieceType>(pt + 2));  // KNIGHT..QUEEN
      Bitboard pieces = bb.byPiece[pieceIdx];
      while (pieces) {
        Square sq = popLsb(pieces);
        if (chebyshevDist(sq, kingSq) <= 3)
          ++totalWeight;
      }
    }
  }

  return totalWeight;
}

static void evalKingDanger(const BitboardSet& bb,
                           const attacks::AttackInfo& info,
                           int& mgScore) {
  for (int c = 0; c < 2; ++c) {
    // Penalty to the defending side: attacks on white king decrease
    // mgScore, attacks on black king increase it.
    int sign = SIDE_SIGN[c];
    int enemy = 1 - c;
    Color color = COLORS[c];

    // Locate king.
    int kingIdx = pieceIndex(color, PieceType::KING);
    Bitboard kingBB = bb.byPiece[kingIdx];
    if (!kingBB) continue;
    Square kingSq = lsb(kingBB);

    // King zone: king square + 8 surrounding squares.
    Bitboard kingZone = attacks::KING[kingSq] | squareBB(kingSq);

    int totalWeight = computeKingDangerWeight(bb, info, enemy, kingZone, kingSq);

    // Look up nonlinear penalty — subtracted because danger hurts the
    // defending side (sign is positive for white, negative for black).
    int idx = totalWeight < KING_DANGER_TABLE_SIZE
            ? totalWeight
            : KING_DANGER_TABLE_SIZE - 1;
    mgScore -= sign * KING_DANGER_TABLE[idx];
  }
}

// ===========================================================================
// Hash table implementations
// ===========================================================================

// ---------------------------------------------------------------------------
// Pawn Hash Table — probe / store implementations.
// resize / free / clear are inherited from HashTableBase.
// ---------------------------------------------------------------------------

const PawnEntry* PawnHashTable::probe(uint64_t hash) const {
  if (!entries) return nullptr;
  int idx = static_cast<int>(hash) & mask;
  uint32_t key32 = static_cast<uint32_t>(hash >> 32);
  const PawnEntry& e = entries[idx];
  return (e.key == key32) ? &e : nullptr;
}

void PawnHashTable::store(uint64_t hash, int16_t mg, int16_t eg,
                          Bitboard passedWhite, Bitboard passedBlack) {
  if (!entries) return;
  int idx = static_cast<int>(hash) & mask;
  PawnEntry& e = entries[idx];
  e.key     = static_cast<uint32_t>(hash >> 32);
  e.mgScore = mg;
  e.egScore = eg;
  e.passedPawns[0] = passedWhite;
  e.passedPawns[1] = passedBlack;
}

// ---------------------------------------------------------------------------
// Evaluation Hash Table — probe / store implementations.
// resize / free / clear are inherited from HashTableBase.
// ---------------------------------------------------------------------------

const EvalEntry* EvalHashTable::probe(uint64_t hash) const {
  if (!entries) return nullptr;
  int idx = static_cast<int>(hash) & mask;
  uint16_t key = static_cast<uint16_t>(hash >> 32);
  const EvalEntry& e = entries[idx];
  return (e.key == key) ? &e : nullptr;
}

void EvalHashTable::store(uint64_t hash, int16_t s) {
  if (!entries) return;
  int idx = static_cast<int>(hash) & mask;
  EvalEntry& e = entries[idx];
  e.key   = static_cast<uint16_t>(hash >> 32);
  e.score = s;
}

// ---------------------------------------------------------------------------
// Bad bishop — penalty for bishops blocked by own pawns on same color.
//
// A bishop loses effectiveness when many friendly pawns occupy squares
// of the same color complex, restricting its mobility.
//
// Reference: https://www.chessprogramming.org/Bad_Bishop
// ---------------------------------------------------------------------------

static void evalBadBishop(const BitboardSet& bb,
                          int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    Color color = COLORS[c];
    Bitboard bishops = bb.byPiece[pieceIndex(color, PieceType::BISHOP)];
    Bitboard pawns   = bb.byPiece[pieceIndex(color, PieceType::PAWN)];
    while (bishops) {
      Square sq = popLsb(bishops);
      Bitboard colorMask = (squareBB(sq) & DARK_SQUARES) ? DARK_SQUARES : LIGHT_SQUARES;
      int blocked = popcount(pawns & colorMask);
      mgScore += sign * blocked * BAD_BISHOP_MG;
      egScore += sign * blocked * BAD_BISHOP_EG;
    }
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

static void evalRookBehindPasser(const BitboardSet& bb,
                                const Bitboard passedPawns[2],
                                int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    Color color         = COLORS[c];
    Color enemy         = COLORS[1 - c];
    Bitboard ownRooks   = bb.byPiece[pieceIndex(color, PieceType::ROOK)];
    Bitboard enemyRooks = bb.byPiece[pieceIndex(enemy, PieceType::ROOK)];

    Bitboard p = passedPawns[c];
    while (p) {
      Square sq = popLsb(p);
      Bitboard fileMask = FILE_A << fileOf(sq);

      // "Behind" the passer: lower rank for white (rsq < sq),
      // higher rank for black (rsq > sq).
      Bitboard ownOnFile = ownRooks & fileMask;
      while (ownOnFile) {
        Square rsq = popLsb(ownOnFile);
        if ((c == 0) ? (rsq < sq) : (rsq > sq))
          egScore += sign * ROOK_BEHIND_OWN_PASSER_EG;
      }
      Bitboard enemyOnFile = enemyRooks & fileMask;
      while (enemyOnFile) {
        Square rsq = popLsb(enemyOnFile);
        if ((c == 0) ? (rsq < sq) : (rsq > sq))
          egScore += sign * ROOK_BEHIND_ENEMY_PASSER_EG;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Main evaluation entry point
// ---------------------------------------------------------------------------

// Internal: shared evaluation body.  `mgScore` and `egScore` are initialized
// by the caller — either from the per-piece material+PST loop or from
// precomputed incremental accumulators.
// ---------------------------------------------------------------------------
// Game phase — determines the interpolation weight between midgame and
// endgame scores in tapered evaluation.
//
// Phase value: sum of non-pawn material weights on the board.
//   Knight/Bishop = 1, Rook = 2, Queen = 4.  Maximum = 24 (all pieces).
// A high phase emphasizes MG scores; a low phase emphasizes EG scores.
//
// Reference: https://www.chessprogramming.org/Tapered_Eval
// ---------------------------------------------------------------------------

int computeGamePhase(const BitboardSet& bb) {
  int phase = popcount(bb.byPiece[pieceIndex('N')]
                     | bb.byPiece[pieceIndex('n')]) * PHASE_KNIGHT
            + popcount(bb.byPiece[pieceIndex('B')]
                     | bb.byPiece[pieceIndex('b')]) * PHASE_BISHOP
            + popcount(bb.byPiece[pieceIndex('R')]
                     | bb.byPiece[pieceIndex('r')])   * PHASE_ROOK
            + popcount(bb.byPiece[pieceIndex('Q')]
                     | bb.byPiece[pieceIndex('q')]) * PHASE_QUEEN;
  return (phase > MAX_PHASE) ? MAX_PHASE : phase;
}

// ---------------------------------------------------------------------------
// Opposite-color bishop scaling — reduces the score when each side has
// exactly one bishop and the bishops operate on different color complexes.
//
// In such endgames, the side with the advantage has difficulty converting
// because pawns on the wrong color complex are hard to promote.  Scaling
// by 0.75 reflects this drawing tendency.
//
// Applied only in endgame positions (phase ≤ 6).
//
// Reference: https://www.chessprogramming.org/Bishops_of_Opposite_Colors
// ---------------------------------------------------------------------------

// Opposite-color bishop scaling constants (numerator/denominator form, 3/4).
static constexpr int OCB_SCALE_NUM         = 3;
static constexpr int OCB_SCALE_DENOM       = 4;
static constexpr int OCB_PHASE_THRESHOLD   = 6;

static int applyOCBScaling(int score, const BitboardSet& bb, int phase) {
  if (phase <= OCB_PHASE_THRESHOLD) {
    Bitboard wb = bb.byPiece[pieceIndex('B')];
    Bitboard bbish = bb.byPiece[pieceIndex('b')];
    if (popcount(wb) == 1 && popcount(bbish) == 1) {
      bool whiteDark = (wb & DARK_SQUARES) != 0;
      bool blackDark = (bbish & DARK_SQUARES) != 0;
      if (whiteDark != blackDark)
        score = score * OCB_SCALE_NUM / OCB_SCALE_DENOM;
    }
  }
  return score;
}

static int evaluateImpl(const BitboardSet& bb, int mgScore, int egScore,
                        PawnHashTable* pawnHash, int precomputedPhase = -1) {
  // Full attack computation first — shared by pawn structure, mobility,
  // king danger, knight outposts, and space evaluation.
  // Attack tables are constexpr — no initialization required.
  attacks::AttackInfo info = attacks::computeAll(bb);

  // Extract pawn attacks from AttackInfo (already computed via bulk shift
  // inside computeAll) — avoids redundant manual shift-OR computation.
  Bitboard whitePawnAtk = info.byPiece[raw(Color::WHITE)][raw(PieceType::PAWN)];
  Bitboard blackPawnAtk = info.byPiece[raw(Color::BLACK)][raw(PieceType::PAWN)];

  Bitboard passedPawns[2] = {0, 0};
  evalPawnStructure(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore,
                    passedPawns, pawnHash);

  evalPassedPawnKingDist(bb, passedPawns, egScore);
  evalBishopPair(bb, mgScore, egScore);
  evalBadBishop(bb, mgScore, egScore);
  evalRookFiles(bb, mgScore, egScore);
  evalRookOnSeventh(bb, mgScore, egScore);
  evalRookBehindPasser(bb, passedPawns, egScore);
  evalKnightOutposts(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore);
  evalKingSafety(bb, mgScore, egScore);
  evalSpace(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore);
  evalTrappedPieces(bb, mgScore, egScore);

  evalMobility(bb, info, mgScore, egScore);
  evalKingDanger(bb, info, mgScore);

  int phase = (precomputedPhase >= 0) ? precomputedPhase
                                     : computeGamePhase(bb);
  int score = (mgScore * phase + egScore * (MAX_PHASE - phase)) / MAX_PHASE;

  score = applyOCBScaling(score, bb, phase);

  return score;
}

int evaluatePosition(const BitboardSet& bb,
                     PawnHashTable* pawnHash) {
  PSQTPair p = computeMaterialPST(bb);
  return evaluateImpl(bb, p.mg, p.eg, pawnHash);
}

int evaluatePosition(const BitboardSet& bb, int mgMatPST, int egMatPST,
                     int phase, PawnHashTable* pawnHash) {
  return evaluateImpl(bb, mgMatPST, egMatPST, pawnHash, phase);
}

}  // namespace eval
}  // namespace LibreChess
