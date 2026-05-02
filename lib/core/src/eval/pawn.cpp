// ---------------------------------------------------------------------------
// eval/pawn.cpp — pawn-structure evaluation terms.
//
// Contains:
//   • evalPawnStructure      — passed/isolated/doubled/backward/connected,
//                              plus pawn hash probe/store.
//   • evalPassedPawnKingDist — king proximity to passers (EG); unstoppable
//                              passer and pawn-race detection.
//   • evalOutsidePasser      — passer outside enemy pawn range (EG).
//   • evalKingPawnTropism    — king distance to all pawns (EG).
//
// All functions live in namespace LibreChess::eval::detail and are composed
// by evaluatePosition() in evaluation.cpp.
//
// Reference: https://www.chessprogramming.org/Pawn_Structure
// ---------------------------------------------------------------------------

// Under TUNING, evaluation.cpp is the sole TU that owns EVAL_CONST param
// definitions (EVAL_CONST expands to empty in TUNING, so any TU including
// eval/params.h defines the globals).  To preserve a single definition
// site while keeping the code split, evaluation.cpp sets EVAL_DEFINER and
// `#include`s this file directly.  Standalone compilation under TUNING is
// skipped (produces an empty object) to avoid ODR conflicts.
#if !defined(TUNING) || defined(EVAL_DEFINER)

#include "internal.h"
#include "params.h"
#include "../attacks.h"
#include "../piece.h"
#include "../zobrist.h"

namespace LibreChess {
namespace eval {
namespace detail {

using piece::pieceIndex;

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

void evalPawnStructure(const BitboardSet& bb,
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

      bool doubled = eval::isDoubled(sq, color, friendly);

      // --- Passed pawn ---
      // Only the frontmost pawn on a file is scored as passed (doubled fix).
      bool passed = eval::isPassed(sq, color, enemy);
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
          Bitboard aheadMask = PAWN_RANK_MASKS.ahead[c][rank];
          Bitboard behindOrLevel = PAWN_RANK_MASKS.behindOrLevel[c][rank];
          int sentries = popcount(enemy & adjFiles & aheadMask);
          int helpers = popcount(friendly & adjFiles & behindOrLevel);
          if (helpers >= sentries) {
            mg += sign * CANDIDATE_PASSED_MG[passedRank];
            eg += sign * CANDIDATE_PASSED_EG[passedRank];
          }
        }
      }

      // --- Isolated pawn ---
      if (eval::isIsolated(sq, friendly)) {
        mg += sign * ISOLATED_PENALTY_MG;
        eg += sign * ISOLATED_PENALTY_EG;
      }

      // --- Doubled pawn ---
      if (doubled) {
        eg += sign * DOUBLED_PENALTY_EG;
      }

      // --- Backward pawn ---
      // Reference: https://www.chessprogramming.org/Backward_Pawn
      if (eval::isBackward(sq, color, friendly, enemy)) {
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

void evalPassedPawnKingDist(const BitboardSet& bb,
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

void evalOutsidePasser(const BitboardSet& bb,
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

void evalKingPawnTropism(const BitboardSet& bb, int& egScore) {
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

}  // namespace detail
}  // namespace eval
}  // namespace LibreChess

#endif  // !TUNING || EVAL_DEFINER
