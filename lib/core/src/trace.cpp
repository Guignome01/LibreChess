#ifdef TUNING

#include "trace.h"
#include "attacks.h"
#include "evaluation.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace LibreChess {
namespace eval {

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
  // Material (4)
  int mat[4];

  // PST (12 tables × 64 squares)
  int pst[12][64];

  // Passed pawn rank bonuses (ranks 0-7, only 1-6 used)
  int passedMg[8], passedEg[8];

  // Pawn structure scalars
  int connected, isolated, doubled, backward;
  int protPassMg, protPassEg, candPassMg, candPassEg;

  // Passed pawn king distance
  int passerOwn, passerEnemy;

  // Bishop pair
  int bpMg, bpEg;

  // Bad bishop
  int badBishMg, badBishEg;

  // Rook on file
  int rookOpen, rookSemi;

  // Rook on 7th
  int r7Mg, r7Eg;

  // Rook behind passer
  int rbpOwn, rbpEnemy;

  // Outpost
  int outpost;

  // King safety shield
  int shieldMissing, shieldAdvanced, shieldOpen;

  // Center control
  int ccOccupy, ccAttack;

  // Space
  int space;

  // Trapped pieces
  int trappedBish, trappedRook;

  // Mobility (per piece type, MG/EG)
  int mobNMg, mobNEg, mobBMg, mobBEg;
  int mobRMg, mobREg, mobQMg, mobQEg;

  // Connectivity
  int conn;

  // Threats (6 types × MG/EG)
  int thPMinMg, thPMinEg, thPRkMg, thPRkEg;
  int thPQnMg, thPQnEg, thNRkMg, thNRkEg;
  int thNQnMg, thNQnEg, thRQnMg, thRQnEg;

  // King danger table
  int kdTable[KING_DANGER_TABLE_SIZE];
};

static TraceIndices TI;

// ---------------------------------------------------------------------------
// initTraceIndices — populate all index fields from the parameter map.
// Called once from buildParamMap() after the map is built.
// ---------------------------------------------------------------------------

static void initTraceIndices() {
  // Material
  TI.mat[0] = findParam("MAT_KNIGHT");
  TI.mat[1] = findParam("MAT_BISHOP");
  TI.mat[2] = findParam("MAT_ROOK");
  TI.mat[3] = findParam("MAT_QUEEN");

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

  // Pawn structure scalars
  TI.connected  = findParam("CONNECTED_PASSED");
  TI.isolated   = findParam("ISOLATED_PENALTY");
  TI.doubled    = findParam("DOUBLED_PENALTY");
  TI.backward   = findParam("BACKWARD_PENALTY");
  TI.protPassMg = findParam("PROTECTED_PASSER_MG");
  TI.protPassEg = findParam("PROTECTED_PASSER_EG");
  TI.candPassMg = findParam("CANDIDATE_PASSER_MG");
  TI.candPassEg = findParam("CANDIDATE_PASSER_EG");

  // Passed pawn king distance
  TI.passerOwn   = findParam("PASSER_OWN_KING");
  TI.passerEnemy = findParam("PASSER_ENEMY_KING");

  // Bishop pair
  TI.bpMg = findParam("BISHOP_PAIR_MG");
  TI.bpEg = findParam("BISHOP_PAIR_EG");

  // Bad bishop
  TI.badBishMg = findParam("BAD_BISHOP_MG");
  TI.badBishEg = findParam("BAD_BISHOP_EG");

  // Rook on file
  TI.rookOpen = findParam("ROOK_OPEN_FILE");
  TI.rookSemi = findParam("ROOK_SEMI_OPEN_FILE");

  // Rook on 7th
  TI.r7Mg = findParam("ROOK_7TH_MG");
  TI.r7Eg = findParam("ROOK_7TH_EG");

  // Rook behind passer
  TI.rbpOwn   = findParam("ROOK_BEHIND_OWN_EG");
  TI.rbpEnemy = findParam("ROOK_BEHIND_ENEMY_EG");

  // Outpost
  TI.outpost = findParam("OUTPOST_BONUS");

  // King safety shield
  TI.shieldMissing  = findParam("SHIELD_MISSING_PAWN");
  TI.shieldAdvanced = findParam("SHIELD_ADVANCED_PAWN");
  TI.shieldOpen     = findParam("SHIELD_OPEN_FILE");

  // Center control
  TI.ccOccupy = findParam("CENTER_OCCUPATION");
  TI.ccAttack = findParam("CENTER_ATTACK");

  // Space
  TI.space = findParam("SPACE_BONUS");

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

  // Connectivity
  TI.conn = findParam("CONNECTIVITY");

  // Threats
  TI.thPMinMg = findParam("THREAT_P_MINOR_MG");
  TI.thPMinEg = findParam("THREAT_P_MINOR_EG");
  TI.thPRkMg  = findParam("THREAT_P_ROOK_MG");
  TI.thPRkEg  = findParam("THREAT_P_ROOK_EG");
  TI.thPQnMg  = findParam("THREAT_P_QUEEN_MG");
  TI.thPQnEg  = findParam("THREAT_P_QUEEN_EG");
  TI.thNRkMg  = findParam("THREAT_N_ROOK_MG");
  TI.thNRkEg  = findParam("THREAT_N_ROOK_EG");
  TI.thNQnMg  = findParam("THREAT_N_QUEEN_MG");
  TI.thNQnEg  = findParam("THREAT_N_QUEEN_EG");
  TI.thRQnMg  = findParam("THREAT_R_QUEEN_MG");
  TI.thRQnEg  = findParam("THREAT_R_QUEEN_EG");

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
  int phase = popcount(bb.byPiece[1] | bb.byPiece[7])  * PHASE_KNIGHT
            + popcount(bb.byPiece[2] | bb.byPiece[8])  * PHASE_BISHOP
            + popcount(bb.byPiece[3] | bb.byPiece[9])  * PHASE_ROOK
            + popcount(bb.byPiece[4] | bb.byPiece[10]) * PHASE_QUEEN;
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
    Bitboard wb = bb.byPiece[2];
    Bitboard bbish = bb.byPiece[8];
    if (popcount(wb) == 1 && popcount(bbish) == 1) {
      bool whiteDark = (wb & DARK_SQUARES) != 0;
      bool blackDark = (bbish & DARK_SQUARES) != 0;
      if (whiteDark != blackDark) ocbScale = 0.75f;
    }
  }

  float mgW = mgWeight * ocbScale;
  float egW = egWeight * ocbScale;
  float bothW = mgW + egW;  // = ocbScale (since mgWeight + egWeight = 1.0).

  // -----------------------------------------------------------------------
  // Fixed pawn material bias.
  //
  // MAT_PAWN (MATERIAL[0] = 100) is pinned — it defines the centipawn
  // unit and is not part of the tuning registry.  Its contribution must
  // be captured as a fixed offset so traceScore matches evaluatePosition.
  // -----------------------------------------------------------------------
  int wPawnCount = popcount(bb.byPiece[0]);
  int bPawnCount = popcount(bb.byPiece[6]);
  t.bias = static_cast<float>((wPawnCount - bPawnCount) * MATERIAL[0]) * bothW;

  // -----------------------------------------------------------------------
  // Material (MATERIAL[1..4]).
  // -----------------------------------------------------------------------
  for (int i = 0; i < 4; ++i) {
    if (TI.mat[i] < 0) continue;
    int pt = i + 1;
    int wCount = popcount(bb.byPiece[pt]);
    int bCount = popcount(bb.byPiece[pt + 6]);
    t.add(TI.mat[i], static_cast<float>(wCount - bCount) * bothW);
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
  Bitboard whitePawns = bb.byPiece[0];
  Bitboard blackPawns = bb.byPiece[6];
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
    int rank = sq / 8;

    if (isPassed(sq, Color::WHITE, blackPawns)) {
      addPawnCoeff(TI.passedMg[rank], mgW);
      addPawnCoeff(TI.passedEg[rank], egW);
      whitePassedFiles |= 1 << (sq & 7);
      if (squareBB(sq) & whitePawnAttacks) {
        addPawnCoeff(TI.protPassMg, mgW);
        addPawnCoeff(TI.protPassEg, egW);
      }
    } else {
      Bitboard mask = 0;
      int file = sq & 7;
      for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
        for (int r = rank + 1; r <= 7; ++r) mask |= squareBB(r * 8 + f);
      if (popcount(blackPawns & mask) == 1) {
        addPawnCoeff(TI.candPassMg, mgW);
        addPawnCoeff(TI.candPassEg, egW);
      }
    }
    if (isIsolated(sq, whitePawns))
      addPawnCoeff(TI.isolated, bothW);
    if (isDoubled(sq, Color::WHITE, whitePawns))
      addPawnCoeff(TI.doubled, bothW);
    if (isBackward(sq, Color::WHITE, whitePawns, blackPawnAttacks))
      addPawnCoeff(TI.backward, bothW);
  }

  Bitboard bp = blackPawns;
  while (bp) {
    Square sq = popLsb(bp);
    int rank = sq / 8;

    if (isPassed(sq, Color::BLACK, whitePawns)) {
      int mirRank = 7 - rank;
      addPawnCoeff(TI.passedMg[mirRank], -mgW);
      addPawnCoeff(TI.passedEg[mirRank], -egW);
      blackPassedFiles |= 1 << (sq & 7);
      if (squareBB(sq) & blackPawnAttacks) {
        addPawnCoeff(TI.protPassMg, -mgW);
        addPawnCoeff(TI.protPassEg, -egW);
      }
    } else {
      Bitboard mask = 0;
      int file = sq & 7;
      for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
        for (int r = rank - 1; r >= 0; --r) mask |= squareBB(r * 8 + f);
      if (popcount(whitePawns & mask) == 1) {
        addPawnCoeff(TI.candPassMg, -mgW);
        addPawnCoeff(TI.candPassEg, -egW);
      }
    }
    if (isIsolated(sq, blackPawns))
      addPawnCoeff(TI.isolated, -bothW);
    if (isDoubled(sq, Color::BLACK, blackPawns))
      addPawnCoeff(TI.doubled, -bothW);
    if (isBackward(sq, Color::BLACK, blackPawns, whitePawnAttacks))
      addPawnCoeff(TI.backward, -bothW);
  }

  for (int f = 0; f < 7; ++f) {
    if ((whitePassedFiles >> f & 1) && (whitePassedFiles >> (f + 1) & 1))
      addPawnCoeff(TI.connected, bothW);
    if ((blackPassedFiles >> f & 1) && (blackPassedFiles >> (f + 1) & 1))
      addPawnCoeff(TI.connected, -bothW);
  }

  for (auto& pc : pawnCoeffs) t.add(pc.first, pc.second);

  // -----------------------------------------------------------------------
  // Passed pawn king distance (EG only).
  // -----------------------------------------------------------------------
  Bitboard wkBB = bb.byPiece[5];
  Bitboard bkBB = bb.byPiece[11];
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
    int wBP = (popcount(bb.byPiece[2]) >= 2) ? 1 : 0;
    int bBP = (popcount(bb.byPiece[8]) >= 2) ? 1 : 0;
    float diff = static_cast<float>(wBP - bBP);
    t.add(TI.bpMg, diff * mgW);
    t.add(TI.bpEg, diff * egW);
  }

  // -----------------------------------------------------------------------
  // Bad bishop (separate MG/EG).
  // -----------------------------------------------------------------------
  {
    float mgCoeff = 0.0f, egCoeff = 0.0f;
    Bitboard wbishops = bb.byPiece[2];
    while (wbishops) {
      Square sq = popLsb(wbishops);
      Bitboard colorMask = (squareBB(sq) & DARK_SQUARES) ? DARK_SQUARES
                                                         : LIGHT_SQUARES;
      int blocked = popcount(whitePawns & colorMask);
      mgCoeff += blocked;
      egCoeff += blocked;
    }
    Bitboard bbishops = bb.byPiece[8];
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
  // Rook on file (same value for MG + EG).
  // -----------------------------------------------------------------------
  {
    Bitboard allPawns = whitePawns | blackPawns;
    float openCoeff = 0.0f, semiCoeff = 0.0f;

    Bitboard wr = bb.byPiece[3];
    while (wr) {
      Square sq = popLsb(wr);
      Bitboard file = fileBB(colOf(sq));
      if (!(file & allPawns))        openCoeff += 1.0f;
      else if (!(file & whitePawns)) semiCoeff += 1.0f;
    }
    Bitboard br = bb.byPiece[9];
    while (br) {
      Square sq = popLsb(br);
      Bitboard file = fileBB(colOf(sq));
      if (!(file & allPawns))        openCoeff -= 1.0f;
      else if (!(file & blackPawns)) semiCoeff -= 1.0f;
    }
    t.add(TI.rookOpen, openCoeff * bothW);
    t.add(TI.rookSemi, semiCoeff * bothW);
  }

  // -----------------------------------------------------------------------
  // Rook on 7th (separate MG/EG).
  // -----------------------------------------------------------------------
  {
    float coeff = 0.0f;
    if ((bb.byPiece[3] & rankBB(6)) && (bb.byPiece[11] & rankBB(7)))
      coeff += popcount(bb.byPiece[3] & rankBB(6));
    if ((bb.byPiece[9] & rankBB(1)) && (bb.byPiece[5] & rankBB(0)))
      coeff -= popcount(bb.byPiece[9] & rankBB(1));
    t.add(TI.r7Mg, coeff * mgW);
    t.add(TI.r7Eg, coeff * egW);
  }

  // -----------------------------------------------------------------------
  // Rook behind passer (EG only).
  // -----------------------------------------------------------------------
  {
    float ownCoeff = 0.0f, enemyCoeff = 0.0f;
    Bitboard whiteRooks = bb.byPiece[3];
    Bitboard blackRooks = bb.byPiece[9];

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

    Bitboard wn = bb.byPiece[1];
    while (wn) {
      Square sq = popLsb(wn);
      if (!(squareBB(sq) & whitePawnAttacks)) continue;
      int file = sq & 7;
      Bitboard adjFiles = 0;
      if (file > 0) adjFiles |= fileBB(file - 1);
      if (file < 7) adjFiles |= fileBB(file + 1);
      int rank = sq / 8;
      Bitboard aboveMask =
          ~((static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1);
      if (blackPawns & adjFiles & aboveMask) continue;
      int bonus = 1;
      if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5) bonus = 2;
      coeff += bonus;
    }

    Bitboard bn = bb.byPiece[7];
    while (bn) {
      Square sq = popLsb(bn);
      if (!(squareBB(sq) & blackPawnAttacks)) continue;
      int file = sq & 7;
      Bitboard adjFiles = 0;
      if (file > 0) adjFiles |= fileBB(file - 1);
      if (file < 7) adjFiles |= fileBB(file + 1);
      int rank = sq / 8;
      Bitboard belowMask = (rank > 0)
          ? (static_cast<Bitboard>(1) << (8 * rank)) - 1
          : 0;
      if (whitePawns & adjFiles & belowMask) continue;
      int bonus = 1;
      if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5) bonus = 2;
      coeff -= bonus;
    }
    t.add(TI.outpost, coeff * bothW);
  }

  // -----------------------------------------------------------------------
  // King safety / pawn shield (MG only).
  // -----------------------------------------------------------------------
  {
    float missingCoeff = 0.0f, advancedCoeff = 0.0f, openCoeff = 0.0f;
    Bitboard allPawns = whitePawns | blackPawns;

    auto shieldSide = [&](int color, int sign) {
      Bitboard kingBB_s = bb.byPiece[color * 6 + 5];
      if (!kingBB_s) return;
      Square kingSq = lsb(kingBB_s);
      int kingFile = colOf(kingSq);

      if (kingFile >= 3 && kingFile <= 4) return;

      int shieldFiles[3];
      if (kingFile <= 2) {
        shieldFiles[0] = 0; shieldFiles[1] = 1; shieldFiles[2] = 2;
      } else {
        shieldFiles[0] = 5; shieldFiles[1] = 6; shieldFiles[2] = 7;
      }

      Bitboard friendlyPawns = bb.byPiece[color * 6];
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
            int pRank = psq / 8;
            bool advanced = (color == 0) ? (pRank >= 2) : (pRank <= 5);
            if (advanced) advancedCoeff += sign;
          }
        }
        if (f == kingFile && !(allPawns & fileMask))
          openCoeff += sign;
      }
    };

    shieldSide(0, 1);
    shieldSide(1, -1);

    t.add(TI.shieldMissing, missingCoeff * mgW);
    t.add(TI.shieldAdvanced, advancedCoeff * mgW);
    t.add(TI.shieldOpen, openCoeff * mgW);
  }

  // -----------------------------------------------------------------------
  // Center control (same value for MG + EG).
  // -----------------------------------------------------------------------
  {
    int wOcc = popcount(whitePawns & CENTER_MASK);
    int bOcc = popcount(blackPawns & CENTER_MASK);
    Bitboard wAtk = shiftNE(whitePawns) | shiftNW(whitePawns);
    Bitboard bAtk = shiftSE(blackPawns) | shiftSW(blackPawns);
    int wAtt = popcount(wAtk & CENTER_MASK);
    int bAtt = popcount(bAtk & CENTER_MASK);

    t.add(TI.ccOccupy, static_cast<float>(wOcc - bOcc) * bothW);
    t.add(TI.ccAttack, static_cast<float>(wAtt - bAtt) * bothW);
  }

  // -----------------------------------------------------------------------
  // Space (same value for MG + EG).
  // -----------------------------------------------------------------------
  {
    Bitboard bAtk = shiftSE(blackPawns) | shiftSW(blackPawns);
    Bitboard wAtk = shiftNE(whitePawns) | shiftNW(whitePawns);
    int ws = popcount(WHITE_SPACE_ZONE & ~bAtk);
    int bs = popcount(BLACK_SPACE_ZONE & ~wAtk);
    t.add(TI.space, static_cast<float>(ws - bs) * bothW);
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
    Bitboard wB = bb.byPiece[2], bB = bb.byPiece[8];
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
    Bitboard wR = bb.byPiece[3], bR = bb.byPiece[9];
    Bitboard wK = bb.byPiece[5], bK = bb.byPiece[11];
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
  // Connectivity (same value for MG + EG).
  // -----------------------------------------------------------------------
  {
    float coeff = 0.0f;
    for (int c = 0; c < 2; ++c) {
      int sign = (c == 0) ? 1 : -1;
      Bitboard friendly = bb.byColor[c];
      Bitboard friendlyAtk = info.byColor[c];
      int pawnI = c * 6, kingI = c * 6 + 5;
      Bitboard targets = friendly & ~bb.byPiece[pawnI] & ~bb.byPiece[kingI];
      coeff += sign * popcount(targets & friendlyAtk);
    }
    t.add(TI.conn, coeff * bothW);
  }

  // -----------------------------------------------------------------------
  // Threats (separate MG/EG, 6 threat types).
  // -----------------------------------------------------------------------
  {
    float pMin = 0, pRk = 0, pQn = 0, nRk = 0, nQn = 0, rQn = 0;
    for (int c = 0; c < 2; ++c) {
      int sign = (c == 0) ? 1 : -1;
      int eo = (1 - c) * 6;
      Bitboard enMinors = bb.byPiece[eo + 1] | bb.byPiece[eo + 2];
      Bitboard enRooks  = bb.byPiece[eo + 3];
      Bitboard enQueens = bb.byPiece[eo + 4];

      Bitboard pAtk = info.byPiece[c][0];
      pMin += sign * popcount(pAtk & enMinors);
      pRk  += sign * popcount(pAtk & enRooks);
      pQn  += sign * popcount(pAtk & enQueens);

      Bitboard minAtk = info.byPiece[c][1] | info.byPiece[c][2];
      nRk += sign * popcount(minAtk & enRooks);
      nQn += sign * popcount(minAtk & enQueens);

      Bitboard rkAtk = info.byPiece[c][3];
      rQn += sign * popcount(rkAtk & enQueens);
    }
    t.add(TI.thPMinMg, pMin * mgW); t.add(TI.thPMinEg, pMin * egW);
    t.add(TI.thPRkMg, pRk * mgW);  t.add(TI.thPRkEg, pRk * egW);
    t.add(TI.thPQnMg, pQn * mgW);  t.add(TI.thPQnEg, pQn * egW);
    t.add(TI.thNRkMg, nRk * mgW);  t.add(TI.thNRkEg, nRk * egW);
    t.add(TI.thNQnMg, nQn * mgW);  t.add(TI.thNQnEg, nQn * egW);
    t.add(TI.thRQnMg, rQn * mgW);  t.add(TI.thRQnEg, rQn * egW);
  }

  // -----------------------------------------------------------------------
  // King danger table (MG only).
  // -----------------------------------------------------------------------
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? -1 : 1;
    int enemy = 1 - c;

    Bitboard kingBB_kd = bb.byPiece[c * 6 + 5];
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
        int pieceIdx = enemy * 6 + pt + 1;
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
// ---------------------------------------------------------------------------
// clang-format off
static std::vector<TuneEntry>& buildRegistry() {
  static std::vector<TuneEntry> reg;
  if (!reg.empty()) return reg;

  // ---- Scalar entries (78) ------------------------------------------------
  // Default values are read from the live variable (*ptr) at registration
  // time — single source of truth with evaluation.cpp constants.

  // --- Material (4) ---
  // MAT_PAWN is pinned at 100 — it defines the centipawn unit.
  // Search pruning margins (futility, delta, razor) are calibrated for
  // 100cp/pawn; allowing MAT_PAWN to drift breaks those margins.
  reg.push_back({"MAT_KNIGHT",              &MATERIAL[1],       MATERIAL[1],  250,  400, 10});
  reg.push_back({"MAT_BISHOP",              &MATERIAL[2],       MATERIAL[2],  250,  420, 10});
  reg.push_back({"MAT_ROOK",                &MATERIAL[3],       MATERIAL[3],  400,  610, 10});
  reg.push_back({"MAT_QUEEN",               &MATERIAL[4],       MATERIAL[4],  800, 1250, 20});

  // --- Passed pawn rank bonus (12) ---
  reg.push_back({"PASSED_R2_MG",            &PASSED_RANK_BONUS_MG[1], PASSED_RANK_BONUS_MG[1],    0,   30,  5});
  reg.push_back({"PASSED_R3_MG",            &PASSED_RANK_BONUS_MG[2], PASSED_RANK_BONUS_MG[2],    0,   40,  5});
  reg.push_back({"PASSED_R4_MG",            &PASSED_RANK_BONUS_MG[3], PASSED_RANK_BONUS_MG[3],    0,   50,  5});
  reg.push_back({"PASSED_R5_MG",            &PASSED_RANK_BONUS_MG[4], PASSED_RANK_BONUS_MG[4],    0,  150, 10});
  reg.push_back({"PASSED_R6_MG",            &PASSED_RANK_BONUS_MG[5], PASSED_RANK_BONUS_MG[5],    0,  180, 10});
  reg.push_back({"PASSED_R7_MG",            &PASSED_RANK_BONUS_MG[6], PASSED_RANK_BONUS_MG[6],   20,  300, 15});
  reg.push_back({"PASSED_R2_EG",            &PASSED_RANK_BONUS_EG[1], PASSED_RANK_BONUS_EG[1],    0,   30,  5});
  reg.push_back({"PASSED_R3_EG",            &PASSED_RANK_BONUS_EG[2], PASSED_RANK_BONUS_EG[2],    0,   50,  5});
  reg.push_back({"PASSED_R4_EG",            &PASSED_RANK_BONUS_EG[3], PASSED_RANK_BONUS_EG[3],    0,   80, 10});
  reg.push_back({"PASSED_R5_EG",            &PASSED_RANK_BONUS_EG[4], PASSED_RANK_BONUS_EG[4],    0,  150, 10});
  reg.push_back({"PASSED_R6_EG",            &PASSED_RANK_BONUS_EG[5], PASSED_RANK_BONUS_EG[5],   20,  300, 15});
  reg.push_back({"PASSED_R7_EG",            &PASSED_RANK_BONUS_EG[6], PASSED_RANK_BONUS_EG[6],   80,  500, 20});

  // --- Pawn structure scalars (8) ---
  reg.push_back({"CONNECTED_PASSED",        &CONNECTED_PASSED,       CONNECTED_PASSED,        0,   40,  5});
  reg.push_back({"ISOLATED_PENALTY",        &ISOLATED_PENALTY,       ISOLATED_PENALTY,      -30,    0,  5});
  reg.push_back({"DOUBLED_PENALTY",         &DOUBLED_PENALTY,        DOUBLED_PENALTY,       -40,    0,  5});
  reg.push_back({"BACKWARD_PENALTY",        &BACKWARD_PENALTY,       BACKWARD_PENALTY,      -35,    0,  5});
  reg.push_back({"PROTECTED_PASSER_MG",     &PROTECTED_PASSER_MG,    PROTECTED_PASSER_MG,     0,   40,  5});
  reg.push_back({"PROTECTED_PASSER_EG",     &PROTECTED_PASSER_EG,    PROTECTED_PASSER_EG,     0,   80,  5});
  reg.push_back({"CANDIDATE_PASSER_MG",     &CANDIDATE_PASSER_MG,    CANDIDATE_PASSER_MG,     0,   30,  5});
  reg.push_back({"CANDIDATE_PASSER_EG",     &CANDIDATE_PASSER_EG,    CANDIDATE_PASSER_EG,     0,   50,  5});

  // --- Bishop pair (2) ---
  reg.push_back({"BISHOP_PAIR_MG",          &BISHOP_PAIR_MG,         BISHOP_PAIR_MG,           0,  100,  5});
  reg.push_back({"BISHOP_PAIR_EG",          &BISHOP_PAIR_EG,         BISHOP_PAIR_EG,          10,  150,  5});

  // --- Rook on file (2) ---
  reg.push_back({"ROOK_OPEN_FILE",          &ROOK_OPEN_FILE,         ROOK_OPEN_FILE,           0,   50,  5});
  reg.push_back({"ROOK_SEMI_OPEN_FILE",     &ROOK_SEMI_OPEN_FILE,    ROOK_SEMI_OPEN_FILE,      0,   40,  5});

  // --- Rook on 7th (2) ---
  reg.push_back({"ROOK_7TH_MG",             &ROOK_7TH_MG,           ROOK_7TH_MG,              0,   50,  5});
  reg.push_back({"ROOK_7TH_EG",             &ROOK_7TH_EG,           ROOK_7TH_EG,              0,   80,  5});

  // --- Rook behind passer (2) ---
  reg.push_back({"ROOK_BEHIND_OWN_EG",      &ROOK_BEHIND_OWN_PASSER_EG,   ROOK_BEHIND_OWN_PASSER_EG,    0,   50,  5});
  reg.push_back({"ROOK_BEHIND_ENEMY_EG",    &ROOK_BEHIND_ENEMY_PASSER_EG, ROOK_BEHIND_ENEMY_PASSER_EG, -50,   10,  5});

  // --- Outpost (1) ---
  reg.push_back({"OUTPOST_BONUS",           &OUTPOST_BONUS,          OUTPOST_BONUS,             0,   60,  5});

  // --- Bad bishop (2) ---
  reg.push_back({"BAD_BISHOP_MG",           &BAD_BISHOP_MG,          BAD_BISHOP_MG,           -15,    0,  1});
  reg.push_back({"BAD_BISHOP_EG",           &BAD_BISHOP_EG,          BAD_BISHOP_EG,           -15,    0,  1});

  // --- Trapped pieces (2) ---
  reg.push_back({"TRAPPED_BISHOP",          &TRAPPED_BISHOP_PENALTY, TRAPPED_BISHOP_PENALTY, -120,    0, 10});
  reg.push_back({"TRAPPED_ROOK",            &TRAPPED_ROOK_PENALTY,   TRAPPED_ROOK_PENALTY,   -100,    0, 10});

  // --- Mobility (8) ---
  reg.push_back({"MOBILITY_KNIGHT_MG",      &MOBILITY_KNIGHT_MG,     MOBILITY_KNIGHT_MG,       0,   20,  1});
  reg.push_back({"MOBILITY_KNIGHT_EG",      &MOBILITY_KNIGHT_EG,     MOBILITY_KNIGHT_EG,       0,   12,  1});
  reg.push_back({"MOBILITY_BISHOP_MG",      &MOBILITY_BISHOP_MG,     MOBILITY_BISHOP_MG,       0,   12,  1});
  reg.push_back({"MOBILITY_BISHOP_EG",      &MOBILITY_BISHOP_EG,     MOBILITY_BISHOP_EG,       0,   12,  1});
  reg.push_back({"MOBILITY_ROOK_MG",        &MOBILITY_ROOK_MG,       MOBILITY_ROOK_MG,         0,   16,  1});
  reg.push_back({"MOBILITY_ROOK_EG",        &MOBILITY_ROOK_EG,       MOBILITY_ROOK_EG,         0,   16,  1});
  reg.push_back({"MOBILITY_QUEEN_MG",       &MOBILITY_QUEEN_MG,      MOBILITY_QUEEN_MG,        0,   12,  1});
  reg.push_back({"MOBILITY_QUEEN_EG",       &MOBILITY_QUEEN_EG,      MOBILITY_QUEEN_EG,        0,   16,  1});

  // --- King safety shield (3) ---
  reg.push_back({"SHIELD_MISSING_PAWN",     &SHIELD_MISSING_PAWN,    SHIELD_MISSING_PAWN,    -60,    0,  5});
  reg.push_back({"SHIELD_ADVANCED_PAWN",    &SHIELD_ADVANCED_PAWN,   SHIELD_ADVANCED_PAWN,   -20,    0,  5});
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

  // --- Center control (2) ---
  reg.push_back({"CENTER_OCCUPATION",       &CENTER_OCCUPATION_BONUS, CENTER_OCCUPATION_BONUS,  0,   50,  5});
  reg.push_back({"CENTER_ATTACK",           &CENTER_ATTACK_BONUS,     CENTER_ATTACK_BONUS,      0,   15,  5});

  // --- Space (1) ---
  reg.push_back({"SPACE_BONUS",             &SPACE_BONUS,            SPACE_BONUS,               0,   10,  1});

  // --- Passed pawn king distance (2) ---
  reg.push_back({"PASSER_OWN_KING",         &PASSER_OWN_KING,       PASSER_OWN_KING,           0,   15,  1});
  reg.push_back({"PASSER_ENEMY_KING",       &PASSER_ENEMY_KING,     PASSER_ENEMY_KING,         0,   25,  2});

  // --- Threats (12) ---
  reg.push_back({"THREAT_P_MINOR_MG",       &THREAT_PAWN_VS_MINOR_MG,   THREAT_PAWN_VS_MINOR_MG,    0,   30,  2});
  reg.push_back({"THREAT_P_MINOR_EG",       &THREAT_PAWN_VS_MINOR_EG,   THREAT_PAWN_VS_MINOR_EG,    0,   25,  2});
  reg.push_back({"THREAT_P_ROOK_MG",        &THREAT_PAWN_VS_ROOK_MG,    THREAT_PAWN_VS_ROOK_MG,     0,   35,  2});
  reg.push_back({"THREAT_P_ROOK_EG",        &THREAT_PAWN_VS_ROOK_EG,    THREAT_PAWN_VS_ROOK_EG,     0,   25,  2});
  reg.push_back({"THREAT_P_QUEEN_MG",       &THREAT_PAWN_VS_QUEEN_MG,   THREAT_PAWN_VS_QUEEN_MG,    0,   50,  3});
  reg.push_back({"THREAT_P_QUEEN_EG",       &THREAT_PAWN_VS_QUEEN_EG,   THREAT_PAWN_VS_QUEEN_EG,    0,   35,  2});
  reg.push_back({"THREAT_N_ROOK_MG",        &THREAT_MINOR_VS_ROOK_MG,   THREAT_MINOR_VS_ROOK_MG,    0,   45,  2});
  reg.push_back({"THREAT_N_ROOK_EG",        &THREAT_MINOR_VS_ROOK_EG,   THREAT_MINOR_VS_ROOK_EG,    0,   45,  2});
  reg.push_back({"THREAT_N_QUEEN_MG",       &THREAT_MINOR_VS_QUEEN_MG,  THREAT_MINOR_VS_QUEEN_MG,   0,   55,  2});
  reg.push_back({"THREAT_N_QUEEN_EG",       &THREAT_MINOR_VS_QUEEN_EG,  THREAT_MINOR_VS_QUEEN_EG,   0,   40,  2});
  reg.push_back({"THREAT_R_QUEEN_MG",       &THREAT_ROOK_VS_QUEEN_MG,   THREAT_ROOK_VS_QUEEN_MG,    0,   30,  1});
  reg.push_back({"THREAT_R_QUEEN_EG",       &THREAT_ROOK_VS_QUEEN_EG,   THREAT_ROOK_VS_QUEEN_EG,    0,   25,  1});

  // --- Connectivity (1) ---
  reg.push_back({"CONNECTIVITY",            &CONNECTIVITY_BONUS,     CONNECTIVITY_BONUS,        0,   25,  1});
  // clang-format on

  // ---- PST entries (752) --------------------------------------------------
  // 12 arrays × 64 squares = 768, minus 16 frozen pawn squares (rank 1 + 8).
  // Bulk-registered via loop.  Name strings are heap-allocated (tuning build
  // only, so the leak is harmless).
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
void setValue(int i, int v)   { *buildRegistry()[i].ptr = v; }
int getDefault(int i)         { return buildRegistry()[i].defaultVal; }
int getMin(int i)             { return buildRegistry()[i].min; }
int getMax(int i)             { return buildRegistry()[i].max; }
int getStep(int i)            { return buildRegistry()[i].step; }

}  // namespace tuning

}  // namespace eval
}  // namespace LibreChess

#endif  // TUNING
