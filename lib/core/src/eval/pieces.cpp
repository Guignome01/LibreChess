// ---------------------------------------------------------------------------
// eval/pieces.cpp — piece-related evaluation terms.
//
// Contains:
//   • evalBishopPair         — bonus when a side has both bishops.
//   • evalBadBishop          — penalty per own pawn on the bishop's color.
//   • evalRookFiles          — rook on open / semi-open file.
//   • evalRookOnSeventh      — rook on enemy 2nd rank vs. back-rank king.
//   • evalRookBehindPasser   — Tarrasch rule (EG).
//   • evalOutposts           — knights and bishops on protected, unattackable
//                              squares; bishops restricted to enemy half.
//   • evalMobility           — nonlinear lookup over safe-square counts.
//   • evalThreats            — attacks on enemy pieces (4 categories).
//
// All functions live in namespace LibreChess::eval::detail and are composed
// by evaluateImpl() in evaluation.cpp.
// ---------------------------------------------------------------------------

// See eval/pawn.cpp for the rationale behind the EVAL_DEFINER guard.
#if !defined(TUNING) || defined(EVAL_DEFINER)

#include "internal.h"
#include "params.h"
#include "../attacks.h"
#include "../piece.h"

namespace LibreChess {
namespace eval {
namespace detail {

using piece::pieceIndex;

// ---------------------------------------------------------------------------
// Bishop pair — bonus when a side has both bishops.
// Reference: https://www.chessprogramming.org/Bishop_Pair
// ---------------------------------------------------------------------------

void evalBishopPair(const BitboardSet& bb,
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
// Bad bishop — penalty for bishops blocked by own pawns on same color.
//
// A bishop loses effectiveness when many friendly pawns occupy squares
// of the same color complex, restricting its mobility.
//
// Reference: https://www.chessprogramming.org/Bad_Bishop
// ---------------------------------------------------------------------------

void evalBadBishop(const BitboardSet& bb,
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
// Rook on open/semi-open file — bonus for rooks not blocked by own pawns.
// Reference: https://www.chessprogramming.org/Rook_on_Open_File
// ---------------------------------------------------------------------------

void evalRookFiles(const BitboardSet& bb,
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

void evalRookOnSeventh(const BitboardSet& bb,
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
// Rook behind passed pawn (Tarrasch Rule) — EG only.
//
// A rook behind a friendly passed pawn supports its advance; behind an
// enemy passed pawn, it restricts the advance.
//
// Reference: https://www.chessprogramming.org/Tarrasch_Rule
// ---------------------------------------------------------------------------

void evalRookBehindPasser(const BitboardSet& bb,
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

void evalOutposts(const BitboardSet& bb,
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
      if (!eval::isOutpostSquare(sq, c, friendlyPAtk, enemyPawns)) continue;

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
      if (!eval::isOutpostSquare(sq, c, friendlyPAtk, enemyPawns)) continue;

      mgScore += sign * BISHOP_OUTPOST_MG;
      egScore += sign * BISHOP_OUTPOST_EG;
    }
  }
}

// ---------------------------------------------------------------------------
// Mobility — nonlinear lookup tables, safe squares only.
// Reference: https://www.chessprogramming.org/Mobility
// ---------------------------------------------------------------------------

void evalMobility(const BitboardSet& bb,
                  const attacks::AttackInfo& info,
                  int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    auto m = eval::computeMobility(bb, info, c);
    mgScore += sign * (MOB_KNIGHT_MG[m.knight] + MOB_BISHOP_MG[m.bishop]
                     + MOB_ROOK_MG[m.rook]     + MOB_QUEEN_MG[m.queen]);
    egScore += sign * (MOB_KNIGHT_EG[m.knight] + MOB_BISHOP_EG[m.bishop]
                     + MOB_ROOK_EG[m.rook]     + MOB_QUEEN_EG[m.queen]);
  }
}

// ---------------------------------------------------------------------------
// Threats — bonus for attacking poorly defended enemy pieces.
// Reference: https://www.chessprogramming.org/Threat_Move
// ---------------------------------------------------------------------------

void evalThreats(const BitboardSet& bb,
                 const attacks::AttackInfo& info,
                 int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = SIDE_SIGN[c];
    auto f = eval::computeThreats(bb, info, c);
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

}  // namespace detail
}  // namespace eval
}  // namespace LibreChess

#endif  // !TUNING || EVAL_DEFINER
