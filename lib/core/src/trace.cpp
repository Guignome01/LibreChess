// ---------------------------------------------------------------------------
// trace.cpp — tuning infrastructure implementation.
//
// Contains: parameter registry, trace extraction (mirrors evaluatePosition),
// descriptor getters (scalar, mobility, PST), and accessor wrappers.
//
// Merged from tools/tune/trace.cpp + tools/tune/tune_registry.cpp so that
// all tuning infrastructure lives in two files (trace.h + trace.cpp)
// alongside the evaluation code it mirrors.
//
// Everything is guarded by #ifdef TUNING — production and test builds
// compile this file to nothing.  The tuner Makefile compiles all files in
// lib/core/src/ with -DTUNING, so this participates via the core wildcard.
//
// Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method
// ---------------------------------------------------------------------------

#ifdef TUNING

#include "trace.h"
#include "attacks.h"
#include "evaluation.h"
#include "piece.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace LibreChess {
namespace eval {

using piece::pieceIndex;

// ===========================================================================
// Parameter index maps (built once from the tuning registry).
//
// ptrMap:  eval variable address → registry index.  Used by extractTrace()
//          to look up param indices directly from eval param pointers,
//          eliminating the manual TraceIndices struct and initTraceIndices().
//
// nameMap: param name → registry index.  Used by tune.cpp (printResults).
// ===========================================================================

/// Runtime registry entry — one tunable parameter with metadata.
struct TuneEntry {
  const char* name;
  int* ptr;
  int defaultVal;  // Snapshot of *ptr at registration time (single source of truth).
  int min, max, step;
};

static std::vector<TuneEntry>& buildRegistry();  // forward decl

static std::unordered_map<int*, int> ptrMap;
static std::unordered_map<std::string, int> nameMap;

void buildParamMap() {
  auto& reg = buildRegistry();
  ptrMap.reserve(reg.size());
  nameMap.reserve(reg.size());
  for (int i = 0; i < static_cast<int>(reg.size()); ++i) {
    ptrMap[reg[i].ptr] = i;
    nameMap[reg[i].name] = i;
  }
}

int findParam(const char* name) {
  auto it = nameMap.find(name);
  return (it != nameMap.end()) ? it->second : -1;
}

/// Returns the registry index for an eval parameter, or -1 if not registered.
/// Enables extractTrace() to reference eval params by address — no manual
/// name→field mapping required.
static int pIdx(int* ptr) {
  auto it = ptrMap.find(ptr);
  return (it != ptrMap.end()) ? it->second : -1;
}

// PST data pointers — maps table index (0–11) to the actual PST array.
// Used by extractTrace() for the PST loop.
// clang-format off
static int* const PST_DATA[12] = {
  PST_PAWN_MG, PST_KNIGHT_MG, PST_BISHOP_MG,
  PST_ROOK_MG, PST_QUEEN_MG,  PST_KING_MG,
  PST_PAWN_EG, PST_KNIGHT_EG, PST_BISHOP_EG,
  PST_ROOK_EG, PST_QUEEN_EG,  PST_KING_EG,
};
// clang-format on

// ===========================================================================
// Trace extraction — mirrors evaluatePosition() but records coefficients
// ===========================================================================

Trace extractTrace(const BitboardSet& bb) {
  Trace t;
  t.entries.reserve(40);

  // -----------------------------------------------------------------------
  // Phase computation (must happen first for tapering).
  // -----------------------------------------------------------------------
  int phase = popcount(bb.byPiece[pieceIndex('N')]
                     | bb.byPiece[pieceIndex('n')]) * PHASE_KNIGHT
            + popcount(bb.byPiece[pieceIndex('B')]
                     | bb.byPiece[pieceIndex('b')]) * PHASE_BISHOP
            + popcount(bb.byPiece[pieceIndex('R')]
                     | bb.byPiece[pieceIndex('r')])   * PHASE_ROOK
            + popcount(bb.byPiece[pieceIndex('Q')]
                     | bb.byPiece[pieceIndex('q')]) * PHASE_QUEEN;
  if (phase > MAX_PHASE) phase = MAX_PHASE;

  float mgWeight = static_cast<float>(phase) / MAX_PHASE;
  float egWeight = static_cast<float>(MAX_PHASE - phase) / MAX_PHASE;

  // -----------------------------------------------------------------------
  // OCB scaling detection — applies to the full tapered score.
  //
  // evaluatePosition() applies OCB as: score = score * 3/4 after tapering.
  // To match, scale BOTH MG and EG weights by ocbScale so every trace
  // coefficient receives the same uniform scaling.
  // -----------------------------------------------------------------------
  float ocbScale = 1.0f;
  if (phase <= 6) {
    Bitboard wb = bb.byPiece[pieceIndex('B')];
    Bitboard bbish = bb.byPiece[pieceIndex('b')];
    if (popcount(wb) == 1 && popcount(bbish) == 1) {
      bool whiteDark = (wb & DARK_SQUARES) != 0;
      bool blackDark = (bbish & DARK_SQUARES) != 0;
      if (whiteDark != blackDark) ocbScale = 0.75f;
    }
  }

  float mgW = mgWeight * ocbScale;
  float egW = egWeight * ocbScale;

  // -----------------------------------------------------------------------
  // Material (P, N, B, R, Q) — all tunable, separate MG and EG.
  // -----------------------------------------------------------------------
  for (int i = 0; i < 5; ++i) {
    int wCount = popcount(bb.byPiece[i]);
    int bCount = popcount(bb.byPiece[i + 6]);
    float diff = static_cast<float>(wCount - bCount);
    t.add(pIdx(&MATERIAL[i]), diff * mgW);
    t.add(pIdx(&MATERIAL_EG[i]), diff * egW);
  }

  // -----------------------------------------------------------------------
  // PST — per piece, per square, separate MG and EG.
  // -----------------------------------------------------------------------
  for (int pieceType = 0; pieceType < 6; ++pieceType) {
    int mgTable = pieceType;
    int egTable = pieceType + 6;

    Bitboard white = bb.byPiece[pieceType];
    while (white) {
      Square sq = popLsb(white);
      t.add(pIdx(PST_DATA[mgTable] + sq), mgW);
      t.add(pIdx(PST_DATA[egTable] + sq), egW);
    }

    Bitboard black = bb.byPiece[pieceType + 6];
    while (black) {
      Square sq = popLsb(black);
      int mirSq = sq ^ 56;
      t.add(pIdx(PST_DATA[mgTable] + mirSq), -mgW);
      t.add(pIdx(PST_DATA[egTable] + mirSq), -egW);
    }
  }

  // -----------------------------------------------------------------------
  // Pawn structure — uses the core eval helpers (isPassed, isIsolated, etc.)
  // -----------------------------------------------------------------------
  Bitboard whitePawns = bb.byPiece[pieceIndex('P')];
  Bitboard blackPawns = bb.byPiece[pieceIndex('p')];
  Bitboard whitePawnAttacks = shiftNE(whitePawns) | shiftNW(whitePawns);
  Bitboard blackPawnAttacks = shiftSE(blackPawns) | shiftSW(blackPawns);

  // Accumulate pawn coefficients (multiple pawns may contribute to the same
  // parameter, e.g. two pawns on the same rank for passed bonus).
  std::unordered_map<int, float> pawnCoeffs;
  auto addPawnCoeff = [&](int idx, float c) {
    if (idx >= 0 && c != 0.0f) pawnCoeffs[idx] += c;
  };

  uint8_t whitePassedFiles = 0, blackPassedFiles = 0;

  Bitboard wp = whitePawns;
  while (wp) {
    Square sq = popLsb(wp);
    int rank = rankOf(sq);

    if (isPassed(sq, Color::WHITE, blackPawns)) {
      addPawnCoeff(pIdx(&PASSED_RANK_BONUS_MG[rank]), mgW);
      addPawnCoeff(pIdx(&PASSED_RANK_BONUS_EG[rank]), egW);
      whitePassedFiles |= 1 << fileOf(sq);
      if (squareBB(sq) & whitePawnAttacks) {
        addPawnCoeff(pIdx(&PROTECTED_PASSER_MG), mgW);
      }
    }
    if (isIsolated(sq, whitePawns)) {
      addPawnCoeff(pIdx(&ISOLATED_PENALTY_MG), mgW);
      addPawnCoeff(pIdx(&ISOLATED_PENALTY_EG), egW);
    }
    if (isDoubled(sq, Color::WHITE, whitePawns)) {
      addPawnCoeff(pIdx(&DOUBLED_PENALTY_MG), mgW);
      addPawnCoeff(pIdx(&DOUBLED_PENALTY_EG), egW);
    }
    if (isBackward(sq, Color::WHITE, whitePawns, blackPawnAttacks)) {
      addPawnCoeff(pIdx(&BACKWARD_PENALTY_MG), mgW);
      addPawnCoeff(pIdx(&BACKWARD_PENALTY_EG), egW);
    }
  }

  Bitboard bp = blackPawns;
  while (bp) {
    Square sq = popLsb(bp);
    int rank = rankOf(sq);

    if (isPassed(sq, Color::BLACK, whitePawns)) {
      int mirRank = 7 - rank;
      addPawnCoeff(pIdx(&PASSED_RANK_BONUS_MG[mirRank]), -mgW);
      addPawnCoeff(pIdx(&PASSED_RANK_BONUS_EG[mirRank]), -egW);
      blackPassedFiles |= 1 << fileOf(sq);
      if (squareBB(sq) & blackPawnAttacks) {
        addPawnCoeff(pIdx(&PROTECTED_PASSER_MG), -mgW);
      }
    }
    if (isIsolated(sq, blackPawns)) {
      addPawnCoeff(pIdx(&ISOLATED_PENALTY_MG), -mgW);
      addPawnCoeff(pIdx(&ISOLATED_PENALTY_EG), -egW);
    }
    if (isDoubled(sq, Color::BLACK, blackPawns)) {
      addPawnCoeff(pIdx(&DOUBLED_PENALTY_MG), -mgW);
      addPawnCoeff(pIdx(&DOUBLED_PENALTY_EG), -egW);
    }
    if (isBackward(sq, Color::BLACK, blackPawns, whitePawnAttacks)) {
      addPawnCoeff(pIdx(&BACKWARD_PENALTY_MG), -mgW);
      addPawnCoeff(pIdx(&BACKWARD_PENALTY_EG), -egW);
    }
  }

  for (int f = 0; f < 7; ++f) {
    if ((whitePassedFiles >> f & 1) && (whitePassedFiles >> (f + 1) & 1)) {
      addPawnCoeff(pIdx(&CONNECTED_PASSED_MG), mgW);
      addPawnCoeff(pIdx(&CONNECTED_PASSED_EG), egW);
    }
    if ((blackPassedFiles >> f & 1) && (blackPassedFiles >> (f + 1) & 1)) {
      addPawnCoeff(pIdx(&CONNECTED_PASSED_MG), -mgW);
      addPawnCoeff(pIdx(&CONNECTED_PASSED_EG), -egW);
    }
  }

  for (auto& pc : pawnCoeffs) t.add(pc.first, pc.second);

  // -----------------------------------------------------------------------
  // Passed pawn king distance (EG only).
  // -----------------------------------------------------------------------
  Bitboard wkBB = bb.byPiece[pieceIndex('K')];
  Bitboard bkBB = bb.byPiece[pieceIndex('k')];
  if (wkBB && bkBB) {
    Square wkSq = lsb(wkBB);
    Square bkSq = lsb(bkBB);
    float ownCoeff = 0.0f, enemyCoeff = 0.0f;

    Bitboard wpPass = whitePawns;
    while (wpPass) {
      Square sq = popLsb(wpPass);
      if (!isPassed(sq, Color::WHITE, blackPawns)) continue;
      ownCoeff   += (7 - chebyshevDist(sq, wkSq));
      enemyCoeff += chebyshevDist(sq, bkSq);
    }
    Bitboard bpPass = blackPawns;
    while (bpPass) {
      Square sq = popLsb(bpPass);
      if (!isPassed(sq, Color::BLACK, whitePawns)) continue;
      ownCoeff   -= (7 - chebyshevDist(sq, bkSq));
      enemyCoeff -= chebyshevDist(sq, wkSq);
    }
    t.add(pIdx(&PASSER_OWN_KING), ownCoeff * egW);
    t.add(pIdx(&PASSER_ENEMY_KING), enemyCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Bishop pair (separate MG/EG).
  // -----------------------------------------------------------------------
  {
    int wBP = (popcount(bb.byPiece[pieceIndex('B')]) >= 2) ? 1 : 0;
    int bBP = (popcount(bb.byPiece[pieceIndex('b')]) >= 2) ? 1 : 0;
    float diff = static_cast<float>(wBP - bBP);
    t.add(pIdx(&BISHOP_PAIR_MG), diff * mgW);
    t.add(pIdx(&BISHOP_PAIR_EG), diff * egW);
  }

  // -----------------------------------------------------------------------
  // Bad bishop (separate MG/EG).
  // -----------------------------------------------------------------------
  {
    float mgCoeff = 0.0f, egCoeff = 0.0f;
    Bitboard wbishops = bb.byPiece[pieceIndex('B')];
    while (wbishops) {
      Square sq = popLsb(wbishops);
      Bitboard colorMask = (squareBB(sq) & DARK_SQUARES) ? DARK_SQUARES
                                                         : LIGHT_SQUARES;
      int blocked = popcount(whitePawns & colorMask);
      mgCoeff += blocked;
      egCoeff += blocked;
    }
    Bitboard bbishops = bb.byPiece[pieceIndex('b')];
    while (bbishops) {
      Square sq = popLsb(bbishops);
      Bitboard colorMask = (squareBB(sq) & DARK_SQUARES) ? DARK_SQUARES
                                                         : LIGHT_SQUARES;
      int blocked = popcount(blackPawns & colorMask);
      mgCoeff -= blocked;
      egCoeff -= blocked;
    }
    t.add(pIdx(&BAD_BISHOP_MG), mgCoeff * mgW);
    t.add(pIdx(&BAD_BISHOP_EG), egCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Rook on file (separate MG/EG).
  // -----------------------------------------------------------------------
  {
    Bitboard allPawns = whitePawns | blackPawns;
    float openCoeff = 0.0f, semiCoeff = 0.0f;

    Bitboard wr = bb.byPiece[pieceIndex('R')];
    while (wr) {
      Square sq = popLsb(wr);
      Bitboard file = fileBB(fileOf(sq));
      if (!(file & allPawns))        openCoeff += 1.0f;
      else if (!(file & whitePawns)) semiCoeff += 1.0f;
    }
    Bitboard br = bb.byPiece[pieceIndex('r')];
    while (br) {
      Square sq = popLsb(br);
      Bitboard file = fileBB(fileOf(sq));
      if (!(file & allPawns))        openCoeff -= 1.0f;
      else if (!(file & blackPawns)) semiCoeff -= 1.0f;
    }
    t.add(pIdx(&ROOK_OPEN_FILE_MG), openCoeff * mgW);
    t.add(pIdx(&ROOK_OPEN_FILE_EG), openCoeff * egW);
    t.add(pIdx(&ROOK_SEMI_OPEN_FILE_MG), semiCoeff * mgW);
    t.add(pIdx(&ROOK_SEMI_OPEN_FILE_EG), semiCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Rook on 7th (separate MG/EG).
  // -----------------------------------------------------------------------
  {
    float coeff = 0.0f;
    Bitboard whiteR7 = bb.byPiece[pieceIndex('R')] & rankBB(6);
    if (whiteR7 && ((bb.byPiece[pieceIndex('k')] & rankBB(7))
               || (bb.byPiece[pieceIndex('p')] & rankBB(6))))
      coeff += popcount(whiteR7);
    Bitboard blackR2 = bb.byPiece[pieceIndex('r')] & rankBB(1);
    if (blackR2 && ((bb.byPiece[pieceIndex('K')] & rankBB(0))
               || (bb.byPiece[pieceIndex('P')] & rankBB(1))))
      coeff -= popcount(blackR2);
    t.add(pIdx(&ROOK_7TH_MG), coeff * mgW);
    t.add(pIdx(&ROOK_7TH_EG), coeff * egW);
  }

  // -----------------------------------------------------------------------
  // Rook behind passer (EG only).
  // -----------------------------------------------------------------------
  {
    float ownCoeff = 0.0f, enemyCoeff = 0.0f;
    Bitboard whiteRooks = bb.byPiece[pieceIndex('R')];
    Bitboard blackRooks = bb.byPiece[pieceIndex('r')];

    Bitboard wpR = whitePawns;
    while (wpR) {
      Square sq = popLsb(wpR);
      if (!isPassed(sq, Color::WHITE, blackPawns)) continue;
      Bitboard fileMask = FILE_A << (sq & 7);
      Bitboard ownR = whiteRooks & fileMask;
      while (ownR) {
        Square rsq = popLsb(ownR);
        if (rsq < sq) ownCoeff += 1.0f;
      }
      Bitboard enR = blackRooks & fileMask;
      while (enR) {
        Square rsq = popLsb(enR);
        if (rsq < sq) enemyCoeff += 1.0f;
      }
    }
    Bitboard bpR = blackPawns;
    while (bpR) {
      Square sq = popLsb(bpR);
      if (!isPassed(sq, Color::BLACK, whitePawns)) continue;
      Bitboard fileMask = FILE_A << (sq & 7);
      Bitboard ownR = blackRooks & fileMask;
      while (ownR) {
        Square rsq = popLsb(ownR);
        if (rsq > sq) ownCoeff -= 1.0f;
      }
      Bitboard enR = whiteRooks & fileMask;
      while (enR) {
        Square rsq = popLsb(enR);
        if (rsq > sq) enemyCoeff -= 1.0f;
      }
    }
    t.add(pIdx(&ROOK_BEHIND_OWN_PASSER_EG), ownCoeff * egW);
    t.add(pIdx(&ROOK_BEHIND_ENEMY_PASSER_EG), enemyCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Knight outposts (same value for MG + EG).
  // -----------------------------------------------------------------------
  {
    constexpr Square SQ_D4 = 27, SQ_D5 = 35, SQ_E4 = 28, SQ_E5 = 36;
    float coeff = 0.0f;

    Bitboard wn = bb.byPiece[pieceIndex('N')];
    while (wn) {
      Square sq = popLsb(wn);
      if (!(squareBB(sq) & whitePawnAttacks)) continue;
      int file = fileOf(sq);
      Bitboard adjFiles = 0;
      if (file > 0) adjFiles |= fileBB(file - 1);
      if (file < 7) adjFiles |= fileBB(file + 1);
      int rank = rankOf(sq);
      Bitboard aboveMask =
          ~((static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1);
      if (blackPawns & adjFiles & aboveMask) continue;
      int bonus = 1;
      if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5) bonus = 2;
      coeff += bonus;
    }

    Bitboard bn = bb.byPiece[pieceIndex('n')];
    while (bn) {
      Square sq = popLsb(bn);
      if (!(squareBB(sq) & blackPawnAttacks)) continue;
      int file = fileOf(sq);
      Bitboard adjFiles = 0;
      if (file > 0) adjFiles |= fileBB(file - 1);
      if (file < 7) adjFiles |= fileBB(file + 1);
      int rank = rankOf(sq);
      Bitboard belowMask = (rank > 0)
          ? (static_cast<Bitboard>(1) << (8 * rank)) - 1
          : 0;
      if (whitePawns & adjFiles & belowMask) continue;
      int bonus = 1;
      if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5) bonus = 2;
      coeff -= bonus;
    }
    t.add(pIdx(&OUTPOST_BONUS_MG), coeff * mgW);
    t.add(pIdx(&OUTPOST_BONUS_EG), coeff * egW);
  }

  // -----------------------------------------------------------------------
  // King safety / pawn shield (MG only).
  // -----------------------------------------------------------------------
  {
    float missingCoeff = 0.0f, rank3Coeff = 0.0f, rank4PlusCoeff = 0.0f,
          openCoeff = 0.0f;
    Bitboard allPawns = whitePawns | blackPawns;

    auto shieldSide = [&](int color, int sign) {
      Bitboard kingBB_s = bb.byPiece[pieceIndex(static_cast<Color>(color), PieceType::KING)];
      if (!kingBB_s) return;
      Square kingSq = lsb(kingBB_s);
      int kingFile = fileOf(kingSq);

      if (kingFile >= 3 && kingFile <= 4) return;

      int shieldFiles[3];
      if (kingFile <= 2) {
        shieldFiles[0] = 0; shieldFiles[1] = 1; shieldFiles[2] = 2;
      } else {
        shieldFiles[0] = 5; shieldFiles[1] = 6; shieldFiles[2] = 7;
      }

      Bitboard friendlyPawns = bb.byPiece[pieceIndex(static_cast<Color>(color), PieceType::PAWN)];
      for (int i = 0; i < 3; ++i) {
        int f = shieldFiles[i];
        Bitboard fileMask = fileBB(f);
        Bitboard shieldPawns = friendlyPawns & fileMask;

        if (!shieldPawns) {
          missingCoeff += sign;
        } else {
          Bitboard copy = shieldPawns;
          while (copy) {
            Square psq = popLsb(copy);
            int pRank = rankOf(psq);
            if (color == 0) {
              if (pRank == 2)       rank3Coeff += sign;
              else if (pRank >= 3)  rank4PlusCoeff += sign;
            } else {
              if (pRank == 5)       rank3Coeff += sign;
              else if (pRank <= 4)  rank4PlusCoeff += sign;
            }
          }
        }
        if (f == kingFile && !(allPawns & fileMask))
          openCoeff += sign;
      }
    };

    shieldSide(0, 1);
    shieldSide(1, -1);

    t.add(pIdx(&SHIELD_MISSING_PAWN), missingCoeff * mgW);
    t.add(pIdx(&SHIELD_ADV_RANK3), rank3Coeff * mgW);
    t.add(pIdx(&SHIELD_ADV_RANK4PLUS), rank4PlusCoeff * mgW);
    t.add(pIdx(&SHIELD_OPEN_FILE), openCoeff * mgW);
  }

  // -----------------------------------------------------------------------
  // Space (separate MG/EG).
  // -----------------------------------------------------------------------
  {
    Bitboard bAtk = shiftSE(blackPawns) | shiftSW(blackPawns);
    Bitboard wAtk = shiftNE(whitePawns) | shiftNW(whitePawns);
    int ws = popcount(WHITE_SPACE_ZONE & ~bAtk);
    int bs = popcount(BLACK_SPACE_ZONE & ~wAtk);
    float diff = static_cast<float>(ws - bs);
    t.add(pIdx(&SPACE_BONUS_MG), diff * mgW);
  }

  // -----------------------------------------------------------------------
  // Trapped pieces (MG only).
  // -----------------------------------------------------------------------
  {
    constexpr Square A7 = 48, B6 = 41, B8 = 57;
    constexpr Square H7 = 55, G6 = 46, G8 = 62;
    constexpr Square A2 = 8,  B3 = 17, B1 = 1;
    constexpr Square H2 = 15, G3 = 22, G1_sq = 6;

    float bishCoeff = 0.0f;
    Bitboard wB = bb.byPiece[pieceIndex('B')],
           bB = bb.byPiece[pieceIndex('b')];
    if ((wB & squareBB(A7)) && (blackPawns & squareBB(B6))) bishCoeff += 1;
    if ((wB & squareBB(B8)) && (blackPawns & squareBB(B6))) bishCoeff += 1;
    if ((wB & squareBB(H7)) && (blackPawns & squareBB(G6))) bishCoeff += 1;
    if ((wB & squareBB(G8)) && (blackPawns & squareBB(G6))) bishCoeff += 1;
    if ((bB & squareBB(A2)) && (whitePawns & squareBB(B3))) bishCoeff -= 1;
    if ((bB & squareBB(B1)) && (whitePawns & squareBB(B3))) bishCoeff -= 1;
    if ((bB & squareBB(H2)) && (whitePawns & squareBB(G3))) bishCoeff -= 1;
    if ((bB & squareBB(G1_sq)) && (whitePawns & squareBB(G3))) bishCoeff -= 1;
    t.add(pIdx(&TRAPPED_BISHOP_PENALTY), bishCoeff * mgW);

    constexpr Square A1 = 0, H1 = 7, F1 = 5, C1 = 2;
    constexpr Square A8 = 56, H8 = 63, F8 = 61, C8 = 58;
    constexpr Square B1_r = 1,  G1_r = 6;
    constexpr Square B8_r = 57, G8_r = 62;

    float rookCoeff = 0.0f;
    Bitboard wR = bb.byPiece[pieceIndex('R')],
           bR = bb.byPiece[pieceIndex('r')];
    Bitboard wK = bb.byPiece[pieceIndex('K')],
           bK = bb.byPiece[pieceIndex('k')];
    if ((wR & squareBB(H1)) && (wK & (squareBB(F1) | squareBB(G1_r))))
      rookCoeff += 1;
    if ((wR & squareBB(A1)) && (wK & (squareBB(B1_r) | squareBB(C1))))
      rookCoeff += 1;
    if ((bR & squareBB(H8)) && (bK & (squareBB(F8) | squareBB(G8_r))))
      rookCoeff -= 1;
    if ((bR & squareBB(A8)) && (bK & (squareBB(B8_r) | squareBB(C8))))
      rookCoeff -= 1;
    t.add(pIdx(&TRAPPED_ROOK_PENALTY), rookCoeff * mgW);
  }

  // -----------------------------------------------------------------------
  // Mobility — nonlinear tables, safe squares (exclude enemy pawn attacks).
  // -----------------------------------------------------------------------
  attacks::AttackInfo info = attacks::computeAll(bb);

  {
    for (int c = 0; c < 2; ++c) {
      float sign = (c == 0) ? 1.0f : -1.0f;
      Bitboard friendly    = bb.byColor[c];
      Bitboard enemyPawnAtk = info.byPiece[1 - c][1];  // PieceType::PAWN = 1
      Bitboard safeMask    = ~friendly & ~enemyPawnAtk;

      int nMob = popcount(info.byPiece[c][2] & safeMask);
      int bMob = popcount(info.byPiece[c][3] & safeMask);
      int rMob = popcount(info.byPiece[c][4] & safeMask);
      int qMob = popcount(info.byPiece[c][5] & safeMask);

      t.add(pIdx(&MOBILITY_KNIGHT_MG[nMob]), sign * mgW);
      t.add(pIdx(&MOBILITY_KNIGHT_EG[nMob]), sign * egW);
      t.add(pIdx(&MOBILITY_BISHOP_MG[bMob]), sign * mgW);
      t.add(pIdx(&MOBILITY_BISHOP_EG[bMob]), sign * egW);
      t.add(pIdx(&MOBILITY_ROOK_MG[rMob]),   sign * mgW);
      t.add(pIdx(&MOBILITY_ROOK_EG[rMob]),   sign * egW);
      t.add(pIdx(&MOBILITY_QUEEN_MG[qMob]),  sign * mgW);
      t.add(pIdx(&MOBILITY_QUEEN_EG[qMob]),  sign * egW);
    }
  }

  // -----------------------------------------------------------------------
  // King danger table (MG only).
  // -----------------------------------------------------------------------
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? -1 : 1;
    int enemy = 1 - c;

    Bitboard kingBB_kd = bb.byPiece[pieceIndex(static_cast<Color>(c), PieceType::KING)];
    if (!kingBB_kd) continue;
    Square kingSq = lsb(kingBB_kd);
    Bitboard kingZone = attacks::KING[kingSq] | squareBB(kingSq);

    int totalWeight = 0;
    for (int pt = 0; pt < 4; ++pt) {
      int pieceType = pt + 2;
      if (info.byPiece[enemy][pieceType] & kingZone)
        totalWeight += KING_DANGER_WEIGHT[pt];
    }
    if (totalWeight > 0) {
      for (int pt = 0; pt < 4; ++pt) {
        // pt 0..3 → KNIGHT..QUEEN (PieceType 2..5)
        int pieceIdx = pieceIndex(static_cast<Color>(enemy), static_cast<PieceType>(pt + 2));
        Bitboard pieces = bb.byPiece[pieceIdx];
        while (pieces) {
          Square sq = popLsb(pieces);
          if (chebyshevDist(sq, kingSq) <= 3) ++totalWeight;
        }
      }
    }

    int idx = (totalWeight < KING_DANGER_TABLE_SIZE)
            ? totalWeight
            : KING_DANGER_TABLE_SIZE - 1;
    t.add(pIdx(&KING_DANGER_TABLE[idx]), static_cast<float>(sign) * mgW);
  }

  return t;
}

// ===========================================================================
// Parameter descriptor getters — metadata for the tuning registry.
//
// Three descriptor types: scalar (individual params), mobility (nonlinear
// table pairs), and PST (piece-square tables).  Each getter returns a
// static array of descriptors with tuning bounds.
// ===========================================================================

// clang-format off
const tuning::ScalarParam* tuning::scalarParams(int& count) {
  static const ScalarParam params[] = {
    // --- Material MG (5) ---
    {"MAT_PAWN_MG",             &MATERIAL[0],               80,  130,  5},
    {"MAT_KNIGHT_MG",           &MATERIAL[1],              250,  400, 10},
    {"MAT_BISHOP_MG",           &MATERIAL[2],              250,  420, 10},
    {"MAT_ROOK_MG",             &MATERIAL[3],              400,  610, 10},
    {"MAT_QUEEN_MG",            &MATERIAL[4],              800, 1250, 20},
    // --- Material EG (5) ---
    {"MAT_PAWN_EG",             &MATERIAL_EG[0],            80,  150,  5},
    {"MAT_KNIGHT_EG",           &MATERIAL_EG[1],           230,  400, 10},
    {"MAT_BISHOP_EG",           &MATERIAL_EG[2],           250,  420, 10},
    {"MAT_ROOK_EG",             &MATERIAL_EG[3],           430,  650, 10},
    {"MAT_QUEEN_EG",            &MATERIAL_EG[4],           850, 1300, 20},
    // --- Passed pawn rank bonus (12) ---
    {"PASSED_R2_MG",            &PASSED_RANK_BONUS_MG[1],    0,   30,  5},
    {"PASSED_R3_MG",            &PASSED_RANK_BONUS_MG[2],    0,   40,  5},
    {"PASSED_R4_MG",            &PASSED_RANK_BONUS_MG[3],    0,   50,  5},
    {"PASSED_R5_MG",            &PASSED_RANK_BONUS_MG[4],    5,  150, 10},
    {"PASSED_R6_MG",            &PASSED_RANK_BONUS_MG[5],   10,  180, 10},
    {"PASSED_R7_MG",            &PASSED_RANK_BONUS_MG[6],   20,  300, 15},
    {"PASSED_R2_EG",            &PASSED_RANK_BONUS_EG[1],    0,   30,  5},
    {"PASSED_R3_EG",            &PASSED_RANK_BONUS_EG[2],    0,   50,  5},
    {"PASSED_R4_EG",            &PASSED_RANK_BONUS_EG[3],    0,   80, 10},
    {"PASSED_R5_EG",            &PASSED_RANK_BONUS_EG[4],   10,  150, 10},
    {"PASSED_R6_EG",            &PASSED_RANK_BONUS_EG[5],   20,  300, 15},
    {"PASSED_R7_EG",            &PASSED_RANK_BONUS_EG[6],   80,  500, 20},
    // --- Pawn structure scalars (9) ---
    {"CONNECTED_PASSED_MG",     &CONNECTED_PASSED_MG,        0,   40,  5},
    {"CONNECTED_PASSED_EG",     &CONNECTED_PASSED_EG,        0,   60,  5},
    {"ISOLATED_PENALTY_MG",     &ISOLATED_PENALTY_MG,      -30,    0,  5},
    {"ISOLATED_PENALTY_EG",     &ISOLATED_PENALTY_EG,      -40,    0,  5},
    {"DOUBLED_PENALTY_MG",      &DOUBLED_PENALTY_MG,       -40,    0,  5},
    {"DOUBLED_PENALTY_EG",      &DOUBLED_PENALTY_EG,       -50,    0,  5},
    {"BACKWARD_PENALTY_MG",     &BACKWARD_PENALTY_MG,      -35,    0,  5},
    {"BACKWARD_PENALTY_EG",     &BACKWARD_PENALTY_EG,      -35,    0,  5},
    {"PROTECTED_PASSER_MG",     &PROTECTED_PASSER_MG,        0,   40,  5},
    // --- Bishop pair (2) ---
    {"BISHOP_PAIR_MG",          &BISHOP_PAIR_MG,              0,  100,  5},
    {"BISHOP_PAIR_EG",          &BISHOP_PAIR_EG,             10,  150,  5},
    // --- Rook on file (4) ---
    {"ROOK_OPEN_FILE_MG",       &ROOK_OPEN_FILE_MG,          0,   50,  5},
    {"ROOK_OPEN_FILE_EG",       &ROOK_OPEN_FILE_EG,          0,   50,  5},
    {"ROOK_SEMI_OPEN_FILE_MG",  &ROOK_SEMI_OPEN_FILE_MG,     0,   40,  5},
    {"ROOK_SEMI_OPEN_FILE_EG",  &ROOK_SEMI_OPEN_FILE_EG,     0,   40,  5},
    // --- Rook on 7th (2) ---
    {"ROOK_7TH_MG",             &ROOK_7TH_MG,                5,   50,  5},
    {"ROOK_7TH_EG",             &ROOK_7TH_EG,                0,   80,  5},
    // --- Rook behind passer (2) ---
    {"ROOK_BEHIND_OWN_EG",      &ROOK_BEHIND_OWN_PASSER_EG,    0,   50,  5},
    {"ROOK_BEHIND_ENEMY_EG",    &ROOK_BEHIND_ENEMY_PASSER_EG, -50,   10,  5},
    // --- Outpost (2) ---
    {"OUTPOST_BONUS_MG",        &OUTPOST_BONUS_MG,            0,   60,  5},
    {"OUTPOST_BONUS_EG",        &OUTPOST_BONUS_EG,            0,   40,  5},
    // --- Bad bishop (2) ---
    {"BAD_BISHOP_MG",           &BAD_BISHOP_MG,             -15,    0,  1},
    {"BAD_BISHOP_EG",           &BAD_BISHOP_EG,             -15,    0,  1},
    // --- Trapped pieces (2) ---
    {"TRAPPED_BISHOP",          &TRAPPED_BISHOP_PENALTY,   -120,    0, 10},
    {"TRAPPED_ROOK",            &TRAPPED_ROOK_PENALTY,     -100,    0, 10},
    // --- King safety shield (4) ---
    {"SHIELD_MISSING_PAWN",     &SHIELD_MISSING_PAWN,       -60,    0,  5},
    {"SHIELD_ADV_RANK3",        &SHIELD_ADV_RANK3,          -30,    0,  5},
    {"SHIELD_ADV_RANK4PLUS",    &SHIELD_ADV_RANK4PLUS,      -30,    0,  5},
    {"SHIELD_OPEN_FILE",        &SHIELD_OPEN_FILE,          -50,    0,  5},
    // --- King danger table (12) ---
    {"KD_TABLE_1",              &KING_DANGER_TABLE[1],        0,   20,  5},
    {"KD_TABLE_2",              &KING_DANGER_TABLE[2],        0,   30,  5},
    {"KD_TABLE_3",              &KING_DANGER_TABLE[3],        0,   50,  5},
    {"KD_TABLE_4",              &KING_DANGER_TABLE[4],        0,   80, 10},
    {"KD_TABLE_5",              &KING_DANGER_TABLE[5],       10,  130, 10},
    {"KD_TABLE_6",              &KING_DANGER_TABLE[6],       20,  200, 10},
    {"KD_TABLE_7",              &KING_DANGER_TABLE[7],       40,  280, 15},
    {"KD_TABLE_8",              &KING_DANGER_TABLE[8],       60,  360, 15},
    {"KD_TABLE_9",              &KING_DANGER_TABLE[9],       80,  440, 20},
    {"KD_TABLE_10",             &KING_DANGER_TABLE[10],     100,  500, 20},
    {"KD_TABLE_11",             &KING_DANGER_TABLE[11],     120,  600, 25},
    {"KD_TABLE_12",             &KING_DANGER_TABLE[12],     150,  700, 25},
    // --- Space (1) ---
    {"SPACE_BONUS_MG",          &SPACE_BONUS_MG,              0,   10,  1},
    // --- Passed pawn king distance (2) ---
    {"PASSER_OWN_KING",         &PASSER_OWN_KING,             0,   15,  1},
    {"PASSER_ENEMY_KING",       &PASSER_ENEMY_KING,           0,   25,  2},
  };
  // clang-format on
  count = sizeof(params) / sizeof(params[0]);
  return params;
}

const tuning::MobilityTableDef* tuning::mobilityDefs(int& count) {
  static const MobilityTableDef defs[] = {
    {"MOB_KNIGHT", MOBILITY_KNIGHT_MG, MOBILITY_KNIGHT_EG, MOBILITY_KNIGHT_SIZE, -80, 120, 2},
    {"MOB_BISHOP", MOBILITY_BISHOP_MG, MOBILITY_BISHOP_EG, MOBILITY_BISHOP_SIZE, -80, 120, 2},
    {"MOB_ROOK",   MOBILITY_ROOK_MG,   MOBILITY_ROOK_EG,   MOBILITY_ROOK_SIZE,   -80, 120, 2},
    {"MOB_QUEEN",  MOBILITY_QUEEN_MG,  MOBILITY_QUEEN_EG,  MOBILITY_QUEEN_SIZE,  -80, 120, 2},
  };
  count = sizeof(defs) / sizeof(defs[0]);
  return defs;
}

// clang-format off
const tuning::PstDef* tuning::pstDefs(int& count) {
  static const PstDef defs[] = {
    {"PST_PAWN_MG",   PST_PAWN_MG,   true,  -100, 100, 5},
    {"PST_KNIGHT_MG", PST_KNIGHT_MG, false, -100, 100, 5},
    {"PST_BISHOP_MG", PST_BISHOP_MG, false, -100, 100, 5},
    {"PST_ROOK_MG",   PST_ROOK_MG,   false, -100, 100, 5},
    {"PST_QUEEN_MG",  PST_QUEEN_MG,  false, -100, 100, 5},
    {"PST_KING_MG",   PST_KING_MG,   false, -100, 100, 5},
    {"PST_PAWN_EG",   PST_PAWN_EG,   true,  -100, 100, 5},
    {"PST_KNIGHT_EG", PST_KNIGHT_EG, false, -100, 100, 5},
    {"PST_BISHOP_EG", PST_BISHOP_EG, false, -100, 100, 5},
    {"PST_ROOK_EG",   PST_ROOK_EG,   false, -100, 100, 5},
    {"PST_QUEEN_EG",  PST_QUEEN_EG,  false, -100, 100, 5},
    {"PST_KING_EG",   PST_KING_EG,   false, -100, 100, 5},
  };
  // clang-format on
  count = sizeof(defs) / sizeof(defs[0]);
  return defs;
}

// ===========================================================================
// buildRegistry — constructs the full parameter list on first access.
//
// Iterates the descriptor getters to populate the registry.  All parameter
// names, pointers, and tuning bounds are owned by the descriptors above —
// buildRegistry never references individual eval param variables directly.
//
// Total: 77 scalar + 124 mobility + 752 PST = 953 tunable parameters.
// ===========================================================================

static std::vector<TuneEntry>& buildRegistry() {
  static std::vector<TuneEntry> reg;
  if (!reg.empty()) return reg;

  // ---- Scalar entries (77) ------------------------------------------------
  int nScalar;
  const auto* scalars = tuning::scalarParams(nScalar);
  for (int i = 0; i < nScalar; ++i) {
    const auto& s = scalars[i];
    reg.push_back({s.name, s.ptr, *s.ptr, s.min, s.max, s.step});
  }

  // ---- Mobility tables (124) ----------------------------------------------
  // Entry [0] of each table is fixed at 0 (zero mobility = zero bonus).
  // Remaining entries [1..size-1] are tunable.
  int nMob;
  const auto* mobs = tuning::mobilityDefs(nMob);
  for (int m = 0; m < nMob; ++m) {
    const auto& md = mobs[m];
    for (int i = 1; i < md.size; ++i) {
      char mgName[32], egName[32];
      std::snprintf(mgName, sizeof(mgName), "%s_MG_%d", md.prefix, i);
      std::snprintf(egName, sizeof(egName), "%s_EG_%d", md.prefix, i);
      char* mgN = new char[std::strlen(mgName) + 1]; std::strcpy(mgN, mgName);
      char* egN = new char[std::strlen(egName) + 1]; std::strcpy(egN, egName);
      reg.push_back({mgN, &md.mgData[i], md.mgData[i], md.min, md.max, md.step});
      reg.push_back({egN, &md.egData[i], md.egData[i], md.min, md.max, md.step});
    }
  }

  // ---- PST entries (752) --------------------------------------------------
  // 12 arrays × 64 squares = 768, minus 16 frozen pawn squares (rank 1 + 8).
  int nPst;
  const auto* psts = tuning::pstDefs(nPst);
  for (int p = 0; p < nPst; ++p) {
    const auto& pd = psts[p];
    for (int sq = 0; sq < 64; ++sq) {
      if (pd.isPawn && (sq < 8 || sq >= 56)) continue;
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%s_%d", pd.prefix, sq);
      char* name = new char[std::strlen(buf) + 1];
      std::strcpy(name, buf);
      reg.push_back({name, &pd.data[sq], pd.data[sq], pd.min, pd.max, pd.step});
    }
  }

  return reg;
}

// ---------------------------------------------------------------------------
// tuning:: accessor functions — thin wrappers around the registry.
// ---------------------------------------------------------------------------

namespace tuning {

int paramCount()              { return static_cast<int>(buildRegistry().size()); }
const char* getName(int i)    { return buildRegistry()[i].name; }
int getValue(int i)           { return *buildRegistry()[i].ptr; }
void setValue(int i, int v)   { *buildRegistry()[i].ptr = v; }
int getDefault(int i)         { return buildRegistry()[i].defaultVal; }
int getMin(int i)             { return buildRegistry()[i].min; }
int getMax(int i)             { return buildRegistry()[i].max; }
int getStep(int i)            { return buildRegistry()[i].step; }

}  // namespace tuning

}  // namespace eval
}  // namespace LibreChess

#endif  // TUNING
