#ifdef TUNING

#include "trace.h"
#include "attacks.h"
#include "evaluation.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace LibreChess {
namespace eval {

// ===========================================================================
// Name → index map (built once from the tuning registry)
// ===========================================================================

static std::unordered_map<std::string, int> paramMap;

void buildParamMap() {
  int n = tuning::paramCount();
  paramMap.reserve(n);
  for (int i = 0; i < n; ++i) {
    paramMap[tuning::getName(i)] = i;
  }
}

int findParam(const char* name) {
  auto it = paramMap.find(name);
  return (it != paramMap.end()) ? it->second : -1;
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
  // OCB scaling detection (applies only to EG coefficients).
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

  float mgW = mgWeight;
  float egW = egWeight * ocbScale;
  float bothW = mgW + egW;  // For params using same value for MG and EG.

  // -----------------------------------------------------------------------
  // Material (MATERIAL[1..4]).
  // -----------------------------------------------------------------------
  static int matIdx[4] = {-1, -1, -1, -1};
  static bool matInit = false;
  if (!matInit) {
    matIdx[0] = findParam("MAT_KNIGHT");
    matIdx[1] = findParam("MAT_BISHOP");
    matIdx[2] = findParam("MAT_ROOK");
    matIdx[3] = findParam("MAT_QUEEN");
    matInit = true;
  }
  for (int i = 0; i < 4; ++i) {
    if (matIdx[i] < 0) continue;
    int pt = i + 1;
    int wCount = popcount(bb.byPiece[pt]);
    int bCount = popcount(bb.byPiece[pt + 6]);
    t.add(matIdx[i], static_cast<float>(wCount - bCount) * bothW);
  }

  // -----------------------------------------------------------------------
  // PST — per piece, per square, separate MG and EG.
  // -----------------------------------------------------------------------
  static const char* pstPrefixes[12] = {
    "PST_PAWN_MG", "PST_KNIGHT_MG", "PST_BISHOP_MG",
    "PST_ROOK_MG", "PST_QUEEN_MG",  "PST_KING_MG",
    "PST_PAWN_EG", "PST_KNIGHT_EG", "PST_BISHOP_EG",
    "PST_ROOK_EG", "PST_QUEEN_EG",  "PST_KING_EG",
  };

  static int pstIdx[12][64];
  static bool pstInit = false;
  if (!pstInit) {
    for (int tbl = 0; tbl < 12; ++tbl) {
      for (int sq = 0; sq < 64; ++sq) {
        char name[32];
        std::snprintf(name, sizeof(name), "%s_%d", pstPrefixes[tbl], sq);
        pstIdx[tbl][sq] = findParam(name);
      }
    }
    pstInit = true;
  }

  for (int pieceType = 0; pieceType < 6; ++pieceType) {
    int mgTable = pieceType;
    int egTable = pieceType + 6;

    Bitboard white = bb.byPiece[pieceType];
    while (white) {
      Square sq = popLsb(white);
      if (pstIdx[mgTable][sq] >= 0) t.add(pstIdx[mgTable][sq], mgW);
      if (pstIdx[egTable][sq] >= 0) t.add(pstIdx[egTable][sq], egW);
    }

    Bitboard black = bb.byPiece[pieceType + 6];
    while (black) {
      Square sq = popLsb(black);
      int mirSq = sq ^ 56;
      if (pstIdx[mgTable][mirSq] >= 0) t.add(pstIdx[mgTable][mirSq], -mgW);
      if (pstIdx[egTable][mirSq] >= 0) t.add(pstIdx[egTable][mirSq], -egW);
    }
  }

  // -----------------------------------------------------------------------
  // Pawn structure — uses the core eval helpers (isPassed, isIsolated, etc.)
  // -----------------------------------------------------------------------
  Bitboard whitePawns = bb.byPiece[0];
  Bitboard blackPawns = bb.byPiece[6];
  Bitboard whitePawnAttacks = shiftNE(whitePawns) | shiftNW(whitePawns);
  Bitboard blackPawnAttacks = shiftSE(blackPawns) | shiftSW(blackPawns);

  static int passedMgIdx[8] = {}, passedEgIdx[8] = {};
  static int connectedIdx = -1, isolatedIdx = -1;
  static int doubledIdx = -1, backwardIdx = -1;
  static int protPassMgIdx = -1, protPassEgIdx = -1;
  static int candPassMgIdx = -1, candPassEgIdx = -1;
  static bool pawnInit = false;
  if (!pawnInit) {
    const char* mgNames[8] = {nullptr, "PASSED_R2_MG", "PASSED_R3_MG",
        "PASSED_R4_MG", "PASSED_R5_MG", "PASSED_R6_MG", "PASSED_R7_MG", nullptr};
    const char* egNames[8] = {nullptr, "PASSED_R2_EG", "PASSED_R3_EG",
        "PASSED_R4_EG", "PASSED_R5_EG", "PASSED_R6_EG", "PASSED_R7_EG", nullptr};
    for (int r = 0; r < 8; ++r) {
      passedMgIdx[r] = mgNames[r] ? findParam(mgNames[r]) : -1;
      passedEgIdx[r] = egNames[r] ? findParam(egNames[r]) : -1;
    }
    connectedIdx  = findParam("CONNECTED_PASSED");
    isolatedIdx   = findParam("ISOLATED_PENALTY");
    doubledIdx    = findParam("DOUBLED_PENALTY");
    backwardIdx   = findParam("BACKWARD_PENALTY");
    protPassMgIdx = findParam("PROTECTED_PASSER_MG");
    protPassEgIdx = findParam("PROTECTED_PASSER_EG");
    candPassMgIdx = findParam("CANDIDATE_PASSER_MG");
    candPassEgIdx = findParam("CANDIDATE_PASSER_EG");
    pawnInit = true;
  }

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
      addPawnCoeff(passedMgIdx[rank], mgW);
      addPawnCoeff(passedEgIdx[rank], egW);
      whitePassedFiles |= 1 << (sq & 7);
      if (squareBB(sq) & whitePawnAttacks) {
        addPawnCoeff(protPassMgIdx, mgW);
        addPawnCoeff(protPassEgIdx, egW);
      }
    } else {
      Bitboard mask = 0;
      int file = sq & 7;
      for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
        for (int r = rank + 1; r <= 7; ++r) mask |= squareBB(r * 8 + f);
      if (popcount(blackPawns & mask) == 1) {
        addPawnCoeff(candPassMgIdx, mgW);
        addPawnCoeff(candPassEgIdx, egW);
      }
    }
    if (isIsolated(sq, whitePawns))
      addPawnCoeff(isolatedIdx, bothW);
    if (isDoubled(sq, Color::WHITE, whitePawns))
      addPawnCoeff(doubledIdx, bothW);
    if (isBackward(sq, Color::WHITE, whitePawns, blackPawnAttacks))
      addPawnCoeff(backwardIdx, bothW);
  }

  Bitboard bp = blackPawns;
  while (bp) {
    Square sq = popLsb(bp);
    int rank = sq / 8;

    if (isPassed(sq, Color::BLACK, whitePawns)) {
      int mirRank = 7 - rank;
      addPawnCoeff(passedMgIdx[mirRank], -mgW);
      addPawnCoeff(passedEgIdx[mirRank], -egW);
      blackPassedFiles |= 1 << (sq & 7);
      if (squareBB(sq) & blackPawnAttacks) {
        addPawnCoeff(protPassMgIdx, -mgW);
        addPawnCoeff(protPassEgIdx, -egW);
      }
    } else {
      Bitboard mask = 0;
      int file = sq & 7;
      for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
        for (int r = rank - 1; r >= 0; --r) mask |= squareBB(r * 8 + f);
      if (popcount(whitePawns & mask) == 1) {
        addPawnCoeff(candPassMgIdx, -mgW);
        addPawnCoeff(candPassEgIdx, -egW);
      }
    }
    if (isIsolated(sq, blackPawns))
      addPawnCoeff(isolatedIdx, -bothW);
    if (isDoubled(sq, Color::BLACK, blackPawns))
      addPawnCoeff(doubledIdx, -bothW);
    if (isBackward(sq, Color::BLACK, blackPawns, whitePawnAttacks))
      addPawnCoeff(backwardIdx, -bothW);
  }

  for (int f = 0; f < 7; ++f) {
    if ((whitePassedFiles >> f & 1) && (whitePassedFiles >> (f + 1) & 1))
      addPawnCoeff(connectedIdx, bothW);
    if ((blackPassedFiles >> f & 1) && (blackPassedFiles >> (f + 1) & 1))
      addPawnCoeff(connectedIdx, -bothW);
  }

  for (auto& pc : pawnCoeffs) t.add(pc.first, pc.second);

  // -----------------------------------------------------------------------
  // Passed pawn king distance (EG only).
  // -----------------------------------------------------------------------
  static int passerOwnIdx = -1, passerEnemyIdx = -1;
  static bool pkdInit = false;
  if (!pkdInit) {
    passerOwnIdx   = findParam("PASSER_OWN_KING");
    passerEnemyIdx = findParam("PASSER_ENEMY_KING");
    pkdInit = true;
  }

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
    t.add(passerOwnIdx, ownCoeff * egW);
    t.add(passerEnemyIdx, enemyCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Bishop pair (separate MG/EG).
  // -----------------------------------------------------------------------
  static int bpMgIdx = -1, bpEgIdx = -1;
  static bool bpInit = false;
  if (!bpInit) {
    bpMgIdx = findParam("BISHOP_PAIR_MG");
    bpEgIdx = findParam("BISHOP_PAIR_EG");
    bpInit = true;
  }
  {
    int wBP = (popcount(bb.byPiece[2]) >= 2) ? 1 : 0;
    int bBP = (popcount(bb.byPiece[8]) >= 2) ? 1 : 0;
    float diff = static_cast<float>(wBP - bBP);
    t.add(bpMgIdx, diff * mgW);
    t.add(bpEgIdx, diff * egW);
  }

  // -----------------------------------------------------------------------
  // Bad bishop (separate MG/EG).
  // -----------------------------------------------------------------------
  static int badBishMgIdx = -1, badBishEgIdx = -1;
  static bool bbInit = false;
  if (!bbInit) {
    badBishMgIdx = findParam("BAD_BISHOP_MG");
    badBishEgIdx = findParam("BAD_BISHOP_EG");
    bbInit = true;
  }
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
    t.add(badBishMgIdx, mgCoeff * mgW);
    t.add(badBishEgIdx, egCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Rook on file (same value for MG + EG).
  // -----------------------------------------------------------------------
  static int rookOpenIdx = -1, rookSemiIdx = -1;
  static bool rfInit = false;
  if (!rfInit) {
    rookOpenIdx = findParam("ROOK_OPEN_FILE");
    rookSemiIdx = findParam("ROOK_SEMI_OPEN_FILE");
    rfInit = true;
  }
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
    t.add(rookOpenIdx, openCoeff * bothW);
    t.add(rookSemiIdx, semiCoeff * bothW);
  }

  // -----------------------------------------------------------------------
  // Rook on 7th (separate MG/EG).
  // -----------------------------------------------------------------------
  static int r7MgIdx = -1, r7EgIdx = -1;
  static bool r7Init = false;
  if (!r7Init) {
    r7MgIdx = findParam("ROOK_7TH_MG");
    r7EgIdx = findParam("ROOK_7TH_EG");
    r7Init = true;
  }
  {
    float coeff = 0.0f;
    if ((bb.byPiece[3] & rankBB(6)) && (bb.byPiece[11] & rankBB(7)))
      coeff += popcount(bb.byPiece[3] & rankBB(6));
    if ((bb.byPiece[9] & rankBB(1)) && (bb.byPiece[5] & rankBB(0)))
      coeff -= popcount(bb.byPiece[9] & rankBB(1));
    t.add(r7MgIdx, coeff * mgW);
    t.add(r7EgIdx, coeff * egW);
  }

  // -----------------------------------------------------------------------
  // Rook behind passer (EG only).
  // -----------------------------------------------------------------------
  static int rbpOwnIdx = -1, rbpEnemyIdx = -1;
  static bool rbpInit = false;
  if (!rbpInit) {
    rbpOwnIdx   = findParam("ROOK_BEHIND_OWN_EG");
    rbpEnemyIdx = findParam("ROOK_BEHIND_ENEMY_EG");
    rbpInit = true;
  }
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
    t.add(rbpOwnIdx, ownCoeff * egW);
    t.add(rbpEnemyIdx, enemyCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Knight outposts (same value for MG + EG).
  // -----------------------------------------------------------------------
  static int outpostIdx = -1;
  static bool outInit = false;
  if (!outInit) {
    outpostIdx = findParam("OUTPOST_BONUS");
    outInit = true;
  }
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
    t.add(outpostIdx, coeff * bothW);
  }

  // -----------------------------------------------------------------------
  // King safety / pawn shield (MG only).
  // -----------------------------------------------------------------------
  static int shieldMissingIdx = -1, shieldAdvancedIdx = -1;
  static int shieldOpenIdx = -1;
  static bool ksInit = false;
  if (!ksInit) {
    shieldMissingIdx  = findParam("SHIELD_MISSING_PAWN");
    shieldAdvancedIdx = findParam("SHIELD_ADVANCED_PAWN");
    shieldOpenIdx     = findParam("SHIELD_OPEN_FILE");
    ksInit = true;
  }
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

    t.add(shieldMissingIdx, missingCoeff * mgW);
    t.add(shieldAdvancedIdx, advancedCoeff * mgW);
    t.add(shieldOpenIdx, openCoeff * mgW);
  }

  // -----------------------------------------------------------------------
  // Center control (same value for MG + EG).
  // -----------------------------------------------------------------------
  static int ccOccupyIdx = -1, ccAttackIdx = -1;
  static bool ccInit = false;
  if (!ccInit) {
    ccOccupyIdx = findParam("CENTER_OCCUPATION");
    ccAttackIdx = findParam("CENTER_ATTACK");
    ccInit = true;
  }
  {
    int wOcc = popcount(whitePawns & CENTER_MASK);
    int bOcc = popcount(blackPawns & CENTER_MASK);
    Bitboard wAtk = shiftNE(whitePawns) | shiftNW(whitePawns);
    Bitboard bAtk = shiftSE(blackPawns) | shiftSW(blackPawns);
    int wAtt = popcount(wAtk & CENTER_MASK);
    int bAtt = popcount(bAtk & CENTER_MASK);

    t.add(ccOccupyIdx, static_cast<float>(wOcc - bOcc) * bothW);
    t.add(ccAttackIdx, static_cast<float>(wAtt - bAtt) * bothW);
  }

  // -----------------------------------------------------------------------
  // Space (same value for MG + EG).
  // -----------------------------------------------------------------------
  static int spaceIdx = -1;
  static bool spInit = false;
  if (!spInit) { spaceIdx = findParam("SPACE_BONUS"); spInit = true; }
  {
    Bitboard bAtk = shiftSE(blackPawns) | shiftSW(blackPawns);
    Bitboard wAtk = shiftNE(whitePawns) | shiftNW(whitePawns);
    int ws = popcount(WHITE_SPACE_ZONE & ~bAtk);
    int bs = popcount(BLACK_SPACE_ZONE & ~wAtk);
    t.add(spaceIdx, static_cast<float>(ws - bs) * bothW);
  }

  // -----------------------------------------------------------------------
  // Trapped pieces (MG only).
  // -----------------------------------------------------------------------
  static int trappedBishIdx = -1, trappedRookIdx = -1;
  static bool trapInit = false;
  if (!trapInit) {
    trappedBishIdx = findParam("TRAPPED_BISHOP");
    trappedRookIdx = findParam("TRAPPED_ROOK");
    trapInit = true;
  }
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
    t.add(trappedBishIdx, bishCoeff * mgW);

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
    t.add(trappedRookIdx, rookCoeff * mgW);
  }

  // -----------------------------------------------------------------------
  // Mobility (separate MG/EG per piece type) — requires attack info.
  // -----------------------------------------------------------------------
  static int mobNMg = -1, mobNEg = -1, mobBMg = -1, mobBEg = -1;
  static int mobRMg = -1, mobREg = -1, mobQMg = -1, mobQEg = -1;
  static bool mobInit = false;
  if (!mobInit) {
    mobNMg = findParam("MOBILITY_KNIGHT_MG");
    mobNEg = findParam("MOBILITY_KNIGHT_EG");
    mobBMg = findParam("MOBILITY_BISHOP_MG");
    mobBEg = findParam("MOBILITY_BISHOP_EG");
    mobRMg = findParam("MOBILITY_ROOK_MG");
    mobREg = findParam("MOBILITY_ROOK_EG");
    mobQMg = findParam("MOBILITY_QUEEN_MG");
    mobQEg = findParam("MOBILITY_QUEEN_EG");
    mobInit = true;
  }

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
    t.add(mobNMg, nCoeff * mgW); t.add(mobNEg, nCoeff * egW);
    t.add(mobBMg, bCoeff * mgW); t.add(mobBEg, bCoeff * egW);
    t.add(mobRMg, rCoeff * mgW); t.add(mobREg, rCoeff * egW);
    t.add(mobQMg, qCoeff * mgW); t.add(mobQEg, qCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // Connectivity (same value for MG + EG).
  // -----------------------------------------------------------------------
  static int connIdx = -1;
  static bool conInit = false;
  if (!conInit) { connIdx = findParam("CONNECTIVITY"); conInit = true; }
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
    t.add(connIdx, coeff * bothW);
  }

  // -----------------------------------------------------------------------
  // Threats (separate MG/EG, 6 threat types).
  // -----------------------------------------------------------------------
  static int thPMinMg = -1, thPMinEg = -1, thPRkMg = -1, thPRkEg = -1;
  static int thPQnMg = -1, thPQnEg = -1, thNRkMg = -1, thNRkEg = -1;
  static int thNQnMg = -1, thNQnEg = -1, thRQnMg = -1, thRQnEg = -1;
  static bool thInit = false;
  if (!thInit) {
    thPMinMg = findParam("THREAT_P_MINOR_MG");
    thPMinEg = findParam("THREAT_P_MINOR_EG");
    thPRkMg  = findParam("THREAT_P_ROOK_MG");
    thPRkEg  = findParam("THREAT_P_ROOK_EG");
    thPQnMg  = findParam("THREAT_P_QUEEN_MG");
    thPQnEg  = findParam("THREAT_P_QUEEN_EG");
    thNRkMg  = findParam("THREAT_N_ROOK_MG");
    thNRkEg  = findParam("THREAT_N_ROOK_EG");
    thNQnMg  = findParam("THREAT_N_QUEEN_MG");
    thNQnEg  = findParam("THREAT_N_QUEEN_EG");
    thRQnMg  = findParam("THREAT_R_QUEEN_MG");
    thRQnEg  = findParam("THREAT_R_QUEEN_EG");
    thInit = true;
  }
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
    t.add(thPMinMg, pMin * mgW); t.add(thPMinEg, pMin * egW);
    t.add(thPRkMg, pRk * mgW);  t.add(thPRkEg, pRk * egW);
    t.add(thPQnMg, pQn * mgW);  t.add(thPQnEg, pQn * egW);
    t.add(thNRkMg, nRk * mgW);  t.add(thNRkEg, nRk * egW);
    t.add(thNQnMg, nQn * mgW);  t.add(thNQnEg, nQn * egW);
    t.add(thRQnMg, rQn * mgW);  t.add(thRQnEg, rQn * egW);
  }

  // -----------------------------------------------------------------------
  // King danger table (MG only).
  // -----------------------------------------------------------------------
  static int kdTableIdx[KING_DANGER_TABLE_SIZE];
  static bool kdInit = false;
  if (!kdInit) {
    kdTableIdx[0] = -1;  // TABLE[0] = 0, fixed.
    const char* names[KING_DANGER_TABLE_SIZE] = {
      nullptr, "KD_TABLE_1",  "KD_TABLE_2",  "KD_TABLE_3",
      "KD_TABLE_4",  "KD_TABLE_5",  "KD_TABLE_6",
      "KD_TABLE_7",  "KD_TABLE_8",  "KD_TABLE_9",
      "KD_TABLE_10", "KD_TABLE_11", "KD_TABLE_12"
    };
    for (int i = 1; i < KING_DANGER_TABLE_SIZE; ++i)
      kdTableIdx[i] = findParam(names[i]);
    kdInit = true;
  }

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
    if (idx > 0 && kdTableIdx[idx] >= 0)
      t.add(kdTableIdx[idx], static_cast<float>(sign) * mgW);
  }

  return t;
}

}  // namespace eval
}  // namespace LibreChess

#endif  // TUNING
