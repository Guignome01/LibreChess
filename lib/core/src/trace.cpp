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
  t.entries.reserve(128);

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

  float mgW = static_cast<float>(phase) / MAX_PHASE;
  float egW = static_cast<float>(MAX_PHASE - phase) / MAX_PHASE;

  // -----------------------------------------------------------------------
  // OCB scaling detection — flag stored for post-hoc application.
  //
  // evaluatePosition() applies OCB as: score = score * 3/4 after tapering.
  // The trace stores OCB as a flag; traceScore applies it as a final
  // multiplication so that integer truncation in the eval's *3/4 is
  // replicated rather than distributed across individual coefficients.
  // -----------------------------------------------------------------------
  if (phase <= 6) {
    Bitboard wb = bb.byPiece[pieceIndex('B')];
    Bitboard bbish = bb.byPiece[pieceIndex('b')];
    if (popcount(wb) == 1 && popcount(bbish) == 1) {
      bool whiteDark = (wb & DARK_SQUARES) != 0;
      bool blackDark = (bbish & DARK_SQUARES) != 0;
      if (whiteDark != blackDark) t.hasOCB = true;
    }
  }

  // -----------------------------------------------------------------------
  // Material (P, N, B, R, Q) — separate MG and EG.
  // Pawn material is pinned (not in the tuning registry), so its
  // contribution goes into the trace bias instead of a tunable entry.
  // Without this, the trace reconstruction is off by
  // 100 × (whitePawns − blackPawns) centipawns, causing the optimizer
  // to distort all other parameters to compensate.
  // -----------------------------------------------------------------------
  for (int i = 0; i < 5; ++i) {
    int wCount = popcount(bb.byPiece[i]);
    int bCount = popcount(bb.byPiece[i + 6]);
    float diff = static_cast<float>(wCount - bCount);
    int mgIdx = pIdx(&MATERIAL[i]);
    int egIdx = pIdx(&MATERIAL_EG[i]);
    if (mgIdx >= 0) t.add(mgIdx, diff * mgW);
    else            t.bias += diff * mgW * MATERIAL[i];
    if (egIdx >= 0) t.add(egIdx, diff * egW);
    else            t.bias += diff * egW * MATERIAL_EG[i];
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
  // Mirrors evalPawnStructure() in evaluation.cpp.
  // -----------------------------------------------------------------------
  Bitboard whitePawns = bb.byPiece[pieceIndex('P')];
  Bitboard blackPawns = bb.byPiece[pieceIndex('p')];
  Bitboard whitePawnAttacks = shiftNE(whitePawns) | shiftNW(whitePawns);
  Bitboard blackPawnAttacks = shiftSE(blackPawns) | shiftSW(blackPawns);
  Bitboard pawnAtks[2] = {whitePawnAttacks, blackPawnAttacks};
  Bitboard pawnsArr[2] = {whitePawns, blackPawns};

  // Accumulate pawn coefficients (multiple pawns may contribute to the same
  // parameter, e.g. two pawns on the same rank for passed bonus).
  std::unordered_map<int, float> pawnCoeffs;
  auto addPawnCoeff = [&](int idx, float c) {
    if (idx >= 0 && c != 0.0f) pawnCoeffs[idx] += c;
  };

  // Track passed pawns for king proximity trace (below).
  Bitboard passedPawns[2] = {0, 0};

  for (int c = 0; c < 2; ++c) {
    float sign = (c == 0) ? 1.0f : -1.0f;
    Color color = (c == 0) ? Color::WHITE : Color::BLACK;
    Bitboard friendly = pawnsArr[c];
    Bitboard enemy = pawnsArr[1 - c];
    Bitboard p = friendly;

    while (p) {
      Square sq = popLsb(p);
      int rank = rankOf(sq);
      int passedRank = (c == 0) ? rank : (7 - rank);
      bool doubled = isDoubled(sq, color, friendly);

      // Passed pawn (only frontmost — doubled pawn excluded).
      bool passed = isPassed(sq, color, enemy);
      if (passed && !doubled) {
        addPawnCoeff(pIdx(&PASSED_RANK_BONUS_MG[passedRank]), sign * mgW);
        addPawnCoeff(pIdx(&PASSED_RANK_BONUS_EG[passedRank]), sign * egW);
        passedPawns[c] |= squareBB(sq);

        // Protected passed pawn.
        if (squareBB(sq) & pawnAtks[c]) {
          addPawnCoeff(pIdx(&PROTECTED_PASSER_MG), sign * mgW);
          addPawnCoeff(pIdx(&PROTECTED_PASSER_EG), sign * egW);
        }
      }

      // Candidate passed pawn.
      if (!passed && !doubled) {
        // Forward file mask (same file, ranks ahead).
        int file = fileOf(sq);
        Bitboard fileMask = fileBB(file);
        Bitboard fwdFile;
        Bitboard aheadMask, behindOrLevel;
        if (c == 0) {
          fwdFile = (rank < 7)
              ? fileMask & ~((static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1)
              : static_cast<Bitboard>(0);
          aheadMask = (rank < 7)
              ? ~((static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1)
              : static_cast<Bitboard>(0);
          behindOrLevel = (static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1;
        } else {
          fwdFile = (rank > 0)
              ? fileMask & ((static_cast<Bitboard>(1) << (8 * rank)) - 1)
              : static_cast<Bitboard>(0);
          aheadMask = (rank > 0)
              ? (static_cast<Bitboard>(1) << (8 * rank)) - 1
              : static_cast<Bitboard>(0);
          behindOrLevel = ~((static_cast<Bitboard>(1) << (8 * rank)) - 1);
        }
        if (!(enemy & fwdFile)) {
          Bitboard adjFiles = adjacentFilesMask(file);
          int sentries = popcount(enemy & adjFiles & aheadMask);
          int helpers = popcount(friendly & adjFiles & behindOrLevel);
          if (helpers >= sentries) {
            addPawnCoeff(pIdx(&CANDIDATE_PASSED_MG[passedRank]), sign * mgW);
            addPawnCoeff(pIdx(&CANDIDATE_PASSED_EG[passedRank]), sign * egW);
          }
        }
      }

      // Isolated pawn.
      if (isIsolated(sq, friendly)) {
        addPawnCoeff(pIdx(&ISOLATED_PENALTY_MG), sign * mgW);
        addPawnCoeff(pIdx(&ISOLATED_PENALTY_EG), sign * egW);
      }

      // Doubled pawn.
      if (doubled) {
        addPawnCoeff(pIdx(&DOUBLED_PENALTY_EG), sign * egW);
      }

      // Backward pawn.
      if (isBackward(sq, color, friendly, enemy)) {
        addPawnCoeff(pIdx(&BACKWARD_PENALTY_MG), sign * mgW);
        addPawnCoeff(pIdx(&BACKWARD_PENALTY_EG), sign * egW);
      }

      // Connected pawn (chain or phalanx).
      {
        bool supported = (squareBB(sq) & pawnAtks[c]) != 0;
        Bitboard adjSameRank = adjacentFilesMask(fileOf(sq)) & rankBB(rank);
        bool phalanx = (friendly & adjSameRank) != 0;
        if (supported || phalanx) {
          addPawnCoeff(pIdx(&CONNECTED_BONUS_MG[passedRank]), sign * mgW);
          addPawnCoeff(pIdx(&CONNECTED_BONUS_EG[passedRank]), sign * egW);
        }
      }
    }
  }

  for (auto& pc : pawnCoeffs) t.add(pc.first, pc.second);

  // -----------------------------------------------------------------------
  // Passed pawn king proximity (EG only) — mirrors evalPassedPawnKingDist().
  // -----------------------------------------------------------------------
  {
    float ownCoeff = 0.0f, enemyCoeff = 0.0f;
    for (int c = 0; c < 2; ++c) {
      Bitboard passed = passedPawns[c];
      if (!passed) continue;
      float sign = (c == 0) ? 1.0f : -1.0f;
      Color col = (c == 0) ? Color::WHITE : Color::BLACK;
      Color ene = (c == 0) ? Color::BLACK : Color::WHITE;
      Square ownKingSq   = lsb(bb.byPiece[pieceIndex(col, PieceType::KING)]);
      Square enemyKingSq = lsb(bb.byPiece[pieceIndex(ene, PieceType::KING)]);
      while (passed) {
        Square sq = popLsb(passed);
        ownCoeff   += sign * chebyshevDistance(sq, ownKingSq);
        enemyCoeff += sign * chebyshevDistance(sq, enemyKingSq);
      }
    }
    t.add(pIdx(&PASSER_OWN_KING_DIST_EG), ownCoeff * egW);
    t.add(pIdx(&PASSER_ENEMY_KING_DIST_EG), enemyCoeff * egW);
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
  // Uses passedPawns[] (non-doubled only), matching evalRookBehindPasser().
  // -----------------------------------------------------------------------
  {
    float ownCoeff = 0.0f, enemyCoeff = 0.0f;
    Bitboard whiteRooks = bb.byPiece[pieceIndex('R')];
    Bitboard blackRooks = bb.byPiece[pieceIndex('r')];

    Bitboard wpR = passedPawns[0];
    while (wpR) {
      Square sq = popLsb(wpR);
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
    Bitboard bpR = passedPawns[1];
    while (bpR) {
      Square sq = popLsb(bpR);
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
  // Outposts — knight (same value for MG + EG) and bishop (enemy half).
  // Uses the shared isOutpostSquare() helper from evaluation.
  // -----------------------------------------------------------------------
  {
    constexpr Square SQ_D4 = 27, SQ_D5 = 35, SQ_E4 = 28, SQ_E5 = 36;
    static constexpr Bitboard ENEMY_HALF[2] = {
      rankBB(4) | rankBB(5) | rankBB(6) | rankBB(7),
      rankBB(0) | rankBB(1) | rankBB(2) | rankBB(3),
    };
    float knightCoeff = 0.0f;
    float bishopCoeff = 0.0f;

    for (int c = 0; c < 2; ++c) {
      float sign = (c == 0) ? 1.0f : -1.0f;

      // Knights.
      Bitboard kn = bb.byPiece[pieceIndex(static_cast<Color>(c), PieceType::KNIGHT)];
      while (kn) {
        Square sq = popLsb(kn);
        if (!isOutpostSquare(sq, c, pawnAtks[c], pawnsArr[1 - c])) continue;
        int bonus = 1;
        if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5) bonus = 2;
        knightCoeff += sign * bonus;
      }

      // Bishops (enemy half only).
      Bitboard bi = bb.byPiece[pieceIndex(static_cast<Color>(c), PieceType::BISHOP)];
      while (bi) {
        Square sq = popLsb(bi);
        if (!(squareBB(sq) & ENEMY_HALF[c])) continue;
        if (!isOutpostSquare(sq, c, pawnAtks[c], pawnsArr[1 - c])) continue;
        bishopCoeff += sign;
      }
    }
    t.add(pIdx(&OUTPOST_BONUS_MG), knightCoeff * mgW);
    t.add(pIdx(&OUTPOST_BONUS_EG), knightCoeff * egW);
    t.add(pIdx(&BISHOP_OUTPOST_MG), bishopCoeff * mgW);
    t.add(pIdx(&BISHOP_OUTPOST_EG), bishopCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // King safety / pawn shield + pawn storm (MG only).
  // -----------------------------------------------------------------------
  {
    std::unordered_map<int, float> shieldCoeffs;
    auto addSC = [&](int idx, float c) {
      if (idx >= 0 && c != 0.0f) shieldCoeffs[idx] += c;
    };
    float openCoeff = 0.0f;

    Bitboard allPawns = whitePawns | blackPawns;

    for (int c = 0; c < 2; ++c) {
      float sign = (c == 0) ? 1.0f : -1.0f;
      Color color = static_cast<Color>(c);
      Color enemy = static_cast<Color>(1 - c);
      bool isWhite = (c == 0);

      Bitboard kingBB_s = bb.byPiece[pieceIndex(color, PieceType::KING)];
      if (!kingBB_s) continue;
      Square kingSq = lsb(kingBB_s);
      int kingFile = fileOf(kingSq);

      if (kingFile >= 3 && kingFile <= 4) continue;

      int shieldFiles[3];
      if (kingFile <= 2) {
        shieldFiles[0] = 0; shieldFiles[1] = 1; shieldFiles[2] = 2;
      } else {
        shieldFiles[0] = 5; shieldFiles[1] = 6; shieldFiles[2] = 7;
      }

      Bitboard friendlyPawns = bb.byPiece[pieceIndex(color, PieceType::PAWN)];
      Bitboard enemyPawns    = bb.byPiece[pieceIndex(enemy, PieceType::PAWN)];

      for (int i = 0; i < 3; ++i) {
        int f = shieldFiles[i];
        Bitboard fileMask = fileBB(f);

        // Shield rank coefficient.
        Bitboard ownOnFile = friendlyPawns & fileMask;
        if (!ownOnFile) {
          addSC(pIdx(&SHIELD_RANK[3]), sign * mgW);
        } else {
          Bitboard view = isWhite ? ownOnFile : byteSwap64(ownOnFile);
          int dist = rankOf(lsb(view)) - 1;
          if (dist > 2) dist = 2;
          addSC(pIdx(&SHIELD_RANK[dist]), sign * mgW);
        }

        // Open file.
        if (f == kingFile && !(allPawns & fileMask))
          openCoeff += sign;

        // Pawn storm.
        Bitboard enemyOnFile = enemyPawns & fileMask;
        if (enemyOnFile) {
          Bitboard eView = isWhite ? enemyOnFile : byteSwap64(enemyOnFile);
          int stormRank = rankOf(lsb(eView));
          addSC(pIdx(&PAWN_STORM[stormRank]), sign * mgW);
        }
      }
    }

    for (auto& sc : shieldCoeffs) t.add(sc.first, sc.second);
    t.add(pIdx(&SHIELD_OPEN_FILE), openCoeff * mgW);
  }

  // AttackInfo — needed by mobility, threats, and king danger helpers.
  attacks::AttackInfo info = attacks::computeAll(bb);

  // -----------------------------------------------------------------------
  // Mobility — uses shared computeMobility() helper.
  // Each table entry gets a coefficient of +1 or -1 (per color/piece).
  // -----------------------------------------------------------------------
  {
    std::unordered_map<int, float> mobCoeffs;
    auto addMobCoeff = [&](int idx, float c) {
      if (idx >= 0 && c != 0.0f) mobCoeffs[idx] += c;
    };

    for (int c = 0; c < 2; ++c) {
      float sign = (c == 0) ? 1.0f : -1.0f;
      auto m = computeMobility(bb, info, c);
      addMobCoeff(pIdx(&MOB_KNIGHT_MG[m.knight]), sign * mgW);
      addMobCoeff(pIdx(&MOB_KNIGHT_EG[m.knight]), sign * egW);
      addMobCoeff(pIdx(&MOB_BISHOP_MG[m.bishop]), sign * mgW);
      addMobCoeff(pIdx(&MOB_BISHOP_EG[m.bishop]), sign * egW);
      addMobCoeff(pIdx(&MOB_ROOK_MG[m.rook]),     sign * mgW);
      addMobCoeff(pIdx(&MOB_ROOK_EG[m.rook]),     sign * egW);
      addMobCoeff(pIdx(&MOB_QUEEN_MG[m.queen]),   sign * mgW);
      addMobCoeff(pIdx(&MOB_QUEEN_EG[m.queen]),   sign * egW);
    }

    for (auto& mc : mobCoeffs) t.add(mc.first, mc.second);
  }

  // -----------------------------------------------------------------------
  // Threats — uses shared computeThreats() helper.
  // Aggregated across colors (like bishop pair / rook files).
  // -----------------------------------------------------------------------
  {
    float byPawnCoeff = 0.0f, byMinorCoeff = 0.0f;
    float byRookCoeff = 0.0f, hangingCoeff = 0.0f;
    for (int c = 0; c < 2; ++c) {
      float sign = (c == 0) ? 1.0f : -1.0f;
      auto f = computeThreats(bb, info, c);
      byPawnCoeff  += sign * f.byPawn;
      byMinorCoeff += sign * f.byMinor;
      byRookCoeff  += sign * f.byRook;
      hangingCoeff += sign * f.hanging;
    }
    t.add(pIdx(&THREAT_BY_PAWN_MG),  byPawnCoeff  * mgW);
    t.add(pIdx(&THREAT_BY_PAWN_EG),  byPawnCoeff  * egW);
    t.add(pIdx(&THREAT_BY_MINOR_MG), byMinorCoeff * mgW);
    t.add(pIdx(&THREAT_BY_MINOR_EG), byMinorCoeff * egW);
    t.add(pIdx(&THREAT_BY_ROOK_MG),  byRookCoeff  * mgW);
    t.add(pIdx(&THREAT_BY_ROOK_EG),  byRookCoeff  * egW);
    t.add(pIdx(&THREAT_HANGING_MG),  hangingCoeff * mgW);
    t.add(pIdx(&THREAT_HANGING_EG),  hangingCoeff * egW);
  }

  // -----------------------------------------------------------------------
  // King danger — uses shared computeKingDanger() helper.
  //
  // KING_SAFETY_TABLE is EVAL_FIXED (S-curve, not tunable).  Safe check
  // bonuses (EVAL_CONST, tunable) add to the attack weight before a single
  // table lookup, so all active checks share the same nonlinear gain.
  //
  // Linearization at the COMBINED operating point (totalWeight) ensures
  // exact reconstruction: bias absorbs the constant offset, and each
  // active check gets coeff = slope (the shared local derivative).
  //   slope  = (table[tw + 1] - table[tw - 1]) / 2   (central difference)
  //   bias  += sign * (table[tw] - Σ_active slope * val_i) * mgW
  //   coeff  = sign * slope * mgW                     (same for all checks)
  // Reconstructed = bias + Σ coeff_i * val_i = table[tw].  Exact.
  // -----------------------------------------------------------------------
  {
    for (int c = 0; c < 2; ++c) {
      auto d = computeKingDanger(bb, info, c);
      if (d.attackerCount < 2 || !d.hasQueen) continue;

      // sign: penalty to the attacked side (white → -1, black → +1).
      float fSign = (c == 0) ? -1.0f : 1.0f;

      // Total weight = base (EVAL_FIXED) + all active safe checks,
      // exactly mirroring evalKingDanger().
      int totalWeight = d.attackWeight;
      if (d.knightSafeCheck) totalWeight += SAFE_CHECK_KNIGHT;
      if (d.bishopSafeCheck) totalWeight += SAFE_CHECK_BISHOP;
      if (d.rookSafeCheck)   totalWeight += SAFE_CHECK_ROOK;
      if (d.queenSafeCheck)  totalWeight += SAFE_CHECK_QUEEN;

      int totalPenalty = kingDangerScore(totalWeight);

      // Shared slope at the combined operating point (central difference,
      // one-sided at table boundaries).
      int tw = totalWeight;
      if (tw < 0) tw = 0;
      if (tw >= KING_SAFETY_TABLE_SIZE) tw = KING_SAFETY_TABLE_SIZE - 1;
      float slope;
      if (tw == 0)
        slope = static_cast<float>(kingDangerScore(1) - kingDangerScore(0));
      else if (tw >= KING_SAFETY_TABLE_SIZE - 1)
        slope = static_cast<float>(kingDangerScore(KING_SAFETY_TABLE_SIZE - 1)
                                 - kingDangerScore(KING_SAFETY_TABLE_SIZE - 2));
      else
        slope = static_cast<float>(kingDangerScore(tw + 1)
                                 - kingDangerScore(tw - 1)) * 0.5f;

      // Bias absorbs: totalPenalty - Σ_active(slope * val_i).
      float checkSum = 0.0f;
      if (d.knightSafeCheck) checkSum += slope * SAFE_CHECK_KNIGHT;
      if (d.bishopSafeCheck) checkSum += slope * SAFE_CHECK_BISHOP;
      if (d.rookSafeCheck)   checkSum += slope * SAFE_CHECK_ROOK;
      if (d.queenSafeCheck)  checkSum += slope * SAFE_CHECK_QUEEN;
      t.bias += fSign * (static_cast<float>(totalPenalty) - checkSum) * mgW;

      // Each active check: coeff = slope (per-unit marginal, same for all).
      auto emitCheck = [&](bool active, int* param) {
        if (!active) return;
        t.add(pIdx(param), fSign * slope * mgW);
      };
      emitCheck(d.knightSafeCheck, &SAFE_CHECK_KNIGHT);
      emitCheck(d.bishopSafeCheck, &SAFE_CHECK_BISHOP);
      emitCheck(d.rookSafeCheck,   &SAFE_CHECK_ROOK);
      emitCheck(d.queenSafeCheck,  &SAFE_CHECK_QUEEN);
    }
  }

  // -----------------------------------------------------------------------
  // Space — uses shared countOpenFiles() + computeSpace() helpers.
  // Aggregated across colors (like bishop pair / rook files).
  // -----------------------------------------------------------------------
  {
    int openFiles = countOpenFiles(bb);
    float spaceCoeff = 0.0f;
    for (int c = 0; c < 2; ++c) {
      float sign = (c == 0) ? 1.0f : -1.0f;
      auto s = computeSpace(bb, c, openFiles);
      spaceCoeff += sign * static_cast<float>(s.bonus * s.weight * s.weight) / 16.0f;
    }
    t.add(pIdx(&SPACE_WEIGHT), spaceCoeff * mgW);
  }

  return t;
}

// ===========================================================================
// Parameter descriptor getters — metadata for the tuning registry.
//
// Two descriptor types: scalar (individual params) and PST (piece-square
// tables).  Mobility tables are loop-generated in buildRegistry() using
// the same pattern as PSTs.
// ===========================================================================

// clang-format off
const tuning::ScalarParam* tuning::scalarParams(int& count) {
  static const ScalarParam params[] = {
    // --- Material MG (4) --- pawn pinned at 100 (see eval_params.h).
    {"MAT_KNIGHT_MG",           &MATERIAL[1]},
    {"MAT_BISHOP_MG",           &MATERIAL[2]},
    {"MAT_ROOK_MG",             &MATERIAL[3]},
    {"MAT_QUEEN_MG",            &MATERIAL[4]},
    // --- Material EG (4) --- pawn pinned at 100 (see eval_params.h).
    {"MAT_KNIGHT_EG",           &MATERIAL_EG[1]},
    {"MAT_BISHOP_EG",           &MATERIAL_EG[2]},
    {"MAT_ROOK_EG",             &MATERIAL_EG[3]},
    {"MAT_QUEEN_EG",            &MATERIAL_EG[4]},
    // --- Passed pawn rank bonus (12) ---
    {"PASSED_R2_MG",            &PASSED_RANK_BONUS_MG[1]},
    {"PASSED_R3_MG",            &PASSED_RANK_BONUS_MG[2]},
    {"PASSED_R4_MG",            &PASSED_RANK_BONUS_MG[3]},
    {"PASSED_R5_MG",            &PASSED_RANK_BONUS_MG[4]},
    {"PASSED_R6_MG",            &PASSED_RANK_BONUS_MG[5]},
    {"PASSED_R7_MG",            &PASSED_RANK_BONUS_MG[6]},
    {"PASSED_R2_EG",            &PASSED_RANK_BONUS_EG[1]},
    {"PASSED_R3_EG",            &PASSED_RANK_BONUS_EG[2]},
    {"PASSED_R4_EG",            &PASSED_RANK_BONUS_EG[3]},
    {"PASSED_R5_EG",            &PASSED_RANK_BONUS_EG[4]},
    {"PASSED_R6_EG",            &PASSED_RANK_BONUS_EG[5]},
    {"PASSED_R7_EG",            &PASSED_RANK_BONUS_EG[6]},
    // --- Pawn structure scalars (3) ---
    {"ISOLATED_PENALTY_MG",     &ISOLATED_PENALTY_MG},
    {"ISOLATED_PENALTY_EG",     &ISOLATED_PENALTY_EG},
    {"DOUBLED_PENALTY_EG",      &DOUBLED_PENALTY_EG},
    // --- Backward pawn (2) ---
    {"BACKWARD_PENALTY_MG",     &BACKWARD_PENALTY_MG},
    {"BACKWARD_PENALTY_EG",     &BACKWARD_PENALTY_EG},
    // --- Connected pawns (10) ---
    {"CONNECTED_R2_MG",         &CONNECTED_BONUS_MG[1]},
    {"CONNECTED_R3_MG",         &CONNECTED_BONUS_MG[2]},
    {"CONNECTED_R4_MG",         &CONNECTED_BONUS_MG[3]},
    {"CONNECTED_R5_MG",         &CONNECTED_BONUS_MG[4]},
    {"CONNECTED_R6_MG",         &CONNECTED_BONUS_MG[5]},
    {"CONNECTED_R7_MG",         &CONNECTED_BONUS_MG[6]},
    {"CONNECTED_R2_EG",         &CONNECTED_BONUS_EG[1]},
    {"CONNECTED_R3_EG",         &CONNECTED_BONUS_EG[2]},
    {"CONNECTED_R4_EG",         &CONNECTED_BONUS_EG[3]},
    {"CONNECTED_R5_EG",         &CONNECTED_BONUS_EG[4]},
    {"CONNECTED_R6_EG",         &CONNECTED_BONUS_EG[5]},
    {"CONNECTED_R7_EG",         &CONNECTED_BONUS_EG[6]},
    // --- Candidate passed pawn (8) ---
    {"CANDIDATE_R3_MG",         &CANDIDATE_PASSED_MG[2]},
    {"CANDIDATE_R4_MG",         &CANDIDATE_PASSED_MG[3]},
    {"CANDIDATE_R5_MG",         &CANDIDATE_PASSED_MG[4]},
    {"CANDIDATE_R6_MG",         &CANDIDATE_PASSED_MG[5]},
    {"CANDIDATE_R3_EG",         &CANDIDATE_PASSED_EG[2]},
    {"CANDIDATE_R4_EG",         &CANDIDATE_PASSED_EG[3]},
    {"CANDIDATE_R5_EG",         &CANDIDATE_PASSED_EG[4]},
    {"CANDIDATE_R6_EG",         &CANDIDATE_PASSED_EG[5]},
    // --- Protected passed pawn (2) ---
    {"PROTECTED_PASSER_MG",     &PROTECTED_PASSER_MG},
    {"PROTECTED_PASSER_EG",     &PROTECTED_PASSER_EG},
    // --- Passed pawn king proximity (2) ---
    {"PASSER_OWN_KING_DIST_EG",   &PASSER_OWN_KING_DIST_EG},
    {"PASSER_ENEMY_KING_DIST_EG", &PASSER_ENEMY_KING_DIST_EG},
    // --- Bishop pair (2) ---
    {"BISHOP_PAIR_MG",          &BISHOP_PAIR_MG},
    {"BISHOP_PAIR_EG",          &BISHOP_PAIR_EG},
    // --- Rook on file (4) ---
    {"ROOK_OPEN_FILE_MG",       &ROOK_OPEN_FILE_MG},
    {"ROOK_OPEN_FILE_EG",       &ROOK_OPEN_FILE_EG},
    {"ROOK_SEMI_OPEN_FILE_MG",  &ROOK_SEMI_OPEN_FILE_MG},
    {"ROOK_SEMI_OPEN_FILE_EG",  &ROOK_SEMI_OPEN_FILE_EG},
    // --- Rook on 7th (2) ---
    {"ROOK_7TH_MG",             &ROOK_7TH_MG},
    {"ROOK_7TH_EG",             &ROOK_7TH_EG},
    // --- Rook behind passer (2) ---
    {"ROOK_BEHIND_OWN_EG",      &ROOK_BEHIND_OWN_PASSER_EG},
    {"ROOK_BEHIND_ENEMY_EG",    &ROOK_BEHIND_ENEMY_PASSER_EG},
    // --- Outpost (4) ---
    {"OUTPOST_BONUS_MG",        &OUTPOST_BONUS_MG},
    {"OUTPOST_BONUS_EG",        &OUTPOST_BONUS_EG},
    {"BISHOP_OUTPOST_MG",       &BISHOP_OUTPOST_MG},
    {"BISHOP_OUTPOST_EG",       &BISHOP_OUTPOST_EG},
    // --- Bad bishop (2) ---
    {"BAD_BISHOP_MG",           &BAD_BISHOP_MG},
    {"BAD_BISHOP_EG",           &BAD_BISHOP_EG},
    // --- Threats (8) ---
    {"THREAT_BY_PAWN_MG",       &THREAT_BY_PAWN_MG},
    {"THREAT_BY_PAWN_EG",       &THREAT_BY_PAWN_EG},
    {"THREAT_BY_MINOR_MG",      &THREAT_BY_MINOR_MG},
    {"THREAT_BY_MINOR_EG",      &THREAT_BY_MINOR_EG},
    {"THREAT_BY_ROOK_MG",       &THREAT_BY_ROOK_MG},
    {"THREAT_BY_ROOK_EG",       &THREAT_BY_ROOK_EG},
    {"THREAT_HANGING_MG",       &THREAT_HANGING_MG},
    {"THREAT_HANGING_EG",       &THREAT_HANGING_EG},
    // --- King safety shield (5) ---
    {"SHIELD_RANK_0",           &SHIELD_RANK[0]},
    {"SHIELD_RANK_1",           &SHIELD_RANK[1]},
    {"SHIELD_RANK_2",           &SHIELD_RANK[2]},
    {"SHIELD_RANK_3",           &SHIELD_RANK[3]},
    {"SHIELD_OPEN_FILE",        &SHIELD_OPEN_FILE},
    // --- Pawn storm (6 tunable: indices 2-7, 0-1 fixed at 0) ---
    {"PAWN_STORM_2",            &PAWN_STORM[2]},
    {"PAWN_STORM_3",            &PAWN_STORM[3]},
    {"PAWN_STORM_4",            &PAWN_STORM[4]},
    {"PAWN_STORM_5",            &PAWN_STORM[5]},
    {"PAWN_STORM_6",            &PAWN_STORM[6]},
    {"PAWN_STORM_7",            &PAWN_STORM[7]},
    // --- Safe check bonuses (4) ---
    {"SAFE_CHECK_KNIGHT",       &SAFE_CHECK_KNIGHT},
    {"SAFE_CHECK_BISHOP",       &SAFE_CHECK_BISHOP},
    {"SAFE_CHECK_ROOK",         &SAFE_CHECK_ROOK},
    {"SAFE_CHECK_QUEEN",        &SAFE_CHECK_QUEEN},
    // --- Space (1) ---
    {"SPACE_WEIGHT",            &SPACE_WEIGHT},
  };
  // clang-format on
  count = sizeof(params) / sizeof(params[0]);
  return params;
}

// clang-format off
const tuning::PstDef* tuning::pstDefs(int& count) {
  static const PstDef defs[] = {
    {"PST_PAWN_MG",   PST_PAWN_MG,   true},
    {"PST_KNIGHT_MG", PST_KNIGHT_MG, false},
    {"PST_BISHOP_MG", PST_BISHOP_MG, false},
    {"PST_ROOK_MG",   PST_ROOK_MG,   false},
    {"PST_QUEEN_MG",  PST_QUEEN_MG,  false},
    {"PST_KING_MG",   PST_KING_MG,   false},
    {"PST_PAWN_EG",   PST_PAWN_EG,   true},
    {"PST_KNIGHT_EG", PST_KNIGHT_EG, false},
    {"PST_BISHOP_EG", PST_BISHOP_EG, false},
    {"PST_ROOK_EG",   PST_ROOK_EG,   false},
    {"PST_QUEEN_EG",  PST_QUEEN_EG,  false},
    {"PST_KING_EG",   PST_KING_EG,   false},
  };
  // clang-format on
  count = sizeof(defs) / sizeof(defs[0]);
  return defs;
}

// ===========================================================================
// buildRegistry — constructs the full parameter list on first access.
//
// Iterates the descriptor getters to populate the registry.  Mobility
// table entries and PST entries are generated via loops (same pattern),
// avoiding repetitive manual listings.
//
// Total: scalar + mobility + PST tunable parameters.
// ===========================================================================

static std::vector<TuneEntry>& buildRegistry() {
  static std::vector<TuneEntry> reg;
  if (!reg.empty()) return reg;

  // ---- Scalar entries -----------------------------------------------------
  int nScalar;
  const auto* scalars = tuning::scalarParams(nScalar);
  for (int i = 0; i < nScalar; ++i) {
    const auto& s = scalars[i];
    reg.push_back({s.name, s.ptr, *s.ptr});
  }

  // ---- Mobility table entries (loop-generated, like PSTs) -----------------
  // 8 tables × variable size = 132 entries total.
  // Sizes mirror EVAL_FIXED values in eval_params.h (const = internal linkage,
  // not accessible via extern).
  struct ArrayInfo { const char* prefix; int* data; int size; };
  // clang-format off
  static const ArrayInfo mobArrays[] = {
    {"MOB_KNIGHT_MG", MOB_KNIGHT_MG, 9},
    {"MOB_KNIGHT_EG", MOB_KNIGHT_EG, 9},
    {"MOB_BISHOP_MG", MOB_BISHOP_MG, 14},
    {"MOB_BISHOP_EG", MOB_BISHOP_EG, 14},
    {"MOB_ROOK_MG",   MOB_ROOK_MG,   15},
    {"MOB_ROOK_EG",   MOB_ROOK_EG,   15},
    {"MOB_QUEEN_MG",  MOB_QUEEN_MG,  28},
    {"MOB_QUEEN_EG",  MOB_QUEEN_EG,  28},
  };
  // clang-format on
  for (const auto& a : mobArrays) {
    for (int i = 0; i < a.size; ++i) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%s_%d", a.prefix, i);
      char* name = new char[std::strlen(buf) + 1];
      std::strcpy(name, buf);
      reg.push_back({name, &a.data[i], a.data[i]});
    }
  }

  // ---- PST entries --------------------------------------------------------
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
      reg.push_back({name, &pd.data[sq], pd.data[sq]});
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

}  // namespace tuning

}  // namespace eval
}  // namespace LibreChess

#endif  // TUNING
