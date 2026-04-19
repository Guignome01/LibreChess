// ---------------------------------------------------------------------------
// eval/king_safety.cpp — king-safety evaluation terms (MG only).
//
// Contains:
//   • evalKingSafety  — pawn shield (rank bonus, open-file penalty) and
//                       pawn storm for both sides.
//   • evalKingDanger  — attacker-count / safe-check S-curve, using the
//                       shared public eval::computeKingDanger() helper.
//
// All functions live in namespace LibreChess::eval::detail and are composed
// by evaluateImpl() in evaluation.cpp.
//
// Reference: https://www.chessprogramming.org/King_Safety
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

// ---------------------------------------------------------------------------
// Evaluate pawn shield for one side. Returns MG bonus (positive = good).
// Rank-indexed: SHIELD_RANK[0] = pawn on home rank, [1] = one up,
// [2] = two+ advanced, [3] = missing.  Also scores pawn storm (enemy pawns
// advancing toward our king on shield files).
// ---------------------------------------------------------------------------

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

void evalKingSafety(const BitboardSet& bb,
                    int& mgScore, int& /* egScore */) {
  mgScore += evalShieldOneSide(bb, Color::WHITE);
  mgScore -= evalShieldOneSide(bb, Color::BLACK);
}

// ---------------------------------------------------------------------------
// King danger — zone-attack-based nonlinear penalty, MG only.
//
// Delegates piece-counting and safe-check detection to the public
// eval::computeKingDanger() helper (shared with trace.cpp), then applies
// the S-curve bonus table.
//
// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
// Reference: https://www.chessprogramming.org/King_Safety#Attack_Units
// ---------------------------------------------------------------------------

void evalKingDanger(const BitboardSet& bb,
                    const attacks::AttackInfo& info,
                    int& mgScore) {
  for (int c = 0; c < 2; ++c) {
    auto d = eval::computeKingDanger(bb, info, c);
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

}  // namespace detail
}  // namespace eval
}  // namespace LibreChess

#endif  // !TUNING || EVAL_DEFINER
