// ---------------------------------------------------------------------------
// eval/internal.h — cross-TU shared declarations for the evaluation module.
//
// Split of evaluation.cpp into orchestration (evaluation.cpp, stays at
// lib/core/src/) + per-topic TUs in lib/core/src/eval/:
//
//   pawn.cpp         — pawn structure, passers, king-pawn tropism
//   pieces.cpp       — bishop pair/bad bishop, rook files/7th/behind-passer,
//                      outposts, mobility, threats
//   king_safety.cpp  — pawn shield, pawn storm, king danger
//
// This header is INTERNAL — consumed only by evaluation.cpp, its sibling
// TUs under eval/, and trace.cpp (TUNING builds).  It is never included
// from outside the core library.
//
// Contents:
//   • Shared constexpr pawn masks (PAWN_MASKS, PAWN_RANK_MASKS).
//   • Color-loop lookup tables (SIDE_SIGN, COLORS).
//   • Inline helpers passedMask / forwardMask.
//   • Forward declarations for the eval::detail sub-evaluators implemented
//     across the three eval/*.cpp TUs and composed by evaluatePosition in
//     evaluation.cpp.
//
// Reference: https://www.chessprogramming.org/Evaluation
// ---------------------------------------------------------------------------

#ifndef LIBRECHESS_EVAL_INTERNAL_H
#define LIBRECHESS_EVAL_INTERNAL_H

#include "../bitboard.h"
#include "../types.h"
#include "../attacks.h"      // attacks::AttackInfo
#include "../evaluation.h"   // PawnHashTable

namespace LibreChess {
namespace eval {
namespace detail {

// ---------------------------------------------------------------------------
// Pawn-structure masks — constexpr, placed in .rodata (Flash on ESP32).
// White-only storage; black masks derived via vertical mirror.
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

inline constexpr PawnMasks makePawnMasks() { return PawnMasks{}; }

// Storage definition lives in eval/internal.cpp (single TU owns the data;
// other TUs see extern const declarations below).  We can't use `inline
// constexpr` here because the native test toolchain (g++ 5.1) predates
// C++17 inline variables.
extern const PawnMasks PAWN_MASKS;

// ---------------------------------------------------------------------------
// Rank-indexed pawn masks — [colorIndex][rank].
//   ahead[c][r]           : all squares strictly ahead of rank r for color c.
//   behindOrLevel[c][r]   : all squares at or behind rank r for color c.
// ---------------------------------------------------------------------------

struct PawnRankMasks {
  Bitboard ahead[2][8] = {};
  Bitboard behindOrLevel[2][8] = {};
  constexpr PawnRankMasks() {
    for (int r = 0; r < 8; ++r) {
      Bitboard wAhead = 0, wBehindOrLevel = 0;
      for (int rr = r + 1; rr < 8; ++rr)
        wAhead |= (static_cast<Bitboard>(0xFF) << (8 * rr));
      for (int rr = 0; rr <= r; ++rr)
        wBehindOrLevel |= (static_cast<Bitboard>(0xFF) << (8 * rr));
      ahead[0][r] = wAhead;
      behindOrLevel[0][r] = wBehindOrLevel;

      Bitboard bAhead = 0, bBehindOrLevel = 0;
      for (int rr = 0; rr < r; ++rr)
        bAhead |= (static_cast<Bitboard>(0xFF) << (8 * rr));
      for (int rr = r; rr < 8; ++rr)
        bBehindOrLevel |= (static_cast<Bitboard>(0xFF) << (8 * rr));
      ahead[1][r] = bAhead;
      behindOrLevel[1][r] = bBehindOrLevel;
    }
  }
};

extern const PawnRankMasks PAWN_RANK_MASKS;

// ---------------------------------------------------------------------------
// Color-loop helpers — lookup tables for bilateral evaluation.
// Reference: "Lookup Tables over Branching" (project principle).
// ---------------------------------------------------------------------------

extern const int SIDE_SIGN[2];
extern const Color COLORS[2];

// ---------------------------------------------------------------------------
// Color-agnostic pawn mask accessors.  White: direct lookup.
// Black: mirror the white mask for the vertically-reflected square.
// ---------------------------------------------------------------------------

inline Bitboard passedMask(Color c, Square sq) {
  if (c == Color::WHITE) return PAWN_MASKS.passed[sq];
  return byteSwap64(PAWN_MASKS.passed[sq ^ 56]);
}

inline Bitboard forwardMask(Color c, Square sq) {
  if (c == Color::WHITE) return PAWN_MASKS.forward[sq];
  return byteSwap64(PAWN_MASKS.forward[sq ^ 56]);
}

// ---------------------------------------------------------------------------
// Sub-evaluator forward declarations.
//
// Each function mutates mgScore/egScore (white-relative centipawns).
// Grouped by TU.  Composed by evaluatePosition() in evaluation.cpp.
// ---------------------------------------------------------------------------

// ---- pawn.cpp -------------------------------------------------------------
void evalPawnStructure(const BitboardSet& bb,
                       int& mgScore, int& egScore,
                       Bitboard passedPawns[2],
                       PawnHashTable* pawnHash);

void evalPassedPawnKingDist(const BitboardSet& bb,
                            const Bitboard passedPawns[2],
                            int& egScore);

void evalOutsidePasser(const BitboardSet& bb,
                       const Bitboard passedPawns[2],
                       int& egScore);

void evalKingPawnTropism(const BitboardSet& bb, int& egScore);

// ---- pieces.cpp -----------------------------------------------------------
void evalBishopPair(const BitboardSet& bb, int& mgScore, int& egScore);
void evalBadBishop(const BitboardSet& bb, int& mgScore, int& egScore);
void evalRookFiles(const BitboardSet& bb, int& mgScore, int& egScore);
void evalRookOnSeventh(const BitboardSet& bb, int& mgScore, int& egScore);
void evalRookBehindPasser(const BitboardSet& bb,
                          const Bitboard passedPawns[2],
                          int& egScore);
void evalOutposts(const BitboardSet& bb,
                  Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                  int& mgScore, int& egScore);
void evalMobility(const BitboardSet& bb,
                  const attacks::AttackInfo& info,
                  int& mgScore, int& egScore);
void evalThreats(const BitboardSet& bb,
                 const attacks::AttackInfo& info,
                 int& mgScore, int& egScore);

// ---- king_safety.cpp ------------------------------------------------------
void evalKingSafety(const BitboardSet& bb, int& mgScore, int& egScore);
void evalKingDanger(const BitboardSet& bb,
                    const attacks::AttackInfo& info,
                    int& mgScore);

}  // namespace detail
}  // namespace eval
}  // namespace LibreChess

#endif  // LIBRECHESS_EVAL_INTERNAL_H
