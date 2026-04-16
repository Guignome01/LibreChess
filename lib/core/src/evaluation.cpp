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

int tempoBonus() { return TEMPO_BONUS; }

int kingDangerScore(int weight) {
  if (weight < 0) weight = 0;
  if (weight >= KING_SAFETY_TABLE_SIZE) weight = KING_SAFETY_TABLE_SIZE - 1;
  return KING_SAFETY_TABLE[weight];
}

bool isPassed(Square sq, Color color, Bitboard enemyPawns) {
  return (enemyPawns & passedMask(color, sq)) == 0;
}

bool isIsolated(Square sq, Bitboard friendlyPawns) {
  return (friendlyPawns & adjacentFilesMask(fileOf(sq))) == 0;
}

bool isDoubled(Square sq, Color color, Bitboard friendlyPawns) {
  return (friendlyPawns & forwardMask(color, sq)) != 0;
}

// ---------------------------------------------------------------------------
// Backward pawn detection.
//
// A pawn is backward when:
//   1. It has adjacent-file friendly pawns (not isolated).
//   2. ALL adjacent-file friendly pawns are strictly ahead (none at or
//      behind the current rank to provide support).
//   3. The stop square (one rank ahead) is controlled by an enemy pawn.
//
// Isolated pawns are not backward — they have their own penalty.
//
// Reference: https://www.chessprogramming.org/Backward_Pawn
// ---------------------------------------------------------------------------

bool isBackward(Square sq, Color color, Bitboard friendlyPawns,
                Bitboard enemyPawns) {
  int file = fileOf(sq);
  Bitboard adjFiles = adjacentFilesMask(file);
  Bitboard adjFriendly = friendlyPawns & adjFiles;

  // Not backward if isolated (no adjacent-file friendly pawns at all).
  if (!adjFriendly) return false;

  // Not backward if any adjacent-file friendly pawn is at or behind.
  int rank = rankOf(sq);
  Bitboard behindOrLevel;
  if (color == Color::WHITE) {
    behindOrLevel = (static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1;
  } else {
    behindOrLevel = ~((static_cast<Bitboard>(1) << (8 * rank)) - 1);
  }
  if (adjFriendly & behindOrLevel) return false;

  // Stop square must be controlled by an enemy pawn.
  Square stopSq = (color == Color::WHITE) ? sq + 8 : sq - 8;
  Bitboard enemyPawnAtk = (color == Color::WHITE)
      ? (shiftSE(enemyPawns) | shiftSW(enemyPawns))
      : (shiftNE(enemyPawns) | shiftNW(enemyPawns));

  return (squareBB(stopSq) & enemyPawnAtk) != 0;
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

  // Precompute pawn attack bitboards for connected/protected/backward.
  Bitboard pawnAtks[2] = {
    shiftNE(whitePawns) | shiftNW(whitePawns),
    shiftSE(blackPawns) | shiftSW(blackPawns)
  };

  int mg = 0, eg = 0;

  for (int c = 0; c < 2; ++c) {
    int sign       = SIDE_SIGN[c];
    Color color    = COLORS[c];
    Bitboard friendly   = pawns[c];
    Bitboard enemy      = pawns[1 - c];

    Bitboard p = friendly;
    while (p) {
      Square sq = popLsb(p);
      int rank = rankOf(sq);
      int file = fileOf(sq);
      // Black rank mirrors: LERF rank 0 is rank 1 for white, rank 8 for black
      int passedRank = (c == 0) ? rank : (7 - rank);

      bool doubled = isDoubled(sq, color, friendly);

      // --- Passed pawn ---
      // Only the frontmost pawn on a file is scored as passed (doubled fix).
      bool passed = isPassed(sq, color, enemy);
      if (passed && !doubled) {
        mg += sign * PASSED_RANK_BONUS_MG[passedRank];
        eg += sign * PASSED_RANK_BONUS_EG[passedRank];
        passedPawns[c] |= squareBB(sq);

        // Protected passed pawn — defended by own pawn.
        // Reference: https://www.chessprogramming.org/Protected_Passed_Pawn
        if (squareBB(sq) & pawnAtks[c]) {
          mg += sign * PROTECTED_PASSER_MG;
          eg += sign * PROTECTED_PASSER_EG;
        }
      }

      // --- Candidate passed pawn ---
      // Not passed, not doubled, no enemy on forward file, helpers ≥ sentries.
      // Reference: https://www.chessprogramming.org/Candidate_Passed_Pawn
      if (!passed && !doubled) {
        Bitboard fwdFile = forwardMask(color, sq);
        if (!(enemy & fwdFile)) {
          Bitboard adjFiles = adjacentFilesMask(file);
          Bitboard aheadMask, behindOrLevel;
          if (c == 0) {
            aheadMask = (rank < 7)
                ? ~((static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1)
                : static_cast<Bitboard>(0);
            behindOrLevel = (static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1;
          } else {
            aheadMask = (rank > 0)
                ? (static_cast<Bitboard>(1) << (8 * rank)) - 1
                : static_cast<Bitboard>(0);
            behindOrLevel = ~((static_cast<Bitboard>(1) << (8 * rank)) - 1);
          }
          int sentries = popcount(enemy & adjFiles & aheadMask);
          int helpers = popcount(friendly & adjFiles & behindOrLevel);
          if (helpers >= sentries) {
            mg += sign * CANDIDATE_PASSED_MG[passedRank];
            eg += sign * CANDIDATE_PASSED_EG[passedRank];
          }
        }
      }

      // --- Isolated pawn ---
      if (isIsolated(sq, friendly)) {
        mg += sign * ISOLATED_PENALTY_MG;
        eg += sign * ISOLATED_PENALTY_EG;
      }

      // --- Doubled pawn ---
      if (doubled) {
        eg += sign * DOUBLED_PENALTY_EG;
      }

      // --- Backward pawn ---
      // Reference: https://www.chessprogramming.org/Backward_Pawn
      if (isBackward(sq, color, friendly, enemy)) {
        mg += sign * BACKWARD_PENALTY_MG;
        eg += sign * BACKWARD_PENALTY_EG;
      }

      // --- Connected pawn (chain or phalanx) ---
      // Chain: defended by a friendly pawn.  Phalanx: same rank, adjacent file.
      // Rank-indexed bonus — advanced connected pawns are worth more.
      // Reference: https://www.chessprogramming.org/Connected_Pawns
      {
        bool supported = (squareBB(sq) & pawnAtks[c]) != 0;
        Bitboard adjSameRank = adjacentFilesMask(file) & rankBB(rank);
        bool phalanx = (friendly & adjSameRank) != 0;
        if (supported || phalanx) {
          mg += sign * CONNECTED_BONUS_MG[passedRank];
          eg += sign * CONNECTED_BONUS_EG[passedRank];
        }
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
// Passed pawn king proximity — Chebyshev distance, EG only.
//
// In endgames, king distance to passed pawns is critical:
//   - Own king close to passer → supports promotion.
//   - Enemy king far from passer → cannot stop promotion.
//
// Not cached in pawn hash (depends on king positions).
//
// Reference: https://www.chessprogramming.org/King_Pawn_Tropism
// ---------------------------------------------------------------------------

static void evalPassedPawnKingDist(const BitboardSet& bb,
                                   const Bitboard passedPawns[2],
                                   int& egScore) {
  Bitboard allPieces = bb.byColor[0] | bb.byColor[1];

  // Per-side check: does the enemy have any non-pawn, non-king material?
  // Unstoppable passer and pawn race only apply in pure pawn endgames —
  // any enemy piece (knight, bishop, rook, queen) can intercept a passer
  // regardless of the king's distance to the promotion square.
  Bitboard enemyPieces[2];
  for (int c = 0; c < 2; ++c) {
    enemyPieces[c] = bb.byColor[1 - c]
        & ~bb.byPiece[pieceIndex(COLORS[1 - c], PieceType::PAWN)]
        & ~bb.byPiece[pieceIndex(COLORS[1 - c], PieceType::KING)];
  }

  // Track the fastest unstoppable passer per side for pawn race detection.
  int bestDist[2]     = {99, 99};
  Square bestPromo[2] = {0, 0};

  for (int c = 0; c < 2; ++c) {
    Bitboard passed = passedPawns[c];
    if (!passed) continue;

    int sign = SIDE_SIGN[c];
    Square ownKingSq   = lsb(bb.byPiece[pieceIndex(COLORS[c], PieceType::KING)]);
    Square enemyKingSq = lsb(bb.byPiece[pieceIndex(COLORS[1 - c], PieceType::KING)]);

    while (passed) {
      Square sq = popLsb(passed);
      int ownDist   = chebyshevDistance(sq, ownKingSq);
      int enemyDist = chebyshevDistance(sq, enemyKingSq);
      egScore += sign * (PASSER_OWN_KING_DIST_EG * ownDist
                       + PASSER_ENEMY_KING_DIST_EG * enemyDist);

      // Rule of the Square — bonus when enemy king cannot catch the pawn.
      // Only valid in pure pawn endgames: any enemy piece can intercept,
      // making the geometric king-distance check meaningless.
      // Pawn distance is capped at 5 to account for the initial double push.
      // Only applied when the frontspan (path to promotion) is clear of all
      // pieces — a blocked pawn is not unstoppable regardless of king distance.
      // Conservative: no STM adjustment (eval has no side-to-move context).
      // Reference: https://www.chessprogramming.org/Rule_of_the_Square
      // Reference: https://www.chessprogramming.org/Unstoppable_Passer
      if (enemyPieces[c] == 0) {
        int promoRank = (c == 0) ? 7 : 0;
        Square promoSq = makeSquare(promoRank, fileOf(sq));
        int pawnDist = (c == 0) ? (7 - rankOf(sq)) : rankOf(sq);
        if (pawnDist > 5) pawnDist = 5;  // double-push from home rank
        bool clearPath = (forwardMask(COLORS[c], sq) & allPieces) == 0;
        int kingToPromo = chebyshevDistance(enemyKingSq, promoSq);
        if (clearPath && kingToPromo > pawnDist) {
          egScore += sign * UNSTOPPABLE_PASSER_EG;
          if (pawnDist < bestDist[c]) {
            bestDist[c] = pawnDist;
            bestPromo[c] = promoSq;
          }
        }
      }
    }
  }

  // Pawn race — when both sides have unstoppable passers, bonus for the side
  // that promotes first.  If the promoted queen gives check, the promoting
  // side gains a critical tempo (opponent must address the check before
  // promoting their own pawn).
  // Both sides must be in a pure pawn endgame for the race to be meaningful.
  // Reference: https://www.chessprogramming.org/Pawn_Race
  if (bestDist[0] <= 7 && bestDist[1] <= 7) {
    int diff = bestDist[1] - bestDist[0];  // positive = white faster
    if (diff != 0) {
      int fasterColor = (diff > 0) ? 0 : 1;
      int raceSgn = SIDE_SIGN[fasterColor];
      egScore += raceSgn * PAWN_RACE_ADVANTAGE_EG;

      // Queen check on promotion — queen attacks = rook + bishop from the
      // promotion square.  Uses current occupancy (approximate).
      Square promoSq = bestPromo[fasterColor];
      Square enemyKingSq = lsb(bb.byPiece[pieceIndex(
          COLORS[1 - fasterColor], PieceType::KING)]);
      Bitboard queenAtk = attacks::queen(promoSq, allPieces);
      if (queenAtk & (static_cast<Bitboard>(1) << enemyKingSq))
        egScore += raceSgn * PAWN_RACE_CHECK_EG;
    }
  }
}

// ---------------------------------------------------------------------------
// Outside passed pawn — bonus when a passer is on a file beyond the range
// of all opponent pawns.  Such passers force the enemy king to the flank,
// leaving the friendly king free to dominate the center and attack the
// remaining pawn mass.  EG only — the concept is most relevant in endgames.
//
// Detection: a passed pawn on file F is "outside" if F < min(enemy files)
// or F > max(enemy files), with at least 2-file separation to avoid
// trivial cases.
//
// Reference: https://www.chessprogramming.org/Outside_Passed_Pawn
// ---------------------------------------------------------------------------

static void evalOutsidePasser(const BitboardSet& bb,
                              const Bitboard passedPawns[2],
                              int& egScore) {
  Bitboard pawns[2] = {
    bb.byPiece[pieceIndex('P')],
    bb.byPiece[pieceIndex('p')]
  };

  for (int c = 0; c < 2; ++c) {
    Bitboard passed = passedPawns[c];
    if (!passed) continue;

    Bitboard enemy = pawns[1 - c];
    if (!enemy) continue;  // no enemy pawns → no "outside" concept

    // Compute file range of enemy pawns.
    int minFile = 7, maxFile = 0;
    Bitboard ep = enemy;
    while (ep) {
      int f = fileOf(popLsb(ep));
      if (f < minFile) minFile = f;
      if (f > maxFile) maxFile = f;
    }

    int sign = SIDE_SIGN[c];
    Bitboard pp = passed;
    while (pp) {
      Square sq = popLsb(pp);
      int f = fileOf(sq);
      if ((f < minFile && minFile - f >= 2) ||
          (f > maxFile && f - maxFile >= 2)) {
        egScore += sign * OUTSIDE_PASSER_EG;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// King-pawn tropism — penalise when a king is far from all pawns, EG only.
//
// In endgames, the king must be near the action: supporting own passers,
// defending own weaknesses, and attacking enemy pawns.  Computed as the
// average Chebyshev distance from each king to ALL pawns (own + enemy).
// The king closer to the overall pawn mass gets a bonus.
//
// This complements passed-pawn king distance (which only covers passers)
// and the king EG PST (which only rewards centralisation). It handles
// positions where all pawns are on one wing and the king is stranded on
// the opposite side.
//
// Reference: https://www.chessprogramming.org/King_Pawn_Tropism
// ---------------------------------------------------------------------------

static void evalKingPawnTropism(const BitboardSet& bb, int& egScore) {
  Bitboard allPawns = bb.byPiece[pieceIndex('P')]
                    | bb.byPiece[pieceIndex('p')];
  if (!allPawns) return;

  int pawnCount = popcount(allPawns);

  int totalDist[2] = {0, 0};
  for (int c = 0; c < 2; ++c) {
    Square kingSq = lsb(bb.byPiece[pieceIndex(COLORS[c], PieceType::KING)]);
    Bitboard p = allPawns;
    while (p) {
      Square sq = popLsb(p);
      totalDist[c] += chebyshevDistance(kingSq, sq);
    }
  }

  // White-relative: positive = white king closer to pawns (good for white).
  // Divide by pawnCount for average, multiply by weight.
  // To avoid integer division truncation, multiply first.
  int diff = totalDist[1] - totalDist[0];  // positive = white closer
  egScore += KING_PAWN_TROPISM_EG * diff / pawnCount;
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
// Threats — bonus for attacking poorly defended enemy pieces.
//
// Uses precomputed AttackInfo from computeAll().  Four threat categories:
//   - ThreatByPawn: pawn attacking enemy minor/rook/queen
//   - ThreatByMinor: minor piece (N/B) attacking enemy rook/queen
//   - ThreatByRook: rook attacking enemy queen
//   - Hanging: attacked piece with no defender
//
// Reference: https://www.chessprogramming.org/Threat_Move
// ---------------------------------------------------------------------------

ThreatCounts computeThreats(const BitboardSet& bb,
                            const attacks::AttackInfo& info, int c) {
  int enemy = 1 - c;
  Color enemyColor = COLORS[enemy];

  Bitboard enemyMinors = bb.byPiece[pieceIndex(enemyColor, PieceType::KNIGHT)]
                       | bb.byPiece[pieceIndex(enemyColor, PieceType::BISHOP)];
  Bitboard enemyRooks  = bb.byPiece[pieceIndex(enemyColor, PieceType::ROOK)];
  Bitboard enemyQueens = bb.byPiece[pieceIndex(enemyColor, PieceType::QUEEN)];

  Bitboard pawnAtk  = info.byPiece[c][raw(PieceType::PAWN)];
  Bitboard minorAtk = info.byPiece[c][raw(PieceType::KNIGHT)]
                    | info.byPiece[c][raw(PieceType::BISHOP)];
  Bitboard rookAtk  = info.byPiece[c][raw(PieceType::ROOK)];

  Bitboard enemyPieces = enemyMinors | enemyRooks | enemyQueens;
  Bitboard attacked = info.byColor[c] & enemyPieces;
  Bitboard defended = info.byColor[enemy];

  return {
    popcount(pawnAtk & (enemyMinors | enemyRooks | enemyQueens)),
    popcount(minorAtk & (enemyRooks | enemyQueens)),
    popcount(rookAtk & enemyQueens),
    popcount(attacked & ~defended),
  };
}

static void evalThreats(const BitboardSet& bb,
                        const attacks::AttackInfo& info,
                        int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    auto f = computeThreats(bb, info, c);
    mgScore += sign * (THREAT_BY_PAWN_MG * f.byPawn
                     + THREAT_BY_MINOR_MG * f.byMinor
                     + THREAT_BY_ROOK_MG * f.byRook
                     + THREAT_HANGING_MG * f.hanging);
    egScore += sign * (THREAT_BY_PAWN_EG * f.byPawn
                     + THREAT_BY_MINOR_EG * f.byMinor
                     + THREAT_BY_ROOK_EG * f.byRook
                     + THREAT_HANGING_EG * f.hanging);
  }
}

// ---------------------------------------------------------------------------
// Mobility — nonlinear lookup tables, safe squares only.
//
// Safe mobility counts attacked squares excluding friendly pieces and
// enemy pawn attacks.  Each piece type uses a per-count lookup table
// that can express diminishing returns and piece-specific mobility curves.
//
// Reference: https://www.chessprogramming.org/Mobility
// ---------------------------------------------------------------------------

MobilityCounts computeMobility(const BitboardSet& bb,
                               const attacks::AttackInfo& info, int c) {
  Bitboard friendly    = bb.byColor[c];
  Bitboard enemyPawnAtk = info.byPiece[1 - c][raw(PieceType::PAWN)];
  Bitboard safeMask    = ~friendly & ~enemyPawnAtk;

  int nMob = popcount(info.byPiece[c][raw(PieceType::KNIGHT)] & safeMask);
  int bMob = popcount(info.byPiece[c][raw(PieceType::BISHOP)] & safeMask);
  int rMob = popcount(info.byPiece[c][raw(PieceType::ROOK)]   & safeMask);
  int qMob = popcount(info.byPiece[c][raw(PieceType::QUEEN)]  & safeMask);

  if (nMob >= MOB_KNIGHT_SIZE) nMob = MOB_KNIGHT_SIZE - 1;
  if (bMob >= MOB_BISHOP_SIZE) bMob = MOB_BISHOP_SIZE - 1;
  if (rMob >= MOB_ROOK_SIZE)   rMob = MOB_ROOK_SIZE - 1;
  if (qMob >= MOB_QUEEN_SIZE)  qMob = MOB_QUEEN_SIZE - 1;

  return {nMob, bMob, rMob, qMob};
}

static void evalMobility(const BitboardSet& bb,
                         const attacks::AttackInfo& info,
                         int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    auto m = computeMobility(bb, info, c);
    mgScore += sign * (MOB_KNIGHT_MG[m.knight] + MOB_BISHOP_MG[m.bishop]
                     + MOB_ROOK_MG[m.rook]     + MOB_QUEEN_MG[m.queen]);
    egScore += sign * (MOB_KNIGHT_EG[m.knight] + MOB_BISHOP_EG[m.bishop]
                     + MOB_ROOK_EG[m.rook]     + MOB_QUEEN_EG[m.queen]);
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
// Rank-indexed: SHIELD_RANK[0] = pawn on home rank, [1] = one up,
// [2] = two+ advanced, [3] = missing.  Also scores pawn storm (enemy pawns
// advancing toward our king on shield files).
static int evalShieldOneSide(const BitboardSet& bb, Color color) {
  Bitboard king = bb.byPiece[pieceIndex(color, PieceType::KING)];
  if (!king) return 0;
  Square kingSq = lsb(king);
  int kingFile = fileOf(kingSq);

  // Select shield files based on king position; skip if king is in the center.
  int shieldFiles[3];
  if (!selectShieldFiles(kingFile, shieldFiles)) return 0;

  Bitboard friendlyPawns = bb.byPiece[pieceIndex(color, PieceType::PAWN)];
  Color enemy = (color == Color::WHITE) ? Color::BLACK : Color::WHITE;
  Bitboard enemyPawns = bb.byPiece[pieceIndex(enemy, PieceType::PAWN)];
  Bitboard allPawns = bb.byPiece[pieceIndex('P')]
                    | bb.byPiece[pieceIndex('p')];
  bool isWhite = (color == Color::WHITE);

  int score = 0;

  for (int i = 0; i < 3; ++i) {
    int f = shieldFiles[i];
    Bitboard fileMask = fileBB(f);

    // --- Shield pawn rank bonus ---
    Bitboard ownOnFile = friendlyPawns & fileMask;
    if (!ownOnFile) {
      score += SHIELD_RANK[3];  // missing pawn
    } else {
      // Find closest friendly pawn to back rank.
      // White: lowest rank = lsb.  Black: highest rank via byteSwap64.
      Bitboard view = isWhite ? ownOnFile : byteSwap64(ownOnFile);
      int rank = rankOf(lsb(view));  // rank from our perspective
      int dist = rank - 1;           // 0 = home, 1 = one up, 2+ = advanced
      if (dist > 2) dist = 2;
      score += SHIELD_RANK[dist];
    }

    // --- Open file penalty ---
    if (f == kingFile && !(allPawns & fileMask))
      score += SHIELD_OPEN_FILE;

    // --- Pawn storm: enemy pawns advancing toward our king ---
    Bitboard enemyOnFile = enemyPawns & fileMask;
    if (enemyOnFile) {
      Bitboard eView = isWhite ? enemyOnFile : byteSwap64(enemyOnFile);
      int stormRank = rankOf(lsb(eView));  // rank from our perspective
      score += PAWN_STORM[stormRank];
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
// Outposts — bonus for knights and bishops on squares that are:
//   (a) protected by a friendly pawn, and
//   (b) not attackable by any enemy pawn (no enemy pawn on adjacent files
//       that could advance to attack the square).
//
// Knight: Central outposts (d4/d5/e4/e5) receive double the bonus.
// Bishop: Only scored on enemy half (ranks 5-7 for white, 1-3 for black).
//
// Reference: https://www.chessprogramming.org/Outposts
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Outpost detection — tests whether a piece on `sq` occupies an outpost.
//
// An outpost is a square that:
//   1. Is protected by a friendly pawn.
//   2. Cannot be attacked by enemy pawns (no enemy pawns on adjacent files
//      that could advance to reach the square).
//
// Reference: https://www.chessprogramming.org/Outposts
// ---------------------------------------------------------------------------

bool isOutpostSquare(Square sq, int colorIdx,
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

static void evalOutposts(const BitboardSet& bb,
                         Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                         int& mgScore, int& egScore) {

  // Central square constants (LERF) — used by knight outpost evaluation.
  constexpr Square SQ_D4 = 27, SQ_D5 = 35, SQ_E4 = 28, SQ_E5 = 36;

  // Enemy-half masks for bishop outposts (ranks 5-7 for white, ranks 1-3 for black).
  static constexpr Bitboard ENEMY_HALF[2] = {
    rankBB(4) | rankBB(5) | rankBB(6) | rankBB(7),  // White: ranks 5-8
    rankBB(0) | rankBB(1) | rankBB(2) | rankBB(3),  // Black: ranks 1-4
  };

  Bitboard pawnAtks[2] = {whitePawnAtk, blackPawnAtk};

  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    Color color            = COLORS[c];
    Color enemy            = COLORS[1 - c];
    Bitboard friendlyPAtk  = pawnAtks[c];
    Bitboard enemyPawns    = bb.byPiece[pieceIndex(enemy, PieceType::PAWN)];

    // --- Knights ---
    Bitboard knights = bb.byPiece[pieceIndex(color, PieceType::KNIGHT)];
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

    // --- Bishops (enemy half only) ---
    Bitboard bishops = bb.byPiece[pieceIndex(color, PieceType::BISHOP)];
    while (bishops) {
      Square sq = popLsb(bishops);
      if (!(squareBB(sq) & ENEMY_HALF[c])) continue;
      if (!isOutpostSquare(sq, c, friendlyPAtk, enemyPawns)) continue;

      mgScore += sign * BISHOP_OUTPOST_MG;
      egScore += sign * BISHOP_OUTPOST_EG;
    }
  }
}

// ---------------------------------------------------------------------------
// King danger — zone-attack-based nonlinear penalty, MG only.
//
// Per-piece counting: each individual enemy piece whose attacks overlap
// the king zone adds its KING_DANGER_WEIGHT.  Safe check bonuses add to
// the weight when an enemy piece can deliver a check on a square not
// defended by the attacked side.  Total weight indexes into the S-curve
// KING_SAFETY_TABLE.
//
// Attacker threshold: danger = 0 if attackerCount < 2 OR no enemy queen.
//
// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
// Reference: https://www.chessprogramming.org/King_Safety#Attack_Units
// ---------------------------------------------------------------------------

KingDangerInfo computeKingDanger(const BitboardSet& bb,
                                 const attacks::AttackInfo& info, int c) {
  int enemy = 1 - c;
  Color color      = COLORS[c];
  Color enemyColor = COLORS[enemy];
  Bitboard occupied = bb.byColor[0] | bb.byColor[1];

  Bitboard kingBB = bb.byPiece[pieceIndex(color, PieceType::KING)];
  if (!kingBB) return {0, 0, false, false, false, false, false};
  Square kingSq = lsb(kingBB);
  Bitboard kingZone = attacks::KING[kingSq] | squareBB(kingSq);

  int attackWeight  = 0;
  int attackerCount = 0;

  // Per-piece zone attack counting.
  Bitboard kn = bb.byPiece[pieceIndex(enemyColor, PieceType::KNIGHT)];
  while (kn) {
    Square sq = popLsb(kn);
    if (attacks::KNIGHT[sq] & kingZone) {
      attackWeight += KING_DANGER_WEIGHT[0];
      attackerCount++;
    }
  }
  Bitboard bi = bb.byPiece[pieceIndex(enemyColor, PieceType::BISHOP)];
  while (bi) {
    Square sq = popLsb(bi);
    if (attacks::bishop(sq, occupied) & kingZone) {
      attackWeight += KING_DANGER_WEIGHT[1];
      attackerCount++;
    }
  }
  Bitboard ro = bb.byPiece[pieceIndex(enemyColor, PieceType::ROOK)];
  while (ro) {
    Square sq = popLsb(ro);
    if (attacks::rook(sq, occupied) & kingZone) {
      attackWeight += KING_DANGER_WEIGHT[2];
      attackerCount++;
    }
  }
  Bitboard qu = bb.byPiece[pieceIndex(enemyColor, PieceType::QUEEN)];
  bool hasQueen = (qu != 0);
  while (qu) {
    Square sq = popLsb(qu);
    if (attacks::queen(sq, occupied) & kingZone) {
      attackWeight += KING_DANGER_WEIGHT[3];
      attackerCount++;
    }
  }

  // Below threshold — safe checks don't apply.
  if (attackerCount < 2 || !hasQueen)
    return {attackWeight, attackerCount, hasQueen, false, false, false, false};

  // Safe check detection.
  Bitboard safeSq = ~info.byColor[c];

  bool knCheck = (info.byPiece[enemy][raw(PieceType::KNIGHT)]
                & attacks::KNIGHT[kingSq] & safeSq) != 0;
  Bitboard bishopSq = attacks::bishop(kingSq, occupied) & safeSq;
  bool biCheck = (info.byPiece[enemy][raw(PieceType::BISHOP)] & bishopSq) != 0;
  Bitboard rookSq = attacks::rook(kingSq, occupied) & safeSq;
  bool roCheck = (info.byPiece[enemy][raw(PieceType::ROOK)] & rookSq) != 0;
  bool quCheck = (info.byPiece[enemy][raw(PieceType::QUEEN)]
                & (bishopSq | rookSq)) != 0;

  return {attackWeight, attackerCount, hasQueen,
          knCheck, biCheck, roCheck, quCheck};
}

static void evalKingDanger(const BitboardSet& bb,
                           const attacks::AttackInfo& info,
                           int& mgScore) {
  for (int c = 0; c < 2; ++c) {
    auto d = computeKingDanger(bb, info, c);
    if (d.attackerCount < 2 || !d.hasQueen) continue;

    int weight = d.attackWeight;
    if (d.knightSafeCheck) weight += SAFE_CHECK_KNIGHT;
    if (d.bishopSafeCheck) weight += SAFE_CHECK_BISHOP;
    if (d.rookSafeCheck)   weight += SAFE_CHECK_ROOK;
    if (d.queenSafeCheck)  weight += SAFE_CHECK_QUEEN;

    int idx = weight < KING_SAFETY_TABLE_SIZE
            ? weight : KING_SAFETY_TABLE_SIZE - 1;
    mgScore -= SIDE_SIGN[c] * KING_SAFETY_TABLE[idx];
  }
}

// ---------------------------------------------------------------------------
// Space evaluation — Stockfish-style safe square counting.
//
// Counts safe squares for minor pieces on the central four files (c-f),
// ranks 2-4 (white) / 5-7 (black).  Squares behind own pawns count double
// (rearspan bonus).  The raw count is scaled by a weight proportional to
// (piece_count - 2 * open_files)^2 / 16, producing a nonlinear bonus
// that rewards space when there are many pieces on the board.
//
// MG only — space advantage matters primarily in the middlegame.
//
// Reference: https://www.chessprogramming.org/Space
// ---------------------------------------------------------------------------

int countOpenFiles(const BitboardSet& bb) {
  Bitboard allPawns = bb.byPiece[pieceIndex(Color::WHITE, PieceType::PAWN)]
                    | bb.byPiece[pieceIndex(Color::BLACK, PieceType::PAWN)];
  int count = 0;
  for (int f = 0; f < 8; ++f) {
    if (!(allPawns & fileBB(f))) ++count;
  }
  return count;
}

SpaceInfo computeSpace(const BitboardSet& bb, int c, int openFiles) {
  static constexpr Bitboard CENTER_FILES = FILE_C | FILE_D | FILE_E | FILE_F;
  static constexpr Bitboard WHITE_SPACE = CENTER_FILES & (RANK_2 | RANK_3 | RANK_4);
  static constexpr Bitboard BLACK_SPACE = CENTER_FILES & (RANK_5 | RANK_6 | RANK_7);
  static constexpr Bitboard SPACE_MASKS[] = {WHITE_SPACE, BLACK_SPACE};

  Color color = COLORS[c];
  Bitboard ownPawns   = bb.byPiece[pieceIndex(color, PieceType::PAWN)];
  Bitboard enemyPawnAtk = (c == 0)
      ? ((bb.byPiece[pieceIndex(Color::BLACK, PieceType::PAWN)] >> 7) & NOT_FILE_A)
      | ((bb.byPiece[pieceIndex(Color::BLACK, PieceType::PAWN)] >> 9) & NOT_FILE_H)
      : ((bb.byPiece[pieceIndex(Color::WHITE, PieceType::PAWN)] << 7) & NOT_FILE_H)
      | ((bb.byPiece[pieceIndex(Color::WHITE, PieceType::PAWN)] << 9) & NOT_FILE_A);

  Bitboard safe = SPACE_MASKS[c] & ~ownPawns & ~enemyPawnAtk;

  Bitboard behind = ownPawns;
  if (c == 0) {
    behind |= behind >> 8;
    behind |= behind >> 16;
  } else {
    behind |= behind << 8;
    behind |= behind << 16;
  }

  int bonus = popcount(safe) + popcount(safe & behind);

  int pieceCount = popcount(bb.byColor[c]);
  int weight = pieceCount - 2 * openFiles;
  if (weight < 0) weight = 0;

  return {bonus, weight};
}

static void evalSpace(const BitboardSet& bb, int& mgScore) {
  int openFiles = countOpenFiles(bb);
  for (int c = 0; c < 2; ++c) {
    auto s = computeSpace(bb, c, openFiles);
    mgScore += SIDE_SIGN[c] * SPACE_WEIGHT * s.bonus * s.weight * s.weight / 16;
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

// ---------------------------------------------------------------------------
// Mop-up evaluation — bonus for driving the losing king toward edges/corners
// when one side has a decisive material advantage.
//
// In won endgames, standard material + PST evaluation provides insufficient
// incentive to deliver checkmate.  The king EG PST penalises corners by only
// ~50cp — too weak to guide mating plans.  This term adds a direct bonus for:
//   1. Pushing the losing king away from the center (Center Manhattan Dist).
//   2. Keeping the winning king close to the losing king (Chebyshev proximity).
//
// Applied to EG score only; the tapered blend phases it in naturally.
// Material is recomputed from bitboards (~12 popcounts) rather than passed
// as a parameter to avoid changing the evaluateImpl API surface.
//
// Reference: https://www.chessprogramming.org/Mop-up_Evaluation
// ---------------------------------------------------------------------------

static void evalMopUp(const BitboardSet& bb, int& egScore) {
  int material = eval::computeMaterial(bb);
  int absMat = material > 0 ? material : -material;
  if (absMat < MOPUP_THRESHOLD) return;

  int winColor  = material > 0 ? 0 : 1;
  int loseColor = 1 - winColor;
  int sign = SIDE_SIGN[winColor];

  Square winKingSq  = lsb(bb.byPiece[pieceIndex(COLORS[winColor],
                                                  PieceType::KING)]);
  Square loseKingSq = lsb(bb.byPiece[pieceIndex(COLORS[loseColor],
                                                  PieceType::KING)]);

  int cmdBonus   = eval::centerManhattanDist(loseKingSq) * MOPUP_CMD_WEIGHT;
  int closeBonus = (14 - eval::chebyshevDistance(winKingSq, loseKingSq))
                   * MOPUP_CLOSE_KING;

  egScore += sign * (cmdBonus + closeBonus);
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
  evalPawnStructure(bb, mgScore, egScore, passedPawns, pawnHash);
  evalPassedPawnKingDist(bb, passedPawns, egScore);
  evalOutsidePasser(bb, passedPawns, egScore);
  evalKingPawnTropism(bb, egScore);

  evalBishopPair(bb, mgScore, egScore);
  evalBadBishop(bb, mgScore, egScore);
  evalRookFiles(bb, mgScore, egScore);
  evalRookOnSeventh(bb, mgScore, egScore);
  evalRookBehindPasser(bb, passedPawns, egScore);
  evalOutposts(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore);
  evalKingSafety(bb, mgScore, egScore);

  evalMobility(bb, info, mgScore, egScore);
  evalThreats(bb, info, mgScore, egScore);
  evalKingDanger(bb, info, mgScore);
  evalSpace(bb, mgScore);
  evalMopUp(bb, egScore);

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
