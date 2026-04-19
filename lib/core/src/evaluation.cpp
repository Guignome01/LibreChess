#include "evaluation.h"
#include "eval/internal.h"
#include "eval/params.h"
#include "attacks.h"
#include "piece.h"
#include "position.h"
#include "utils.h"
#include "zobrist.h"

// Under TUNING, evaluation.cpp is the sole TU that owns EVAL_CONST param
// definitions (to avoid multi-definition at link time).  The split eval/*.cpp
// files guard their bodies with a `!TUNING || EVAL_DEFINER` condition and
// produce empty objects in the tuner build.  To keep the sub-evaluators
// (pawn/pieces/king_safety) available in TUNING mode we inline their bodies
// into this TU directly by defining EVAL_DEFINER before the #include.
// See lib/core/src/eval/pawn.cpp for details.
#ifdef TUNING
#define EVAL_DEFINER
#include "eval/pawn.cpp"
#include "eval/pieces.cpp"
#include "eval/king_safety.cpp"
#undef EVAL_DEFINER
#endif

#include <cstring>       // memset
#include <new>           // nothrow

namespace {

using namespace LibreChess;
using piece::pieceIndex;

}  // anonymous namespace

// ===========================================================================
// eval — evaluation constants, helpers, and public API
// ===========================================================================

namespace LibreChess {
namespace eval {

// Bring the shared internal helpers (PAWN_MASKS, PAWN_RANK_MASKS,
// SIDE_SIGN, COLORS, passedMask, forwardMask) from eval/internal.h into
// this TU so the remaining static evaluators below (evalSpace, evalMopUp,
// applyOCBScaling) read the same way they used to when everything lived
// in one file.
using detail::PAWN_MASKS;
using detail::PAWN_RANK_MASKS;
using detail::SIDE_SIGN;
using detail::COLORS;
using detail::passedMask;
using detail::forwardMask;

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
  Bitboard behindOrLevel =
      PAWN_RANK_MASKS.behindOrLevel[color == Color::WHITE ? 0 : 1][rank];
  if (adjFriendly & behindOrLevel) return false;

  // Stop square must be controlled by an enemy pawn.
  Square stopSq = (color == Color::WHITE) ? sq + 8 : sq - 8;
  Bitboard enemyPawnAtk = (color == Color::WHITE)
      ? (shiftSE(enemyPawns) | shiftSW(enemyPawns))
      : (shiftNE(enemyPawns) | shiftNW(enemyPawns));

  return (squareBB(stopSq) & enemyPawnAtk) != 0;
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

// ---------------------------------------------------------------------------
// Space evaluation — safe square counting.
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
// as a parameter to avoid changing the evaluatePosition API surface.
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

int evaluatePosition(const Position& pos, PawnHashTable* pawnHash) {
  const BitboardSet& bb = pos.bitboards();
  int mgScore = pos.mgPST();
  int egScore = pos.egPST();
  // Full attack computation first — shared by pawn structure, mobility,
  // king danger, knight outposts, and space evaluation.
  // Attack tables are constexpr — no initialization required.
  attacks::AttackInfo info = attacks::computeAll(bb);

  // Extract pawn attacks from AttackInfo (already computed via bulk shift
  // inside computeAll) — avoids redundant manual shift-OR computation.
  Bitboard whitePawnAtk = info.byPiece[raw(Color::WHITE)][raw(PieceType::PAWN)];
  Bitboard blackPawnAtk = info.byPiece[raw(Color::BLACK)][raw(PieceType::PAWN)];

  Bitboard passedPawns[2] = {0, 0};
  detail::evalPawnStructure(bb, mgScore, egScore, passedPawns, pawnHash);
  detail::evalPassedPawnKingDist(bb, passedPawns, egScore);
  detail::evalOutsidePasser(bb, passedPawns, egScore);
  detail::evalKingPawnTropism(bb, egScore);

  detail::evalBishopPair(bb, mgScore, egScore);
  detail::evalBadBishop(bb, mgScore, egScore);
  detail::evalRookFiles(bb, mgScore, egScore);
  detail::evalRookOnSeventh(bb, mgScore, egScore);
  detail::evalRookBehindPasser(bb, passedPawns, egScore);
  detail::evalOutposts(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore);
  detail::evalKingSafety(bb, mgScore, egScore);

  detail::evalMobility(bb, info, mgScore, egScore);
  detail::evalThreats(bb, info, mgScore, egScore);
  detail::evalKingDanger(bb, info, mgScore);
  evalSpace(bb, mgScore);
  evalMopUp(bb, egScore);

  int phase = pos.phase();
  int score = (mgScore * phase + egScore * (MAX_PHASE - phase)) / MAX_PHASE;

  score = applyOCBScaling(score, bb, phase);

  return score;
}

}  // namespace eval
}  // namespace LibreChess
