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
// Name → index map (built once from the tuning registry)
// ===========================================================================

static std::unordered_map<std::string, int> paramMap;

static void initTraceIndices();  // defined after TraceIndices struct

void buildParamMap() {
  int n = tuning::paramCount();
  paramMap.reserve(n);
  for (int i = 0; i < n; ++i) {
    paramMap[tuning::getName(i)] = i;
  }
  initTraceIndices();
}

int findParam(const char* name) {
  auto it = paramMap.find(name);
  return (it != paramMap.end()) ? it->second : -1;
}

// ===========================================================================
// TraceIndices — all parameter-index lookups cached in one struct.
//
// Populated once by initTraceIndices() (called from buildParamMap()).
// Replaces the previous pattern of scattered static-bool/static-int
// lazy-init blocks inside extractTrace().
// ===========================================================================

struct TraceIndices {
  // Material — MG (5, includes pawn MG) + EG (5, includes pawn EG)
  int matMg[5];        // P, N, B, R, Q MG (all tunable)
  int matEg[5];        // P, N, B, R, Q EG (all tunable)

  // PST (12 tables × 64 squares)
  int pst[12][64];

  // Passed pawn rank bonuses (ranks 0-7, only 1-6 used)
  int passedMg[8], passedEg[8];

  // Pawn structure scalars (separate MG/EG)
  int connectedMg, connectedEg;
  int isolatedMg, isolatedEg;
  int doubledMg, doubledEg;
  int backwardMg, backwardEg;
  int protPassMg;

  // Passed pawn king distance
  int passerOwn, passerEnemy;

  // Bishop pair
  int bpMg, bpEg;

  // Bad bishop
  int badBishMg, badBishEg;

  // Rook on file (separate MG/EG)
  int rookOpenMg, rookOpenEg;
  int rookSemiMg, rookSemiEg;

  // Rook on 7th
  int r7Mg, r7Eg;

  // Rook behind passer
  int rbpOwn, rbpEnemy;

  // Outpost (separate MG/EG)
  int outpostMg, outpostEg;

  // King safety shield
  int shieldMissing, shieldAdvRank3, shieldAdvRank4Plus, shieldOpen;

  // Space (MG only — EG component removed, CPW: midgame concept)
  int spaceMg;

  // Trapped pieces
  int trappedBish, trappedRook;

  // Mobility (per piece type, MG/EG)
  int mobNMg, mobNEg, mobBMg, mobBEg;
  int mobRMg, mobREg, mobQMg, mobQEg;

  // Threats (MG only — EG components removed, CPW: midgame concept)
  int thPMinMg, thPRkMg;
  int thPQnMg, thNRkMg;
  int thNQnMg, thRQnMg;

  // King danger table
  int kdTable[KING_DANGER_TABLE_SIZE];
};

static TraceIndices TI;

// ---------------------------------------------------------------------------
// initTraceIndices — populate all index fields from the parameter map.
// Called once from buildParamMap() after the map is built.
// ---------------------------------------------------------------------------

static void initTraceIndices() {
  // Material — MG (5 including pawn) + EG (5 including pawn)
  TI.matMg[0] = findParam("MAT_PAWN_MG");
  TI.matMg[1] = findParam("MAT_KNIGHT_MG");
  TI.matMg[2] = findParam("MAT_BISHOP_MG");
  TI.matMg[3] = findParam("MAT_ROOK_MG");
  TI.matMg[4] = findParam("MAT_QUEEN_MG");
  TI.matEg[0] = findParam("MAT_PAWN_EG");
  TI.matEg[1] = findParam("MAT_KNIGHT_EG");
  TI.matEg[2] = findParam("MAT_BISHOP_EG");
  TI.matEg[3] = findParam("MAT_ROOK_EG");
  TI.matEg[4] = findParam("MAT_QUEEN_EG");

  // PST
  static const char* pstPrefixes[12] = {
    "PST_PAWN_MG", "PST_KNIGHT_MG", "PST_BISHOP_MG",
    "PST_ROOK_MG", "PST_QUEEN_MG",  "PST_KING_MG",
    "PST_PAWN_EG", "PST_KNIGHT_EG", "PST_BISHOP_EG",
    "PST_ROOK_EG", "PST_QUEEN_EG",  "PST_KING_EG",
  };
  for (int tbl = 0; tbl < 12; ++tbl) {
    for (int sq = 0; sq < 64; ++sq) {
      char name[32];
      std::snprintf(name, sizeof(name), "%s_%d", pstPrefixes[tbl], sq);
      TI.pst[tbl][sq] = findParam(name);
    }
  }

  // Passed pawn rank bonuses
  const char* passedMgNames[8] = {nullptr, "PASSED_R2_MG", "PASSED_R3_MG",
      "PASSED_R4_MG", "PASSED_R5_MG", "PASSED_R6_MG", "PASSED_R7_MG", nullptr};
  const char* passedEgNames[8] = {nullptr, "PASSED_R2_EG", "PASSED_R3_EG",
      "PASSED_R4_EG", "PASSED_R5_EG", "PASSED_R6_EG", "PASSED_R7_EG", nullptr};
  for (int r = 0; r < 8; ++r) {
    TI.passedMg[r] = passedMgNames[r] ? findParam(passedMgNames[r]) : -1;
    TI.passedEg[r] = passedEgNames[r] ? findParam(passedEgNames[r]) : -1;
  }

  // Pawn structure scalars (MG/EG split)
  TI.connectedMg = findParam("CONNECTED_PASSED_MG");
  TI.connectedEg = findParam("CONNECTED_PASSED_EG");
  TI.isolatedMg  = findParam("ISOLATED_PENALTY_MG");
  TI.isolatedEg  = findParam("ISOLATED_PENALTY_EG");
  TI.doubledMg   = findParam("DOUBLED_PENALTY_MG");
  TI.doubledEg   = findParam("DOUBLED_PENALTY_EG");
  TI.backwardMg  = findParam("BACKWARD_PENALTY_MG");
  TI.backwardEg  = findParam("BACKWARD_PENALTY_EG");
  TI.protPassMg  = findParam("PROTECTED_PASSER_MG");

  // Passed pawn king distance
  TI.passerOwn   = findParam("PASSER_OWN_KING");
  TI.passerEnemy = findParam("PASSER_ENEMY_KING");

  // Bishop pair
  TI.bpMg = findParam("BISHOP_PAIR_MG");
  TI.bpEg = findParam("BISHOP_PAIR_EG");

  // Bad bishop
  TI.badBishMg = findParam("BAD_BISHOP_MG");
  TI.badBishEg = findParam("BAD_BISHOP_EG");

  // Rook on file (MG/EG split)
  TI.rookOpenMg = findParam("ROOK_OPEN_FILE_MG");
  TI.rookOpenEg = findParam("ROOK_OPEN_FILE_EG");
  TI.rookSemiMg = findParam("ROOK_SEMI_OPEN_FILE_MG");
  TI.rookSemiEg = findParam("ROOK_SEMI_OPEN_FILE_EG");

  // Rook on 7th
  TI.r7Mg = findParam("ROOK_7TH_MG");
  TI.r7Eg = findParam("ROOK_7TH_EG");

  // Rook behind passer
  TI.rbpOwn   = findParam("ROOK_BEHIND_OWN_EG");
  TI.rbpEnemy = findParam("ROOK_BEHIND_ENEMY_EG");

  // Outpost (MG/EG split)
  TI.outpostMg = findParam("OUTPOST_BONUS_MG");
  TI.outpostEg = findParam("OUTPOST_BONUS_EG");

  // King safety shield
  TI.shieldMissing     = findParam("SHIELD_MISSING_PAWN");
  TI.shieldAdvRank3    = findParam("SHIELD_ADV_RANK3");
  TI.shieldAdvRank4Plus = findParam("SHIELD_ADV_RANK4PLUS");
  TI.shieldOpen        = findParam("SHIELD_OPEN_FILE");

  // Space
  TI.spaceMg = findParam("SPACE_BONUS_MG");

  // Trapped pieces
  TI.trappedBish = findParam("TRAPPED_BISHOP");
  TI.trappedRook = findParam("TRAPPED_ROOK");

  // Mobility
  TI.mobNMg = findParam("MOBILITY_KNIGHT_MG");
  TI.mobNEg = findParam("MOBILITY_KNIGHT_EG");
  TI.mobBMg = findParam("MOBILITY_BISHOP_MG");
  TI.mobBEg = findParam("MOBILITY_BISHOP_EG");
  TI.mobRMg = findParam("MOBILITY_ROOK_MG");
  TI.mobREg = findParam("MOBILITY_ROOK_EG");
  TI.mobQMg = findParam("MOBILITY_QUEEN_MG");
  TI.mobQEg = findParam("MOBILITY_QUEEN_EG");

  // Threats
  TI.thPMinMg = findParam("THREAT_P_MINOR_MG");
  TI.thPRkMg  = findParam("THREAT_P_ROOK_MG");
  TI.thPQnMg  = findParam("THREAT_P_QUEEN_MG");
  TI.thNRkMg  = findParam("THREAT_N_ROOK_MG");
  TI.thNQnMg  = findParam("THREAT_N_QUEEN_MG");
  TI.thRQnMg  = findParam("THREAT_R_QUEEN_MG");

  // King danger table
  TI.kdTable[0] = -1;  // TABLE[0] = 0, fixed.
  const char* kdNames[KING_DANGER_TABLE_SIZE] = {
    nullptr, "KD_TABLE_1",  "KD_TABLE_2",  "KD_TABLE_3",
    "KD_TABLE_4",  "KD_TABLE_5",  "KD_TABLE_6",
    "KD_TABLE_7",  "KD_TABLE_8",  "KD_TABLE_9",
    "KD_TABLE_10", "KD_TABLE_11", "KD_TABLE_12"
  };
  for (int i = 1; i < KING_DANGER_TABLE_SIZE; ++i)
    TI.kdTable[i] = findParam(kdNames[i]);
}

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
    if (TI.matMg[i] >= 0) t.add(TI.matMg[i], diff * mgW);
    if (TI.matEg[i] >= 0) t.add(TI.matEg[i], diff * egW);
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
      if (TI.pst[mgTable][sq] >= 0) t.add(TI.pst[mgTable][sq], mgW);
      if (TI.pst[egTable][sq] >= 0) t.add(TI.pst[egTable][sq], egW);
    }

    Bitboard black = bb.byPiece[pieceType + 6];
    while (black) {
      Square sq = popLsb(black);
      int mirSq = sq ^ 56;
      if (TI.pst[mgTable][mirSq] >= 0) t.add(TI.pst[mgTable][mirSq], -mgW);
      if (TI.pst[egTable][mirSq] >= 0) t.add(TI.pst[egTable][mirSq], -egW);
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
      addPawnCoeff(TI.passedMg[rank], mgW);
      addPawnCoeff(TI.passedEg[rank], egW);
      whitePassedFiles |= 1 << fileOf(sq);
      if (squareBB(sq) & whitePawnAttacks) {
        addPawnCoeff(TI.protPassMg, mgW);
      }
    }
    if (isIsolated(sq, whitePawns)) {
      addPawnCoeff(TI.isolatedMg, mgW);
      addPawnCoeff(TI.isolatedEg, egW);
    }
    if (isDoubled(sq, Color::WHITE, whitePawns)) {
      addPawnCoeff(TI.doubledMg, mgW);
      addPawnCoeff(TI.doubledEg, egW);
    }
    if (isBackward(sq, Color::WHITE, whitePawns, blackPawnAttacks)) {
      addPawnCoeff(TI.backwardMg, mgW);
      addPawnCoeff(TI.backwardEg, egW);
    }
  }

  Bitboard bp = blackPawns;
  while (bp) {
    Square sq = popLsb(bp);
    int rank = rankOf(sq);

    if (isPassed(sq, Color::BLACK, whitePawns)) {
      int mirRank = 7 - rank;
      addPawnCoeff(TI.passedMg[mirRank], -mgW);
      addPawnCoeff(TI.passedEg[mirRank], -egW);
      blackPassedFiles |= 1 << fileOf(sq);
      if (squareBB(sq) & blackPawnAttacks) {
        addPawnCoeff(TI.protPassMg, -mgW);
      }
    }
    if (isIsolated(sq, blackPawns)) {
      addPawnCoeff(TI.isolatedMg, -mgW);
      addPawnCoeff(TI.isolatedEg, -egW);
    }
    if (isDoubled(sq, Color::BLACK, blackPawns)) {
      addPawnCoeff(TI.doubledMg, -mgW);
      addPawnCoeff(TI.doubledEg, -egW);
    }
    if (isBackward(sq, Color::BLACK, blackPawns, whitePawnAttacks)) {
      addPawnCoeff(TI.backwardMg, -mgW);
      addPawnCoeff(TI.backwardEg, -egW);
    }
  }

  for (int f = 0; f < 7; ++f) {
    if ((whitePassedFiles >> f & 1) && (whitePassedFiles >> (f + 1) & 1)) {
      addPawnCoeff(TI.connectedMg, mgW);
      addPawnCoeff(TI.connectedEg, egW);
    }
    if ((blackPassedFiles >> f & 1) && (blackPassedFiles >> (f + 1) & 1)) {
      addPawnCoeff(TI.connectedMg, -mgW);
      addPawnCoeff(TI.connectedEg, -egW);
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
    t.add(TI.passerOwn, ownCoeff * egW);
    t.add(TI.passerEnemy, enemyCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Bishop pair (separate MG/EG).
  // -----------------------------------------------------------------------
  {
    int wBP = (popcount(bb.byPiece[pieceIndex('B')]) >= 2) ? 1 : 0;
    int bBP = (popcount(bb.byPiece[pieceIndex('b')]) >= 2) ? 1 : 0;
    float diff = static_cast<float>(wBP - bBP);
    t.add(TI.bpMg, diff * mgW);
    t.add(TI.bpEg, diff * egW);
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
    t.add(TI.badBishMg, mgCoeff * mgW);
    t.add(TI.badBishEg, egCoeff * egW);
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
    t.add(TI.rookOpenMg, openCoeff * mgW);
    t.add(TI.rookOpenEg, openCoeff * egW);
    t.add(TI.rookSemiMg, semiCoeff * mgW);
    t.add(TI.rookSemiEg, semiCoeff * egW);
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
    t.add(TI.r7Mg, coeff * mgW);
    t.add(TI.r7Eg, coeff * egW);
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
    t.add(TI.rbpOwn, ownCoeff * egW);
    t.add(TI.rbpEnemy, enemyCoeff * egW);
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
    t.add(TI.outpostMg, coeff * mgW);
    t.add(TI.outpostEg, coeff * egW);
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

    t.add(TI.shieldMissing, missingCoeff * mgW);
    t.add(TI.shieldAdvRank3, rank3Coeff * mgW);
    t.add(TI.shieldAdvRank4Plus, rank4PlusCoeff * mgW);
    t.add(TI.shieldOpen, openCoeff * mgW);
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
    t.add(TI.spaceMg, diff * mgW);
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
    t.add(TI.trappedBish, bishCoeff * mgW);

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
    t.add(TI.trappedRook, rookCoeff * mgW);
  }

  // -----------------------------------------------------------------------
  // Mobility (separate MG/EG per piece type) — requires attack info.
  // -----------------------------------------------------------------------
  attacks::AttackInfo info = attacks::computeAll(bb);

  {
    float nCoeff = 0, bCoeff = 0, rCoeff = 0, qCoeff = 0;
    for (int c = 0; c < 2; ++c) {
      int sign = (c == 0) ? 1 : -1;
      Bitboard friendly = bb.byColor[c];
      nCoeff += sign * popcount(info.byPiece[c][2] & ~friendly);
      bCoeff += sign * popcount(info.byPiece[c][3] & ~friendly);
      rCoeff += sign * popcount(info.byPiece[c][4] & ~friendly);
      qCoeff += sign * popcount(info.byPiece[c][5] & ~friendly);
    }
    t.add(TI.mobNMg, nCoeff * mgW); t.add(TI.mobNEg, nCoeff * egW);
    t.add(TI.mobBMg, bCoeff * mgW); t.add(TI.mobBEg, bCoeff * egW);
    t.add(TI.mobRMg, rCoeff * mgW); t.add(TI.mobREg, rCoeff * egW);
    t.add(TI.mobQMg, qCoeff * mgW); t.add(TI.mobQEg, qCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Threats (separate MG/EG, 6 threat types — minor-vs-queen and
  // rook-vs-queen have MG only).
  // -----------------------------------------------------------------------
  {
    float pMin = 0, pRk = 0, pQn = 0, nRk = 0, nQn = 0, rQn = 0;
    for (int c = 0; c < 2; ++c) {
      int sign = (c == 0) ? 1 : -1;
      Color enemy = static_cast<Color>(1 - c);
      Bitboard enMinors = bb.byPiece[pieceIndex(enemy, PieceType::KNIGHT)]
                        | bb.byPiece[pieceIndex(enemy, PieceType::BISHOP)];
      Bitboard enRooks  = bb.byPiece[pieceIndex(enemy, PieceType::ROOK)];
      Bitboard enQueens = bb.byPiece[pieceIndex(enemy, PieceType::QUEEN)];

      Bitboard pAtk = info.byPiece[c][1];   // PieceType::PAWN = 1
      pMin += sign * popcount(pAtk & enMinors);
      pRk  += sign * popcount(pAtk & enRooks);
      pQn  += sign * popcount(pAtk & enQueens);

      Bitboard minAtk = info.byPiece[c][2] | info.byPiece[c][3];  // KNIGHT|BISHOP
      nRk += sign * popcount(minAtk & enRooks);
      nQn += sign * popcount(minAtk & enQueens);

      Bitboard rkAtk = info.byPiece[c][4];   // PieceType::ROOK = 4
      rQn += sign * popcount(rkAtk & enQueens);
    }
    t.add(TI.thPMinMg, pMin * mgW);
    t.add(TI.thPRkMg, pRk * mgW);
    t.add(TI.thPQnMg, pQn * mgW);
    t.add(TI.thNRkMg, nRk * mgW);
    t.add(TI.thNQnMg, nQn * mgW);
    t.add(TI.thRQnMg, rQn * mgW);
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
    if (idx > 0 && TI.kdTable[idx] >= 0)
      t.add(TI.kdTable[idx], static_cast<float>(sign) * mgW);
  }

  return t;
}

// ===========================================================================
// Tuning parameter registry — maps tunable eval constants to metadata
// so the optimizer can read/write them at runtime.
//
// Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method
// ===========================================================================

struct TuneEntry {
  const char* name;
  int* ptr;
  int defaultVal;  // Snapshot of *ptr at registration time (single source of truth).
  int min, max, step;
};

// ---------------------------------------------------------------------------
// buildRegistry — constructs the full parameter list on first access.
//
// Scalar eval constants are listed explicitly.  PST entries (12 arrays × 64
// squares, minus 16 frozen pawn rank-1/rank-8 squares = 752 entries) are
// bulk-registered via loops to avoid 752 manual lines.
// Total: 91 scalar + 752 PST = 843 tunable parameters.
// ---------------------------------------------------------------------------
// clang-format off
static std::vector<TuneEntry>& buildRegistry() {
  static std::vector<TuneEntry> reg;
  if (!reg.empty()) return reg;

  // ---- Scalar entries (91) ------------------------------------------------
  // Default values are read from the live variable (*ptr) at registration
  // time — single source of truth with evaluation.cpp constants.

  // --- Material MG (5) ---
  // All piece types tunable.  Search pruning margins (futility, delta,
  // razor) are hardcoded centipawn constants, not multiples of pawn value,
  // so pawn MG drifting slightly from 100 is safe.
  reg.push_back({"MAT_PAWN_MG",             &MATERIAL[0],       MATERIAL[0],   80,  130,  5});
  reg.push_back({"MAT_KNIGHT_MG",           &MATERIAL[1],       MATERIAL[1],  250,  400, 10});
  reg.push_back({"MAT_BISHOP_MG",           &MATERIAL[2],       MATERIAL[2],  250,  420, 10});
  reg.push_back({"MAT_ROOK_MG",             &MATERIAL[3],       MATERIAL[3],  400,  610, 10});
  reg.push_back({"MAT_QUEEN_MG",            &MATERIAL[4],       MATERIAL[4],  800, 1250, 20});

  // --- Material EG (5) ---
  // N/B/R/Q EG values allow phase-optimal material weights.
  reg.push_back({"MAT_PAWN_EG",             &MATERIAL_EG[0],    MATERIAL_EG[0],  80,  150,  5});
  reg.push_back({"MAT_KNIGHT_EG",           &MATERIAL_EG[1],    MATERIAL_EG[1], 230,  400, 10});
  reg.push_back({"MAT_BISHOP_EG",           &MATERIAL_EG[2],    MATERIAL_EG[2], 250,  420, 10});
  reg.push_back({"MAT_ROOK_EG",             &MATERIAL_EG[3],    MATERIAL_EG[3], 430,  650, 10});
  reg.push_back({"MAT_QUEEN_EG",            &MATERIAL_EG[4],    MATERIAL_EG[4], 850, 1300, 20});

  // --- Passed pawn rank bonus (12) ---
  reg.push_back({"PASSED_R2_MG",            &PASSED_RANK_BONUS_MG[1], PASSED_RANK_BONUS_MG[1],    0,   30,  5});
  reg.push_back({"PASSED_R3_MG",            &PASSED_RANK_BONUS_MG[2], PASSED_RANK_BONUS_MG[2],    0,   40,  5});
  reg.push_back({"PASSED_R4_MG",            &PASSED_RANK_BONUS_MG[3], PASSED_RANK_BONUS_MG[3],    0,   50,  5});
  reg.push_back({"PASSED_R5_MG",            &PASSED_RANK_BONUS_MG[4], PASSED_RANK_BONUS_MG[4],    5,  150, 10});
  reg.push_back({"PASSED_R6_MG",            &PASSED_RANK_BONUS_MG[5], PASSED_RANK_BONUS_MG[5],   10,  180, 10});
  reg.push_back({"PASSED_R7_MG",            &PASSED_RANK_BONUS_MG[6], PASSED_RANK_BONUS_MG[6],   20,  300, 15});
  reg.push_back({"PASSED_R2_EG",            &PASSED_RANK_BONUS_EG[1], PASSED_RANK_BONUS_EG[1],    0,   30,  5});
  reg.push_back({"PASSED_R3_EG",            &PASSED_RANK_BONUS_EG[2], PASSED_RANK_BONUS_EG[2],    0,   50,  5});
  reg.push_back({"PASSED_R4_EG",            &PASSED_RANK_BONUS_EG[3], PASSED_RANK_BONUS_EG[3],    0,   80, 10});
  reg.push_back({"PASSED_R5_EG",            &PASSED_RANK_BONUS_EG[4], PASSED_RANK_BONUS_EG[4],   10,  150, 10});
  reg.push_back({"PASSED_R6_EG",            &PASSED_RANK_BONUS_EG[5], PASSED_RANK_BONUS_EG[5],   20,  300, 15});
  reg.push_back({"PASSED_R7_EG",            &PASSED_RANK_BONUS_EG[6], PASSED_RANK_BONUS_EG[6],   80,  500, 20});

  // --- Pawn structure scalars (9) ---
  reg.push_back({"CONNECTED_PASSED_MG",     &CONNECTED_PASSED_MG,    CONNECTED_PASSED_MG,    0,   40,  5});
  reg.push_back({"CONNECTED_PASSED_EG",     &CONNECTED_PASSED_EG,    CONNECTED_PASSED_EG,    0,   60,  5});
  reg.push_back({"ISOLATED_PENALTY_MG",     &ISOLATED_PENALTY_MG,    ISOLATED_PENALTY_MG,  -30,    0,  5});
  reg.push_back({"ISOLATED_PENALTY_EG",     &ISOLATED_PENALTY_EG,    ISOLATED_PENALTY_EG,  -40,    0,  5});
  reg.push_back({"DOUBLED_PENALTY_MG",      &DOUBLED_PENALTY_MG,     DOUBLED_PENALTY_MG,   -40,    0,  5});
  reg.push_back({"DOUBLED_PENALTY_EG",      &DOUBLED_PENALTY_EG,     DOUBLED_PENALTY_EG,   -50,    0,  5});
  reg.push_back({"BACKWARD_PENALTY_MG",     &BACKWARD_PENALTY_MG,    BACKWARD_PENALTY_MG,  -35,    0,  5});
  reg.push_back({"BACKWARD_PENALTY_EG",     &BACKWARD_PENALTY_EG,    BACKWARD_PENALTY_EG,  -35,    0,  5});
  reg.push_back({"PROTECTED_PASSER_MG",     &PROTECTED_PASSER_MG,    PROTECTED_PASSER_MG,    0,   40,  5});

  // --- Bishop pair (2) ---
  reg.push_back({"BISHOP_PAIR_MG",          &BISHOP_PAIR_MG,         BISHOP_PAIR_MG,           0,  100,  5});
  reg.push_back({"BISHOP_PAIR_EG",          &BISHOP_PAIR_EG,         BISHOP_PAIR_EG,          10,  150,  5});

  // --- Rook on file (4) ---
  reg.push_back({"ROOK_OPEN_FILE_MG",       &ROOK_OPEN_FILE_MG,      ROOK_OPEN_FILE_MG,        0,   50,  5});
  reg.push_back({"ROOK_OPEN_FILE_EG",       &ROOK_OPEN_FILE_EG,      ROOK_OPEN_FILE_EG,        0,   50,  5});
  reg.push_back({"ROOK_SEMI_OPEN_FILE_MG",  &ROOK_SEMI_OPEN_FILE_MG, ROOK_SEMI_OPEN_FILE_MG,   0,   40,  5});
  reg.push_back({"ROOK_SEMI_OPEN_FILE_EG",  &ROOK_SEMI_OPEN_FILE_EG, ROOK_SEMI_OPEN_FILE_EG,   0,   40,  5});

  // --- Rook on 7th (2) ---
  reg.push_back({"ROOK_7TH_MG",             &ROOK_7TH_MG,           ROOK_7TH_MG,              5,   50,  5});
  reg.push_back({"ROOK_7TH_EG",             &ROOK_7TH_EG,           ROOK_7TH_EG,              0,   80,  5});

  // --- Rook behind passer (2) ---
  reg.push_back({"ROOK_BEHIND_OWN_EG",      &ROOK_BEHIND_OWN_PASSER_EG,   ROOK_BEHIND_OWN_PASSER_EG,    0,   50,  5});
  reg.push_back({"ROOK_BEHIND_ENEMY_EG",    &ROOK_BEHIND_ENEMY_PASSER_EG, ROOK_BEHIND_ENEMY_PASSER_EG, -50,   10,  5});

  // --- Outpost (2) ---
  reg.push_back({"OUTPOST_BONUS_MG",        &OUTPOST_BONUS_MG,       OUTPOST_BONUS_MG,          0,   60,  5});
  reg.push_back({"OUTPOST_BONUS_EG",        &OUTPOST_BONUS_EG,       OUTPOST_BONUS_EG,          0,   40,  5});

  // --- Bad bishop (2) ---
  reg.push_back({"BAD_BISHOP_MG",           &BAD_BISHOP_MG,          BAD_BISHOP_MG,           -15,    0,  1});
  reg.push_back({"BAD_BISHOP_EG",           &BAD_BISHOP_EG,          BAD_BISHOP_EG,           -15,    0,  1});

  // --- Trapped pieces (2) ---
  reg.push_back({"TRAPPED_BISHOP",          &TRAPPED_BISHOP_PENALTY, TRAPPED_BISHOP_PENALTY, -120,    0, 10});
  reg.push_back({"TRAPPED_ROOK",            &TRAPPED_ROOK_PENALTY,   TRAPPED_ROOK_PENALTY,   -100,    0, 10});

  // --- Mobility (8) ---
  // Min bounds prevent zeroing: each piece type must have a non-zero
  // mobility weight in at least one phase (tuner-starved features otherwise
  // collapse to zero, losing signal).
  reg.push_back({"MOBILITY_KNIGHT_MG",      &MOBILITY_KNIGHT_MG,     MOBILITY_KNIGHT_MG,       1,   20,  1});
  reg.push_back({"MOBILITY_KNIGHT_EG",      &MOBILITY_KNIGHT_EG,     MOBILITY_KNIGHT_EG,       1,   12,  1});
  reg.push_back({"MOBILITY_BISHOP_MG",      &MOBILITY_BISHOP_MG,     MOBILITY_BISHOP_MG,       1,   12,  1});
  reg.push_back({"MOBILITY_BISHOP_EG",      &MOBILITY_BISHOP_EG,     MOBILITY_BISHOP_EG,       1,   12,  1});
  reg.push_back({"MOBILITY_ROOK_MG",        &MOBILITY_ROOK_MG,       MOBILITY_ROOK_MG,         1,   16,  1});
  reg.push_back({"MOBILITY_ROOK_EG",        &MOBILITY_ROOK_EG,       MOBILITY_ROOK_EG,         1,   16,  1});
  reg.push_back({"MOBILITY_QUEEN_MG",       &MOBILITY_QUEEN_MG,      MOBILITY_QUEEN_MG,        1,   12,  1});
  reg.push_back({"MOBILITY_QUEEN_EG",       &MOBILITY_QUEEN_EG,      MOBILITY_QUEEN_EG,        1,   16,  1});

  // --- King safety shield (4) ---
  reg.push_back({"SHIELD_MISSING_PAWN",     &SHIELD_MISSING_PAWN,    SHIELD_MISSING_PAWN,    -60,    0,  5});
  reg.push_back({"SHIELD_ADV_RANK3",        &SHIELD_ADV_RANK3,       SHIELD_ADV_RANK3,       -30,    0,  5});
  reg.push_back({"SHIELD_ADV_RANK4PLUS",    &SHIELD_ADV_RANK4PLUS,   SHIELD_ADV_RANK4PLUS,   -30,    0,  5});
  reg.push_back({"SHIELD_OPEN_FILE",        &SHIELD_OPEN_FILE,       SHIELD_OPEN_FILE,       -50,    0,  5});

  // --- King danger table (12) ---
  // Entries 1..12 of the nonlinear penalty table.  TABLE[0] = 0 is fixed.
  // Weights are constexpr — tuning shifts danger via table entries instead,
  // keeping all parameters linear for analytical gradient computation.
  reg.push_back({"KD_TABLE_1",              &KING_DANGER_TABLE[1],   KING_DANGER_TABLE[1],     0,   20,  5});
  reg.push_back({"KD_TABLE_2",              &KING_DANGER_TABLE[2],   KING_DANGER_TABLE[2],     0,   30,  5});
  reg.push_back({"KD_TABLE_3",              &KING_DANGER_TABLE[3],   KING_DANGER_TABLE[3],     0,   50,  5});
  reg.push_back({"KD_TABLE_4",              &KING_DANGER_TABLE[4],   KING_DANGER_TABLE[4],     0,   80, 10});
  reg.push_back({"KD_TABLE_5",              &KING_DANGER_TABLE[5],   KING_DANGER_TABLE[5],    10,  130, 10});
  reg.push_back({"KD_TABLE_6",              &KING_DANGER_TABLE[6],   KING_DANGER_TABLE[6],    20,  200, 10});
  reg.push_back({"KD_TABLE_7",              &KING_DANGER_TABLE[7],   KING_DANGER_TABLE[7],    40,  280, 15});
  reg.push_back({"KD_TABLE_8",              &KING_DANGER_TABLE[8],   KING_DANGER_TABLE[8],    60,  360, 15});
  reg.push_back({"KD_TABLE_9",              &KING_DANGER_TABLE[9],   KING_DANGER_TABLE[9],    80,  440, 20});
  reg.push_back({"KD_TABLE_10",             &KING_DANGER_TABLE[10],  KING_DANGER_TABLE[10],  100,  500, 20});
  reg.push_back({"KD_TABLE_11",             &KING_DANGER_TABLE[11],  KING_DANGER_TABLE[11],  120,  600, 25});
  reg.push_back({"KD_TABLE_12",             &KING_DANGER_TABLE[12],  KING_DANGER_TABLE[12],  150,  700, 25});

  // --- Space (1) ---
  // Space is a middlegame concept (CPW); EG component removed after
  // multiple tuning runs consistently converged it to zero.
  reg.push_back({"SPACE_BONUS_MG",          &SPACE_BONUS_MG,         SPACE_BONUS_MG,            0,   10,  1});

  // --- Passed pawn king distance (2) ---
  reg.push_back({"PASSER_OWN_KING",         &PASSER_OWN_KING,       PASSER_OWN_KING,           0,   15,  1});
  reg.push_back({"PASSER_ENEMY_KING",       &PASSER_ENEMY_KING,     PASSER_ENEMY_KING,         0,   25,  2});

  // --- Threats (6) ---
  // Threats are a middlegame concept (CPW: piece safety dictates midgame
  // strategy).  EG components removed after multiple tuning runs
  // consistently converged all four EG threat values to zero.
  reg.push_back({"THREAT_P_MINOR_MG",       &THREAT_PAWN_VS_MINOR_MG,   THREAT_PAWN_VS_MINOR_MG,    0,   30,  2});
  reg.push_back({"THREAT_P_ROOK_MG",        &THREAT_PAWN_VS_ROOK_MG,    THREAT_PAWN_VS_ROOK_MG,     0,   35,  2});
  reg.push_back({"THREAT_P_QUEEN_MG",       &THREAT_PAWN_VS_QUEEN_MG,   THREAT_PAWN_VS_QUEEN_MG,    0,   50,  3});
  reg.push_back({"THREAT_N_ROOK_MG",        &THREAT_MINOR_VS_ROOK_MG,   THREAT_MINOR_VS_ROOK_MG,    0,   45,  2});
  reg.push_back({"THREAT_N_QUEEN_MG",       &THREAT_MINOR_VS_QUEEN_MG,  THREAT_MINOR_VS_QUEEN_MG,   0,   55,  2});
  reg.push_back({"THREAT_R_QUEEN_MG",       &THREAT_ROOK_VS_QUEEN_MG,   THREAT_ROOK_VS_QUEEN_MG,    0,   30,  1});

  // clang-format on

  // ---- PST entries (752) --------------------------------------------------
  // 12 arrays × 64 squares = 768, minus 16 frozen pawn squares (rank 1 + 8).
  // Bulk-registered via loop.  Total: 91 scalar + 752 PST = 843 params.
  struct PstDef {
    const char* prefix;
    int* data;
    bool isPawn;
  };
  // clang-format off
  PstDef psts[] = {
    {"PST_PAWN_MG",   PST_PAWN_MG,   true },
    {"PST_KNIGHT_MG", PST_KNIGHT_MG, false},
    {"PST_BISHOP_MG", PST_BISHOP_MG, false},
    {"PST_ROOK_MG",   PST_ROOK_MG,   false},
    {"PST_QUEEN_MG",  PST_QUEEN_MG,  false},
    {"PST_KING_MG",   PST_KING_MG,   false},
    {"PST_PAWN_EG",   PST_PAWN_EG,   true },
    {"PST_KNIGHT_EG", PST_KNIGHT_EG, false},
    {"PST_BISHOP_EG", PST_BISHOP_EG, false},
    {"PST_ROOK_EG",   PST_ROOK_EG,   false},
    {"PST_QUEEN_EG",  PST_QUEEN_EG,  false},
    {"PST_KING_EG",   PST_KING_EG,   false},
  };
  // clang-format on

  for (const auto& p : psts) {
    for (int sq = 0; sq < 64; ++sq) {
      // Pawn rank 1 (sq 0-7) and rank 8 (sq 56-63) are never occupied.
      if (p.isPawn && (sq < 8 || sq >= 56)) continue;

      char buf[32];
      std::snprintf(buf, sizeof(buf), "%s_%d", p.prefix, sq);

      // Heap-allocate name — tuning build only, leak is intentional.
      char* name = new char[std::strlen(buf) + 1];
      std::strcpy(name, buf);

      reg.push_back({name, &p.data[sq], p.data[sq], -100, 100, 5});
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
void setValue(int i, int v)   { *buildRegistry()[i].ptr = v; invalidatePSQT(); }
int getDefault(int i)         { return buildRegistry()[i].defaultVal; }
int getMin(int i)             { return buildRegistry()[i].min; }
int getMax(int i)             { return buildRegistry()[i].max; }
int getStep(int i)            { return buildRegistry()[i].step; }

}  // namespace tuning

}  // namespace eval
}  // namespace LibreChess

#endif  // TUNING
