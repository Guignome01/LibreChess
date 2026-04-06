#include "evaluation.h"
#include "attacks.h"
#include "piece.h"
#include "utils.h"
#include "zobrist.h"

#include <cstring>       // memset
#include <new>           // nothrow

// Under TUNING, eval constants are mutable with external linkage so the
// tuner (trace.cpp) can read/write them at runtime.  Production builds
// keep full constexpr optimisation with file-local linkage.
//
//   EVAL_CONST  — tunable parameters (mutable in tuning builds).
//   EVAL_FIXED  — non-tunable constants that trace.cpp must see.
#ifdef TUNING
#define EVAL_CONST              // mutable, external linkage
#define EVAL_FIXED const        // immutable, external linkage
// Tuner accesses arrays via int* (TuneEntry), so element types stay int.
#define PST_ELEM int
#define MAT_ELEM int
#else
#define EVAL_CONST static constexpr   // immutable, file-local
#define EVAL_FIXED static constexpr   // immutable, file-local
// Production: narrow element types for smaller .bss / Flash footprint.
#define PST_ELEM int8_t
#define MAT_ELEM int16_t
#endif

namespace {

using namespace LibreChess;
using piece::pieceIndex;

// ---------------------------------------------------------------------------
// Pawn-structure masks — file-local, initialized lazily.
//
// Passed and forward masks are stored for White only; Black masks are
// derived at query time via vertical mirror (byteSwap64 + sq^56).  This
// halves the .bss footprint (−1,024 bytes).  The derivation cost is
// negligible because pawn structure is cached in the pawn hash table
// (~95% hit rate).
//
// The isolated-pawn mask (adjacent files) is derived inline from
// adjacentFilesMask(), eliminating a dedicated 64-byte table.
// ---------------------------------------------------------------------------

Bitboard pawnPassedMask[64] = {};    // White-only; Black via mirror
Bitboard pawnForwardMask[64] = {};   // White-only; Black via mirror

// Vertical mirror — swaps ranks (rank 1↔8, 2↔7, etc.).
static inline uint64_t byteSwap64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_bswap64(v);
#else
  v = ((v >> 8) & 0x00FF00FF00FF00FFULL) | ((v & 0x00FF00FF00FF00FFULL) << 8);
  v = ((v >> 16) & 0x0000FFFF0000FFFFULL) | ((v & 0x0000FFFF0000FFFFULL) << 16);
  return (v >> 32) | (v << 32);
#endif
}

Bitboard adjacentFilesMask(int file) {
  Bitboard mask = 0;
  if (file > 0) mask |= fileBB(file - 1);
  if (file < 7) mask |= fileBB(file + 1);
  return mask;
}

// Passed-pawn mask for either color.  White: direct lookup.
// Black: mirror the mask for the vertically reflected square.
inline Bitboard passedMask(Color c, Square sq) {
  if (c == Color::WHITE) return pawnPassedMask[sq];
  return byteSwap64(pawnPassedMask[sq ^ 56]);
}

// Forward-file mask for either color (same file, ranks ahead).
inline Bitboard forwardMask(Color c, Square sq) {
  if (c == Color::WHITE) return pawnForwardMask[sq];
  return byteSwap64(pawnForwardMask[sq ^ 56]);
}

void initPawnMasksImpl() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  for (Square sq = 0; sq < 64; ++sq) {
    const int rank = rankOf(sq);
    const int file = fileOf(sq);

    Bitboard sameAndAdjacentFiles = fileBB(file);
    if (file > 0) sameAndAdjacentFiles |= fileBB(file - 1);
    if (file < 7) sameAndAdjacentFiles |= fileBB(file + 1);

    Bitboard whitePassed = 0;
    Bitboard whiteForward = 0;

    for (int r = rank + 1; r < 8; ++r) {
      Bitboard rankMask = rankBB(r);
      whitePassed |= sameAndAdjacentFiles & rankMask;
      whiteForward |= fileBB(file) & rankMask;
    }

    pawnPassedMask[sq] = whitePassed;
    pawnForwardMask[sq] = whiteForward;
  }
}

}  // anonymous namespace

// ===========================================================================
// eval — evaluation constants, helpers, and public API
// ===========================================================================

namespace LibreChess {
namespace eval {

// ---------------------------------------------------------------------------
// Material values indexed by piece type offset (P=0 N=1 B=2 R=3 Q=4 K=5).
// Separate MG and EG tables allow the tuner to find phase-optimal piece
// values independently.  Pawns gain value in endgames (promotion proximity),
// knights lose value (fewer targets), rooks gain (open board).
//
// MATERIAL (= MATERIAL_MG) defines the centipawn unit (PAWN MG = 100 fixed).
// materialValue() returns MATERIAL[idx] for SEE, lazy eval, delta pruning.
//
// Reference: https://www.chessprogramming.org/Material
// ---------------------------------------------------------------------------
EVAL_CONST MAT_ELEM MATERIAL[] = {87, 386, 419, 549, 1093, 0};
EVAL_CONST MAT_ELEM MATERIAL_EG[] = {80, 230, 250, 468, 956, 0};

// ---------------------------------------------------------------------------
// Piece-square tables — centipawns, LERF order (a1=0, h8=63).
// White's perspective; black mirrors via (sq ^ 56).
// Based on the simplified evaluation function (CPW / Tomasz Michniewski).
//
// Midgame (MG) tables: king should hide behind pawns, minor pieces want
// center control. Endgame (EG) tables: king should be active and central,
// passed pawns and advancement matter more.
// ---------------------------------------------------------------------------

// clang-format off

// --- Midgame PSTs ---

EVAL_CONST PST_ELEM PST_PAWN_MG[64] = {
     0,   0,   0,   0,   0,   0,   0,   0,
    -4,  -5,  -6,   0,   2,  18,  15,   4,
    -3,  -9,   3,   0,   8,  -4,   6,   0,
    -7,  -9,  -4,   4,   3,  -2, -11, -13,
     2,   0,  -1,   1,   5,   2,   0,  -1,
     1,   0,   1,  -1,   0,   2,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KNIGHT_MG[64] = {
     0,  -8,  -1,  -1,  -2,   0,  -8,   0,
    -1,   0,   0,   7,   6,   0,   0,   0,
    -5,  -1,   3,   2,   2,   9,   2,  -3,
    -3,   0,   2,   0,   5,   1,   0,  -4,
     0,   2,  -1,   4,  -3,   1,  -4,   0,
     0,   0,   0,   1,   1,   1,   0,   0,
    -2,  -1,   1,   0,   0,   0,   0,  -1,
    -2,   0,   0,   0,   0,  -1,   0,  -1
};

EVAL_CONST PST_ELEM PST_BISHOP_MG[64] = {
     0,  -1,  -3,  -1,   0, -13,   0,  -1,
     0,   9,   0,   1,   4,   1,  19,   0,
     0,   1,   3,  -2,   3,   4,   1,  -1,
    -1,  -1,  -3,   2,   2,  -3,   0,  -2,
    -1,  -4,  -1,   0,   1,  -1,  -1,  -1,
    -1,   0,   0,   0,   1,   1,   0,   0,
    -2,  -2,  -1,   0,  -1,   0,  -1,  -2,
    -1,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_ROOK_MG[64] = {
     0,   1,   3,   5,   6,  15,  -5,  -8,
    -2,  -1,  -1,  -1,  -1,   0,   0,  -2,
    -2,   0,  -1,   0,   0,   0,   0,   0,
    -1,  -1,  -1,  -1,  -1,   0,   0,   0,
    -1,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
    -1,  -2,   0,   0,  -1,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_QUEEN_MG[64] = {
     0,  -1,   0,  13,   1,  -1,   0,   0,
    -1,   0,   3,   6,   8,   1,   0,   0,
    -2,   0,   0,   1,   1,   2,   1,  -1,
    -5,  -1,  -2,  -2,   0,   0,   0,   0,
    -1,  -2,  -1,  -3,  -1,   0,  -1,  -2,
    -1,  -1,   0,   0,   1,   1,   1,   2,
    -2,  -7,  -1,   0,  -1,   1,   0,   1,
    -1,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KING_MG[64] = {
    -1,   2,   1,  -8,  -8,  -1,  19,  -1,
     0,   0,   0,  -5,  -6,  -1,   4,   2,
     0,   0,   0,  -1,  -1,   1,   1,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   1,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

// --- Endgame PSTs ---

EVAL_CONST PST_ELEM PST_PAWN_EG[64] = {
     0,   0,   0,   0,   0,   0,   0,   0,
    -2,  -1,   2,   0,   1,   3,  -2,  -6,
    -5,  -3,  -1,   0,   2,   0,  -3,  -5,
     1,   1,  -2,  -2,  -1,  -2,   0,  -2,
     6,   3,   1,  -3,  -1,   0,   2,   2,
     5,   2,   1,  -2,  -2,   0,   0,   1,
     1,   1,   0,  -1,  -1,  -1,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KNIGHT_EG[64] = {
     0,  -1,   0,   0,   0,   0,  -2,   0,
     0,   0,  -1,   1,   1,   0,   0,   0,
    -1,   0,  -1,   1,   1,  -1,   0,  -1,
    -1,   0,   2,   1,   3,   0,   0,  -1,
     0,   1,   1,   2,   1,   1,   0,   0,
     0,   0,   1,   1,   0,   0,   0,   0,
    -1,  -1,   0,   0,   0,   0,   0,  -1,
    -1,   0,   0,   0,   0,  -1,   0,  -1
};

EVAL_CONST PST_ELEM PST_BISHOP_EG[64] = {
    -1,   0,  -2,   0,   0,  -2,   0,  -1,
     0,   0,  -1,   1,   1,   0,   2,   0,
     0,   1,   1,   1,   3,   1,   0,   0,
     0,   0,   0,   0,   0,   1,   0,  -1,
     0,   0,   0,   0,   0,  -1,   0,  -1,
     0,   0,   0,   0,   0,   1,   0,   0,
    -1,  -1,  -1,   0,   0,   0,   0,  -1,
     0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_ROOK_EG[64] = {
     2,   1,   1,   2,   1,   4,  -1,  -3,
    -1,   0,   0,   0,   0,   0,   0,  -1,
    -1,   0,  -1,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,  -1,
     0,   0,   1,   0,   0,   0,   0,   0,
     1,   1,   0,   0,   0,   0,   0,  -1,
    -1,   0,   0,   0,  -1,   0,   0,  -1,
     1,   1,   1,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_QUEEN_EG[64] = {
     0,   0,   0,  -1,   0,   0,   0,   0,
     0,   0,   0,   1,   1,   0,   0,   0,
    -1,  -1,   0,   0,   0,   1,   1,   0,
    -1,   0,   0,   0,   0,   0,   0,   0,
    -1,   0,   0,   0,   0,   0,   0,   0,
    -1,   0,   0,   0,   1,   0,   1,   1,
    -1,  -1,   0,   0,   0,   1,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KING_EG[64] = {
    -1,   0,   0,  -5,  -5,  -2,  -5,  -7,
    -1,   0,   1,  -1,   0,   5,   4,  -2,
    -1,   0,   2,   0,   1,   5,   2,  -1,
    -1,   0,   1,   0,   1,   3,   1,  -1,
    -1,   0,   1,   0,   0,   3,   2,   0,
     0,   1,   1,   0,   0,   2,   2,   0,
     0,   0,   0,  -1,   0,   1,   1,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

// clang-format on

// Indexed by piece type offset (PAWN=0 .. KING=5).
// File-local PST pointer lookup tables.
static const PST_ELEM* const PST_MG[6] = {
    PST_PAWN_MG, PST_KNIGHT_MG, PST_BISHOP_MG,
    PST_ROOK_MG, PST_QUEEN_MG,  PST_KING_MG};

static const PST_ELEM* const PST_EG[6] = {
    PST_PAWN_EG, PST_KNIGHT_EG, PST_BISHOP_EG,
    PST_ROOK_EG, PST_QUEEN_EG,  PST_KING_EG};

// ---------------------------------------------------------------------------
// Flat PSQT lookup tables — pre-combined material + PST + color sign.
//
// White pieces (indices 0–5): PSQT[idx][sq] = +MATERIAL[idx] + PST[idx][sq]
// Black pieces (indices 6–11): PSQT[idx][sq] = -(MATERIAL[idx-6] + PST[idx-6][sq ^ 56])
//
// Eliminates 3 conditional branches and pointer indirection per
// pieceSquareMG/EG call.  Built lazily on first use; TUNING builds
// invalidate via invalidatePSQT() after parameter changes.
//
// Reference: https://www.chessprogramming.org/Piece-Square_Tables
// ---------------------------------------------------------------------------

static int16_t PSQT_MG[12][64];
static int16_t PSQT_EG[12][64];
static bool psqtReady_ = false;

static void buildPSQT() {
  for (int type = 0; type < 6; ++type) {
    for (Square sq = 0; sq < 64; ++sq) {
      int mg =  (MATERIAL[type]    + PST_MG[type][sq]);
      int eg =  (MATERIAL_EG[type] + PST_EG[type][sq]);
      PSQT_MG[type][sq]     = static_cast<int16_t>( mg);
      PSQT_EG[type][sq]     = static_cast<int16_t>( eg);
      PSQT_MG[type + 6][sq] = static_cast<int16_t>(-(MATERIAL[type]    + PST_MG[type][sq ^ 56]));
      PSQT_EG[type + 6][sq] = static_cast<int16_t>(-(MATERIAL_EG[type] + PST_EG[type][sq ^ 56]));
    }
  }
  psqtReady_ = true;
}

static inline void ensurePSQT() {
  if (!psqtReady_) buildPSQT();
}

// ---------------------------------------------------------------------------
// Pawn-structure query functions (public for testing).
// Use the file-local mask arrays initialized by initPawnMasksImpl().
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
  ensurePSQT();
  return {PSQT_MG[pieceIdx][sq], PSQT_EG[pieceIdx][sq]};
}

PSQTPair computeMaterialPST(const BitboardSet& bb) {
  ensurePSQT();
  int mg = 0, eg = 0;
  for (int i = 0; i < 12; ++i) {
    Bitboard pieces = bb.byPiece[i];
    while (pieces) {
      Square sq = popLsb(pieces);
      mg += PSQT_MG[i][sq];
      eg += PSQT_EG[i][sq];
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

void initPawnMasks() { initPawnMasksImpl(); }

bool isPassed(Square sq, Color color, Bitboard enemyPawns) {
  return (enemyPawns & passedMask(color, sq)) == 0;
}

bool isIsolated(Square sq, Bitboard friendlyPawns) {
  return (friendlyPawns & adjacentFilesMask(fileOf(sq))) == 0;
}

bool isDoubled(Square sq, Color color, Bitboard friendlyPawns) {
  return (friendlyPawns & forwardMask(color, sq)) != 0;
}

bool isBackward(Square sq, Color color, Bitboard friendlyPawns, Bitboard enemyPawnAttacks) {
  const int file = fileOf(sq);
  Bitboard adjacentPawns = friendlyPawns & adjacentFilesMask(file);
  if (!adjacentPawns) return false;

  Square nextSq = SQ_NONE;
  if (color == Color::WHITE) {
    if (sq >= 56) return false;
    nextSq = sq + 8;
  } else {
    if (sq < 8) return false;
    nextSq = sq - 8;
  }

  Bitboard nextSquare = squareBB(nextSq);
  if ((enemyPawnAttacks & nextSquare) == 0) return false;

  Bitboard adjacentAttacks = (color == Color::WHITE)
      ? (shiftNE(adjacentPawns) | shiftNW(adjacentPawns))
      : (shiftSE(adjacentPawns) | shiftSW(adjacentPawns));

  return (adjacentAttacks & nextSquare) == 0;
}

// ---------------------------------------------------------------------------
// Pawn structure constants — centipawns.
//
// Passed pawn bonuses use exponential rank-based scaling: advanced passers
// are worth dramatically more than newly created ones.  Indexed by LERF
// rank (0=rank1, 7=rank8).  Indices 0 and 7 are unused (pawns can't
// occupy rank 1 or rank 8).
//
// Reference: https://www.chessprogramming.org/Passed_Pawn
// ---------------------------------------------------------------------------

EVAL_CONST int PASSED_RANK_BONUS_MG[] = {0, 0, 0, 0, 5, 10, 20, 0};
EVAL_CONST int PASSED_RANK_BONUS_EG[] = {0, 0, 0, 8, 43, 117, 187, 0};
EVAL_CONST int CONNECTED_PASSED_MG =   0;  // tuned to 0 — kept as tuning placeholder
EVAL_CONST int CONNECTED_PASSED_EG =   0;  // tuned to 0 — kept as tuning placeholder
EVAL_CONST int ISOLATED_PENALTY_MG = -19;
EVAL_CONST int ISOLATED_PENALTY_EG =  -8;
EVAL_CONST int DOUBLED_PENALTY_MG  =  -1;
EVAL_CONST int DOUBLED_PENALTY_EG  = -10;
EVAL_CONST int BACKWARD_PENALTY_MG =  -5;
EVAL_CONST int BACKWARD_PENALTY_EG =  -1;

// Protected passed pawn — extra bonus when a passer is defended by a pawn.
// Reference: https://www.chessprogramming.org/Passed_Pawn#Protected
EVAL_CONST int PROTECTED_PASSER_MG = 7;

// ---------------------------------------------------------------------------
// Pawn structure scoring — centipawns, white-relative.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Pawn Hash Table — probe and store helpers.
//
// The pawn hash table caches pawn structure MG/EG scores keyed by a
// pawn-only Zobrist hash.  Since pawn structure changes infrequently
// (only on pawn moves/captures), hit rates exceed 95% in typical searches.
//
// Reference: https://www.chessprogramming.org/Pawn_Hash_Table
// ---------------------------------------------------------------------------

// Probe the pawn hash table.  On hit, adds cached scores to mgScore/egScore
// and returns true.  On miss (or if pawnHash is null), sets pHash for later
// store and returns false.
static bool probePawnHash(const BitboardSet& bb, PawnHashTable* pawnHash,
                          uint64_t& pHash, int& mgScore, int& egScore) {
  if (!pawnHash) return false;
  pHash = zobrist::computePawnHash(bb);
  const PawnEntry* cached = pawnHash->probe(pHash);
  if (cached) {
    mgScore += cached->mgScore;
    egScore += cached->egScore;
    return true;
  }
  return false;
}

// Store pawn structure scores in the pawn hash table.
static void storePawnHash(PawnHashTable* pawnHash, uint64_t pHash,
                          int mg, int eg) {
  if (pawnHash)
    pawnHash->store(pHash, static_cast<int16_t>(mg), static_cast<int16_t>(eg));
}

static void evalPawnStructure(const BitboardSet& bb,
                              Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                              int& mgScore, int& egScore,
                              PawnHashTable* pawnHash) {
  Bitboard whitePawns = bb.byPiece[pieceIndex('P')];
  Bitboard blackPawns = bb.byPiece[pieceIndex('p')];

  if (!whitePawns && !blackPawns) return;

  // --- Pawn hash probe ---
  // Reference: https://www.chessprogramming.org/Pawn_Hash_Table
  uint64_t pHash = 0;
  if (probePawnHash(bb, pawnHash, pHash, mgScore, egScore)) return;

  Bitboard pawns[2]      = {whitePawns, blackPawns};
  Bitboard pawnAtks[2]   = {whitePawnAtk, blackPawnAtk};

  int mg = 0, eg = 0;
  uint8_t passedFiles[2] = {0, 0};

  for (int c = 0; c < 2; ++c) {
    int sign       = (c == 0) ? 1 : -1;
    Color color    = static_cast<Color>(c);
    Bitboard friendly   = pawns[c];
    Bitboard enemy      = pawns[1 - c];
    Bitboard friendAtk  = pawnAtks[c];
    Bitboard enemyAtk   = pawnAtks[1 - c];

    Bitboard p = friendly;
    while (p) {
      Square sq = popLsb(p);
      int rank = rankOf(sq);
      // Black rank mirrors: LERF rank 0 is rank 1 for white, rank 8 for black
      int passedRank = (c == 0) ? rank : (7 - rank);

      if (isPassed(sq, color, enemy)) {
        mg += sign * PASSED_RANK_BONUS_MG[passedRank];
        eg += sign * PASSED_RANK_BONUS_EG[passedRank];
        passedFiles[c] |= 1 << (sq & 7);
        if (squareBB(sq) & friendAtk)
          mg += sign * PROTECTED_PASSER_MG;
      }
      if (isIsolated(sq, friendly)) {
        mg += sign * ISOLATED_PENALTY_MG;
        eg += sign * ISOLATED_PENALTY_EG;
      }
      if (isDoubled(sq, color, friendly)) {
        mg += sign * DOUBLED_PENALTY_MG;
        eg += sign * DOUBLED_PENALTY_EG;
      }
      if (isBackward(sq, color, friendly, enemyAtk)) {
        mg += sign * BACKWARD_PENALTY_MG;
        eg += sign * BACKWARD_PENALTY_EG;
      }
    }
  }

  // Connected passed pawns — bonus per adjacent-file pair of passed pawns.
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    for (int f = 0; f < 7; ++f) {
      if ((passedFiles[c] >> f & 1) && (passedFiles[c] >> (f + 1) & 1)) {
        mg += sign * CONNECTED_PASSED_MG;
        eg += sign * CONNECTED_PASSED_EG;
      }
    }
  }

  mgScore += mg;
  egScore += eg;

  // --- Pawn hash store ---
  // Reference: https://www.chessprogramming.org/Pawn_Hash_Table
  storePawnHash(pawnHash, pHash, mg, eg);
}

// ---------------------------------------------------------------------------
// Bishop pair — bonus when a side has both bishops.
// The bishop pair is one of the most consistently impactful evaluation terms
// because two bishops complement each other's color coverage.
//
// Values: +30cp MG, +50cp EG (EG bonus is higher because open board favors
// the long-range diagonal pair).
//
// Reference: https://www.chessprogramming.org/Bishop_Pair
// ---------------------------------------------------------------------------

EVAL_CONST int BISHOP_PAIR_MG = 33;
EVAL_CONST int BISHOP_PAIR_EG = 46;

static void evalBishopPair(const BitboardSet& bb,
                           int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    Color color = static_cast<Color>(c);
    if (popcount(bb.byPiece[pieceIndex(color, PieceType::BISHOP)]) >= 2) {
      int sign = (c == 0) ? 1 : -1;
      mgScore += sign * BISHOP_PAIR_MG;
      egScore += sign * BISHOP_PAIR_EG;
    }
  }
}

// ---------------------------------------------------------------------------
// Rook on open/semi-open file — bonus for rooks not blocked by own pawns.
//
// Open file (no pawns at all):       +20cp
// Semi-open file (no friendly pawns): +10cp
//
// Reference: https://www.chessprogramming.org/Rook_on_Open_File
// ---------------------------------------------------------------------------

EVAL_CONST int ROOK_OPEN_FILE_MG      = 41;
EVAL_CONST int ROOK_OPEN_FILE_EG      =  0;
EVAL_CONST int ROOK_SEMI_OPEN_FILE_MG = 17;
EVAL_CONST int ROOK_SEMI_OPEN_FILE_EG =  4;

static void evalRookFiles(const BitboardSet& bb,
                          int& mgScore, int& egScore) {
  Bitboard whitePawns = bb.byPiece[pieceIndex('P')];
  Bitboard blackPawns = bb.byPiece[pieceIndex('p')];
  Bitboard allPawns   = whitePawns | blackPawns;

  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Color color = static_cast<Color>(c);
    Bitboard rooks        = bb.byPiece[pieceIndex(color, PieceType::ROOK)];
    Bitboard friendlyPawns = (c == 0) ? whitePawns : blackPawns;
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
// (rank 7 for white, rank 2 for black) and the enemy king is on the back
// rank. Rooks on the 7th cut off the king and attack pawns from behind.
//
// Values: +20cp MG, +30cp EG.
//
// Reference: https://www.chessprogramming.org/Rook_on_Seventh
// ---------------------------------------------------------------------------

EVAL_CONST int ROOK_7TH_MG = 5;
EVAL_CONST int ROOK_7TH_EG = 26;

static void evalRookOnSeventh(const BitboardSet& bb,
                              int& mgScore, int& egScore) {
  // Per-color: the 7th rank for the attacker, the 8th for the enemy king.
  // White: rooks on rank 7 (LERF 6), enemy king on rank 8 (LERF 7).
  // Black: rooks on rank 2 (LERF 1), enemy king on rank 1 (LERF 0).
  static constexpr int SEVENTH_RANK[2] = {6, 1};
  static constexpr int EIGHTH_RANK[2]  = {7, 0};

  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Color color = static_cast<Color>(c);
    Color enemy = static_cast<Color>(1 - c);
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
// Mobility — per-piece attack count bonus.
//
// Pieces that control more squares are worth more. We count the number of
// squares each piece attacks (excluding squares occupied by friendly pieces)
// and apply a small per-square bonus. Weight varies by piece type:
//   Knight/Bishop: +4cp per square
//   Rook:          +2cp per square
//   Queen:         +1cp per square
//
// Queen gets a lower weight because it always has high mobility, so
// differentials are less meaningful (also avoids over-rewarding queen
// centralization at the expense of development).
//
// Reference: https://www.chessprogramming.org/Mobility
// ---------------------------------------------------------------------------

EVAL_CONST int MOBILITY_KNIGHT_MG = 7;
EVAL_CONST int MOBILITY_KNIGHT_EG = 5;
EVAL_CONST int MOBILITY_BISHOP_MG = 4;
EVAL_CONST int MOBILITY_BISHOP_EG = 4;
EVAL_CONST int MOBILITY_ROOK_MG   = 2;
EVAL_CONST int MOBILITY_ROOK_EG   = 3;
EVAL_CONST int MOBILITY_QUEEN_MG  = 1;
EVAL_CONST int MOBILITY_QUEEN_EG  = 4;

static void evalMobility(const BitboardSet& bb,
                         const attacks::AttackInfo& info,
                         int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Bitboard friendly = bb.byColor[c];

    int knightMob = popcount(info.byPiece[c][2] & ~friendly);
    int bishopMob = popcount(info.byPiece[c][3] & ~friendly);
    int rookMob   = popcount(info.byPiece[c][4] & ~friendly);
    int queenMob  = popcount(info.byPiece[c][5] & ~friendly);

    int mgBonus = knightMob * MOBILITY_KNIGHT_MG
                + bishopMob * MOBILITY_BISHOP_MG
                + rookMob   * MOBILITY_ROOK_MG
                + queenMob  * MOBILITY_QUEEN_MG;

    int egBonus = knightMob * MOBILITY_KNIGHT_EG
                + bishopMob * MOBILITY_BISHOP_EG
                + rookMob   * MOBILITY_ROOK_EG
                + queenMob  * MOBILITY_QUEEN_EG;

    mgScore += sign * mgBonus;
    egScore += sign * egBonus;
  }
}

// ---------------------------------------------------------------------------
// King safety / pawn shield — midgame only.
//
// When the king is castled (files a-c or f-h), a bonus/penalty is given
// based on the integrity of the pawn shield in front of the king:
//   - Missing shield pawn:                -15cp per pawn
//   - Shield pawn advanced beyond rank 3: -5cp per pawn
//   - Open file directly in front of king: -20cp (additional)
//
// If the king is in the center (files d-e), no shield evaluation applies
// (the penalty from the MG king PST already discourages a central king).
//
// This score is applied to MG only — in endgames, king safety is irrelevant
// and king centralization (via EG king PST) takes over.
//
// Reference: https://www.chessprogramming.org/King_Safety
// ---------------------------------------------------------------------------

EVAL_CONST int SHIELD_MISSING_PAWN  = -24;
EVAL_CONST int SHIELD_ADV_RANK3     =   0;  // tuned to 0 — kept as tuning placeholder
EVAL_CONST int SHIELD_ADV_RANK4PLUS =  -9;
EVAL_CONST int SHIELD_OPEN_FILE     = -42;

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

// Evaluate pawn shield for one side. Returns MG bonus (positive = good).
static int evalShieldOneSide(const BitboardSet& bb, Color color) {
  uint8_t c = raw(color);
  Square kingSq = 0;

  // Find king square
  Bitboard king = bb.byPiece[pieceIndex(color, PieceType::KING)];
  if (!king) return 0;
  kingSq = lsb(king);

  int kingFile = fileOf(kingSq);

  // Select shield files based on king position; skip if king is in the center.
  int shieldFiles[3];
  if (!selectShieldFiles(kingFile, shieldFiles)) return 0;

  // Friendly pawns bitboard
  Bitboard friendlyPawns = bb.byPiece[pieceIndex(color, PieceType::PAWN)];
  Bitboard allPawns = bb.byPiece[pieceIndex('P')]
                    | bb.byPiece[pieceIndex('p')];

  int score = 0;

  for (int i = 0; i < 3; ++i) {
    int f = shieldFiles[i];
    Bitboard fileMask = fileBB(f);
    Bitboard shieldPawns = friendlyPawns & fileMask;

    if (!shieldPawns) {
      // Missing pawn in the shield
      score += SHIELD_MISSING_PAWN;
    } else {
      // Check if the pawn is advanced — rank-indexed penalty.
      // Pawns one square forward (rank 3 for white / rank 6 for black) get a
      // mild penalty; pawns two or more squares forward get a heavier one.
      // Normalize rank to white-relative (0=home rank) for both colors.
      // Reference: https://www.chessprogramming.org/King_Safety
      Bitboard copy = shieldPawns;
      while (copy) {
        Square sq = popLsb(copy);
        int rank = rankOf(sq);
        int relRank = (color == Color::WHITE) ? rank : (7 - rank);
        if (relRank == 2)       score += SHIELD_ADV_RANK3;
        else if (relRank >= 3)  score += SHIELD_ADV_RANK4PLUS;
      }
    }

    // Additional penalty for open file (no pawns at all)
    if (f == kingFile && !(allPawns & fileMask)) {
      score += SHIELD_OPEN_FILE;
    }
  }

  return score;
}

static void evalKingSafety(const BitboardSet& bb,
                           int& mgScore, int& /* egScore */) {
  mgScore += evalShieldOneSide(bb, Color::WHITE);
  mgScore -= evalShieldOneSide(bb, Color::BLACK);
}

// ---------------------------------------------------------------------------
// Distance helper — Chebyshev distance between two LERF squares.
// Used by evalPassedPawnKingDist (endgame) and evalKingDanger (midgame).
// ---------------------------------------------------------------------------

// Chebyshev distance between two LERF squares.
#ifdef TUNING
int chebyshevDist(Square a, Square b) {
#else
static int chebyshevDist(Square a, Square b) {
#endif
  int dr = rankOf(a) - rankOf(b);
  int df = fileOf(a) - fileOf(b);
  if (dr < 0) dr = -dr;
  if (df < 0) df = -df;
  return dr > df ? dr : df;
}

// ---------------------------------------------------------------------------
// Passed pawn king distance — endgame bonus/penalty based on both kings'
// proximity to passed pawns.
//
// In endgames, a passed pawn's value depends heavily on whether its own
// king can escort it to promotion and whether the enemy king can intercept.
// Each passer receives:
//   - Bonus for own king proximity:   PASSER_OWN_KING × (7 − distance)
//   - Bonus for enemy king distance:  PASSER_ENEMY_KING × distance
//
// Applied to the EG score only (in the midgame, king safety and PSTs
// already govern king placement).  Not cached in the pawn hash because
// it depends on king positions, which change independently of pawn
// structure.
//
// Reference: https://www.chessprogramming.org/King_Distance#Passed_Pawn
// ---------------------------------------------------------------------------

EVAL_CONST int PASSER_OWN_KING   = 0;  // tuned to 0 — kept as tuning placeholder
EVAL_CONST int PASSER_ENEMY_KING = 7;

static void evalPassedPawnKingDist(const BitboardSet& bb,
                                  const Bitboard passedPawns[2],
                                  int& egScore) {
  Bitboard wkBB = bb.byPiece[pieceIndex('K')];
  Bitboard bkBB = bb.byPiece[pieceIndex('k')];
  if (!wkBB || !bkBB) return;
  Square kingSq[2] = {lsb(wkBB), lsb(bkBB)};

  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Bitboard p = passedPawns[c];
    while (p) {
      Square sq = popLsb(p);
      int ownDist   = chebyshevDist(sq, kingSq[c]);
      int enemyDist = chebyshevDist(sq, kingSq[1 - c]);
      egScore += sign * (PASSER_OWN_KING * (7 - ownDist)
                       + PASSER_ENEMY_KING * enemyDist);
    }
  }
}

// ---------------------------------------------------------------------------
// Space — bonus for territory behind own pawns in the centre files.
//
// Controlling space behind the pawn chain gives pieces room to manoeuvre.
// For each side, count safe squares on files c–f, ranks 2–4 (for White)
// or ranks 5–7 (for Black) that are not attacked by enemy pawns.
//
// Value: +1cp per safe square. Applied to both MG and EG.
//
// Reference: https://www.chessprogramming.org/Space
// ---------------------------------------------------------------------------

EVAL_CONST int SPACE_BONUS_MG = 8;

// Files c–f, ranks 2–4 (LERF ranks 1–3) for White.
EVAL_FIXED Bitboard WHITE_SPACE_ZONE =
    (FILE_C | FILE_D | FILE_E | FILE_F) & (RANK_2 | RANK_3 | RANK_4);

// Files c–f, ranks 5–7 (LERF ranks 4–6) for Black.
EVAL_FIXED Bitboard BLACK_SPACE_ZONE =
    (FILE_C | FILE_D | FILE_E | FILE_F) & (RANK_5 | RANK_6 | RANK_7);

static void evalSpace(const BitboardSet& /* bb */,
                      Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                      int& mgScore, int& /* egScore */) {

  int whiteSpace = popcount(WHITE_SPACE_ZONE & ~blackPawnAtk);
  int blackSpace = popcount(BLACK_SPACE_ZONE & ~whitePawnAtk);

  int diff = whiteSpace - blackSpace;
  mgScore += diff * SPACE_BONUS_MG;
}

// ---------------------------------------------------------------------------
// Knight outposts — bonus for knights on squares that are:
//   (a) protected by a friendly pawn, and
//   (b) not attackable by any enemy pawn (no enemy pawn on adjacent files
//       that could advance to attack the square).
//
// Central outposts (d4/d5/e4/e5) receive double the bonus.
//
// Bonus: +15cp (base), +30cp (central). Both MG and EG.
//
// Reference: https://www.chessprogramming.org/Outposts
// ---------------------------------------------------------------------------

EVAL_CONST int OUTPOST_BONUS_MG = 15;
EVAL_CONST int OUTPOST_BONUS_EG = 14;

// ---------------------------------------------------------------------------
// Outpost detection — tests whether a knight on `sq` occupies an outpost.
//
// An outpost is a square that:
//   1. Is protected by a friendly pawn.
//   2. Cannot be attacked by enemy pawns (no enemy pawns on adjacent files
//      that could advance to reach the square).
//
// Reference: https://www.chessprogramming.org/Outposts
// ---------------------------------------------------------------------------

static bool isOutpostSquare(Square sq, int colorIdx,
                            Bitboard friendlyPawnAtk, Bitboard enemyPawns) {
  // Must be protected by a friendly pawn.
  if (!(squareBB(sq) & friendlyPawnAtk)) return false;

  // Must not be attackable by enemy pawns on adjacent files.
  int file = fileOf(sq);
  Bitboard adjFiles = 0;
  if (file > 0) adjFiles |= fileBB(file - 1);
  if (file < 7) adjFiles |= fileBB(file + 1);

  int rank = rankOf(sq);
  // White: enemy (black) pawns above (rank+1..7) advance down to attack.
  // Black: enemy (white) pawns below (0..rank-1) advance up to attack.
  Bitboard dangerMask = (colorIdx == 0)
      ? ~((static_cast<Bitboard>(1) << (8 * (rank + 1))) - 1)
      : (rank > 0) ? (static_cast<Bitboard>(1) << (8 * rank)) - 1
                   : static_cast<Bitboard>(0);
  return !(enemyPawns & adjFiles & dangerMask);
}

static void evalKnightOutposts(const BitboardSet& bb,
                               Bitboard whitePawnAtk, Bitboard blackPawnAtk,
                               int& mgScore, int& egScore) {

  // Central square constants (LERF) — used by outpost evaluation.
  constexpr Square SQ_D4 = 27, SQ_D5 = 35, SQ_E4 = 28, SQ_E5 = 36;

  // Unified loop — evaluate outposts for both colors with parameterized
  // pawn directions.  Enemy pawns that can advance to attack the knight
  // come from ranks "above" (for white) or "below" (for black) in LERF.
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Color color            = static_cast<Color>(c);
    Color enemy            = static_cast<Color>(1 - c);
    Bitboard knights       = bb.byPiece[pieceIndex(color, PieceType::KNIGHT)];
    Bitboard friendlyPAtk  = (c == 0) ? whitePawnAtk : blackPawnAtk;
    Bitboard enemyPawns    = bb.byPiece[pieceIndex(enemy, PieceType::PAWN)];

    while (knights) {
      Square sq = popLsb(knights);

      if (!isOutpostSquare(sq, c, friendlyPAtk, enemyPawns)) continue;

      int bonusMg = OUTPOST_BONUS_MG;
      int bonusEg = OUTPOST_BONUS_EG;
      if (sq == SQ_D4 || sq == SQ_D5 || sq == SQ_E4 || sq == SQ_E5) {
        bonusMg *= 2;
        bonusEg *= 2;
      }

      mgScore += sign * bonusMg;
      egScore += sign * bonusEg;
    }
  }
}

// ---------------------------------------------------------------------------
// Trapped pieces — penalty for pieces stuck in corners/edges where pawns
// block their retreat.
//
// Detects common trapped piece patterns:
//   • Bishop on a7/b8 (or mirrored h7/g8) with enemy pawn blocking escape
//   • Bishop on a2/b1 (or mirrored h2/g1) with enemy pawn blocking escape
//   • Rook trapped by own uncastled king on the same side
//
// Values: −50cp per trapped bishop, −40cp per trapped rook (MG only, since
// in endgames piece mobility increases and trapping is less permanent).
//
// Reference: https://www.chessprogramming.org/Trapped_Pieces
// ---------------------------------------------------------------------------

EVAL_CONST int TRAPPED_BISHOP_PENALTY = -120;
EVAL_CONST int TRAPPED_ROOK_PENALTY   =  -34;

static void evalTrappedPieces(const BitboardSet& bb,
                              int& mgScore, int& /* egScore */) {
  Bitboard whiteBishops = bb.byPiece[pieceIndex('B')];
  Bitboard blackBishops = bb.byPiece[pieceIndex('b')];
  Bitboard whitePawns   = bb.byPiece[pieceIndex('P')];
  Bitboard blackPawns   = bb.byPiece[pieceIndex('p')];
  Bitboard whiteRooks   = bb.byPiece[pieceIndex('R')];
  Bitboard blackRooks   = bb.byPiece[pieceIndex('r')];
  Bitboard whiteKing    = bb.byPiece[pieceIndex('K')];
  Bitboard blackKing    = bb.byPiece[pieceIndex('k')];

  // Trapped bishop lookup table — each entry maps a bishop square to the
  // blocking pawn square.  White patterns use blackPawns; black patterns use
  // whitePawns.  Replaces 8 individual if-statements.
  // Reference: https://www.chessprogramming.org/Trapped_Pieces
  struct BishopTrap { Square bishop; Square blocker; };
  static constexpr BishopTrap WHITE_TRAPS[] = {
    {48, 41}, {57, 41},   // a7/b8 blocked by b6
    {55, 46}, {62, 46},   // h7/g8 blocked by g6
  };
  static constexpr BishopTrap BLACK_TRAPS[] = {
    { 8, 17}, { 1, 17},   // a2/b1 blocked by b3
    {15, 22}, { 6, 22},   // h2/g1 blocked by g3
  };

  for (const auto& t : WHITE_TRAPS) {
    if ((whiteBishops & squareBB(t.bishop)) && (blackPawns & squareBB(t.blocker)))
      mgScore += TRAPPED_BISHOP_PENALTY;
  }
  for (const auto& t : BLACK_TRAPS) {
    if ((blackBishops & squareBB(t.bishop)) && (whitePawns & squareBB(t.blocker)))
      mgScore -= TRAPPED_BISHOP_PENALTY;
  }

  // Trapped rook — rook hemmed in by own uncastled king.
  // Each entry maps a rook square to the king squares that trap it.
  struct RookTrap { Square rook; Bitboard kingMask; };
  static constexpr RookTrap WHITE_ROOK_TRAPS[] = {
    {7, squareBB(5) | squareBB(6)},    // h1 trapped by king on f1/g1
    {0, squareBB(1) | squareBB(2)},    // a1 trapped by king on b1/c1
  };
  static constexpr RookTrap BLACK_ROOK_TRAPS[] = {
    {63, squareBB(61) | squareBB(62)}, // h8 trapped by king on f8/g8
    {56, squareBB(57) | squareBB(58)}, // a8 trapped by king on b8/c8
  };

  for (const auto& t : WHITE_ROOK_TRAPS) {
    if ((whiteRooks & squareBB(t.rook)) && (whiteKing & t.kingMask))
      mgScore += TRAPPED_ROOK_PENALTY;
  }
  for (const auto& t : BLACK_ROOK_TRAPS) {
    if ((blackRooks & squareBB(t.rook)) && (blackKing & t.kingMask))
      mgScore -= TRAPPED_ROOK_PENALTY;
  }
}

// ---------------------------------------------------------------------------
// Threats — bonus for lower-value pieces attacking higher-value enemy pieces.
//
// Uses the pre-computed AttackInfo to detect piece-level pressure: pawns
// threatening minors/rooks/queens, minors threatening rooks/queens, and
// rooks threatening queens.  Each attacker-victim pair has separate MG/EG
// weights — threats are most valuable in the midgame when piece safety
// dictates strategy.
//
// Reference: https://www.chessprogramming.org/Evaluation#Threats
// ---------------------------------------------------------------------------

EVAL_CONST int THREAT_PAWN_VS_MINOR_MG  = 23;
EVAL_CONST int THREAT_PAWN_VS_ROOK_MG   = 13;
EVAL_CONST int THREAT_PAWN_VS_QUEEN_MG  = 15;
EVAL_CONST int THREAT_MINOR_VS_ROOK_MG  = 22;
EVAL_CONST int THREAT_MINOR_VS_QUEEN_MG = 13;
EVAL_CONST int THREAT_ROOK_VS_QUEEN_MG  = 24;

static void evalThreats(const BitboardSet& bb,
                        const attacks::AttackInfo& info,
                        int& mgScore, int& /* egScore */) {
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    int enemy = 1 - c;

    // Enemy piece bitboards.
    Color enemyColor = static_cast<Color>(enemy);
    Bitboard enemyMinors = bb.byPiece[pieceIndex(enemyColor, PieceType::KNIGHT)]
                         | bb.byPiece[pieceIndex(enemyColor, PieceType::BISHOP)];
    Bitboard enemyRooks  = bb.byPiece[pieceIndex(enemyColor, PieceType::ROOK)];
    Bitboard enemyQueens = bb.byPiece[pieceIndex(enemyColor, PieceType::QUEEN)];

    // Friendly pawn attacks (PieceType::PAWN = 1).
    Bitboard pawnAtk = info.byPiece[c][1];
    int mg = 0;

    mg += popcount(pawnAtk & enemyMinors) * THREAT_PAWN_VS_MINOR_MG;
    mg += popcount(pawnAtk & enemyRooks)  * THREAT_PAWN_VS_ROOK_MG;
    mg += popcount(pawnAtk & enemyQueens) * THREAT_PAWN_VS_QUEEN_MG;

    // Friendly minor attacks (KNIGHT=2, BISHOP=3).
    Bitboard minorAtk = info.byPiece[c][2] | info.byPiece[c][3];
    mg += popcount(minorAtk & enemyRooks)  * THREAT_MINOR_VS_ROOK_MG;
    mg += popcount(minorAtk & enemyQueens) * THREAT_MINOR_VS_QUEEN_MG;

    // Friendly rook attacks (PieceType::ROOK = 4).
    Bitboard rookAtk = info.byPiece[c][4];
    mg += popcount(rookAtk & enemyQueens) * THREAT_ROOK_VS_QUEEN_MG;

    mgScore += sign * mg;
  }
}

// ---------------------------------------------------------------------------
// King danger — unified king safety evaluation combining zone attacks and
// piece proximity.
//
// Counts enemy piece types attacking the king ring (king square + 8
// surrounding squares) using the AttackInfo already computed for mobility.
// When at least one piece attacks the zone, nearby enemy pieces (Chebyshev
// distance ≤ 3) add proximity pressure to the attacker weight sum.  The
// combined weight is looked up in a nonlinear danger table, reflecting
// that multiple coordinated attackers create super-linear threat.
//
// Merges the concepts of "king tropism" (piece proximity) and "attacking
// king zone" (piece influence on king ring) into a single coherent signal.
// Proximity only matters when there's a genuine attack — distant pieces
// that happen to be nearby don't contribute.
//
// MG-only — in endgames, king exposure is less relevant and king
// centralization (via EG PSTs) takes over.
//
// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
// Reference: https://www.chessprogramming.org/King_Safety#King_Tropism
// ---------------------------------------------------------------------------

// Per-piece-type zone attack weight: how dangerous each attacker type is.
// Indexed by minor/major piece offset (N=0, B=1, R=2, Q=3).
// Fixed (not tunable) — tuning shifts danger via TABLE entries instead,
// keeping all parameters linear for analytical gradient computation.
EVAL_FIXED int KING_DANGER_WEIGHT[] = {2, 2, 3, 5};

// Nonlinear danger table — maps total attacker weight to MG penalty (cp).
// Index = sum of attacker weights (0..max).  Clamped at the last entry.
// TABLE[0] stays fixed at 0; entries 1..12 are tunable.
EVAL_CONST int KING_DANGER_TABLE[] = {0, 0, 8, 8, 11, 11, 26, 40, 60, 105, 167, 167, 231};

// ---------------------------------------------------------------------------
// King danger weight computation — sums per-piece-type zone attack weights
// and adds proximity bonuses for enemy pieces near the king.
//
// Each minor/major piece type has a fixed danger weight (KING_DANGER_WEIGHT).
// If any piece attacks the king zone, nearby pieces (Chebyshev distance ≤ 3)
// add incremental weight regardless of whether they directly attack the zone.
//
// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
// ---------------------------------------------------------------------------

static int computeKingDangerWeight(const BitboardSet& bb,
                                   const attacks::AttackInfo& info,
                                   int enemy, Bitboard kingZone,
                                   Square kingSq) {
  // Sum attacker weights for enemy pieces attacking the king zone.
  int totalWeight = 0;
  Color enemyColor = static_cast<Color>(enemy);
  for (int pt = 0; pt < 4; ++pt) {
    // PieceType: KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5
    int pieceType = pt + 2;
    if (info.byPiece[enemy][pieceType] & kingZone)
      totalWeight += KING_DANGER_WEIGHT[pt];
  }

  // Proximity bonus: close enemy pieces amplify the zone attack.
  // Only applied when at least one piece attacks the zone — avoids
  // noise from pieces that happen to be nearby but aren't threatening.
  if (totalWeight > 0) {
    for (int pt = 0; pt < 4; ++pt) {
      int pieceIdx = pieceIndex(enemyColor, static_cast<PieceType>(pt + 2));  // KNIGHT..QUEEN
      Bitboard pieces = bb.byPiece[pieceIdx];
      while (pieces) {
        Square sq = popLsb(pieces);
        if (chebyshevDist(sq, kingSq) <= 3)
          ++totalWeight;
      }
    }
  }

  return totalWeight;
}

static void evalKingDanger(const BitboardSet& bb,
                           const attacks::AttackInfo& info,
                           int& mgScore) {
  for (int c = 0; c < 2; ++c) {
    // Penalty applies to the defending side: attacks on white king hurt
    // white (mgScore -=), attacks on black king hurt black (mgScore +=).
    int sign = (c == 0) ? -1 : 1;
    int enemy = 1 - c;
    Color color = static_cast<Color>(c);

    // Locate king.
    int kingIdx = pieceIndex(color, PieceType::KING);
    Bitboard kingBB = bb.byPiece[kingIdx];
    if (!kingBB) continue;
    Square kingSq = lsb(kingBB);

    // King zone: king square + 8 surrounding squares.
    Bitboard kingZone = attacks::KING[kingSq] | squareBB(kingSq);

    int totalWeight = computeKingDangerWeight(bb, info, enemy, kingZone, kingSq);

    // Look up nonlinear penalty.
    int idx = totalWeight < KING_DANGER_TABLE_SIZE
            ? totalWeight
            : KING_DANGER_TABLE_SIZE - 1;
    mgScore += sign * KING_DANGER_TABLE[idx];
  }
}

// ===========================================================================
// Hash table implementations
// ===========================================================================

// ---------------------------------------------------------------------------
// Pawn Hash Table
// ---------------------------------------------------------------------------

void PawnHashTable::resize(int numEntries) {
  free();
  size = utils::roundDownPow2(numEntries);
  if (size == 0) return;
  mask = size - 1;
  entries = new (std::nothrow) PawnEntry[size];
  if (!entries) { size = 0; mask = 0; return; }  // OOM fallback
  clear();
}

void PawnHashTable::free() {
  delete[] entries;
  entries = nullptr;
  size = 0;
  mask = 0;
}

void PawnHashTable::clear() {
  if (entries) std::memset(entries, 0, size * sizeof(PawnEntry));
}

const PawnEntry* PawnHashTable::probe(uint64_t hash) const {
  if (!entries) return nullptr;
  int idx = static_cast<int>(hash) & mask;
  uint32_t key32 = static_cast<uint32_t>(hash >> 32);
  const PawnEntry& e = entries[idx];
  return (e.key == key32) ? &e : nullptr;
}

void PawnHashTable::store(uint64_t hash, int16_t mg, int16_t eg) {
  if (!entries) return;
  int idx = static_cast<int>(hash) & mask;
  PawnEntry& e = entries[idx];
  e.key     = static_cast<uint32_t>(hash >> 32);
  e.mgScore = mg;
  e.egScore = eg;
}

// ---------------------------------------------------------------------------
// Evaluation Hash Table
// ---------------------------------------------------------------------------

void EvalHashTable::resize(int numEntries) {
  free();
  size = utils::roundDownPow2(numEntries);
  if (size == 0) return;
  mask = size - 1;
  entries = new (std::nothrow) EvalEntry[size];
  if (!entries) { size = 0; mask = 0; return; }  // OOM fallback
  clear();
}

void EvalHashTable::free() {
  delete[] entries;
  entries = nullptr;
  size = 0;
  mask = 0;
}

void EvalHashTable::clear() {
  if (entries) std::memset(entries, 0, size * sizeof(EvalEntry));
}

const EvalEntry* EvalHashTable::probe(uint64_t hash) const {
  if (!entries) return nullptr;
  int idx = static_cast<int>(hash) & mask;
  uint32_t key32 = static_cast<uint32_t>(hash >> 32);
  const EvalEntry& e = entries[idx];
  return (e.key == key32) ? &e : nullptr;
}

void EvalHashTable::store(uint64_t hash, int16_t s) {
  if (!entries) return;
  int idx = static_cast<int>(hash) & mask;
  EvalEntry& e = entries[idx];
  e.key   = static_cast<uint32_t>(hash >> 32);
  e.score = s;
  e.pad   = 0;
}

// ---------------------------------------------------------------------------
// Bad bishop — penalty for bishops blocked by own pawns on same color.
//
// A bishop loses effectiveness when many friendly pawns occupy squares
// of the same color complex, restricting its mobility.
//
// Reference: https://www.chessprogramming.org/Bad_Bishop
// ---------------------------------------------------------------------------

EVAL_CONST int BAD_BISHOP_MG = -5;
EVAL_CONST int BAD_BISHOP_EG = -3;

static void evalBadBishop(const BitboardSet& bb,
                          int& mgScore, int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Color color = static_cast<Color>(c);
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
// Rook behind passed pawn (Tarrasch Rule) — EG only.
//
// A rook behind a friendly passed pawn supports its advance; behind an
// enemy passed pawn, it restricts the advance.
//
// Reference: https://www.chessprogramming.org/Tarrasch_Rule
// ---------------------------------------------------------------------------

EVAL_CONST int ROOK_BEHIND_OWN_PASSER_EG   =  5;
EVAL_CONST int ROOK_BEHIND_ENEMY_PASSER_EG  = -40;

static void evalRookBehindPasser(const BitboardSet& bb,
                                const Bitboard passedPawns[2],
                                int& egScore) {
  for (int c = 0; c < 2; ++c) {
    int sign = (c == 0) ? 1 : -1;
    Color color         = static_cast<Color>(c);
    Color enemy         = static_cast<Color>(1 - c);
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

static int evaluateImpl(const BitboardSet& bb, int mgScore, int egScore,
                        PawnHashTable* pawnHash, int precomputedPhase = -1) {
  // Full attack computation first — shared by pawn structure, mobility,
  // threats, king danger, knight outposts, and space evaluation.
  // Precondition: attacks::init() must have been called before first
  // evaluation (guaranteed by Position constructor).
  attacks::AttackInfo info = attacks::computeAll(bb);

  // Extract pawn attacks from AttackInfo (already computed via bulk shift
  // inside computeAll) — avoids redundant manual shift-OR computation.
  Bitboard whitePawnAtk = info.byPiece[0][piece::raw(PieceType::PAWN)];
  Bitboard blackPawnAtk = info.byPiece[1][piece::raw(PieceType::PAWN)];

  evalPawnStructure(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore, pawnHash);

  // Collect passed pawns once — avoids redundant isPassed() calls in
  // evalPassedPawnKingDist and evalRookBehindPasser.
  Bitboard passedPawns[2] = {0, 0};
  for (int c = 0; c < 2; ++c) {
    Color color    = static_cast<Color>(c);
    Color enemyClr = static_cast<Color>(1 - c);
    Bitboard pawns = bb.byPiece[pieceIndex(color, PieceType::PAWN)];
    Bitboard enemy = bb.byPiece[pieceIndex(enemyClr, PieceType::PAWN)];
    Bitboard p = pawns;
    while (p) {
      Square sq = popLsb(p);
      if (isPassed(sq, color, enemy))
        passedPawns[c] |= squareBB(sq);
    }
  }

  evalPassedPawnKingDist(bb, passedPawns, egScore);
  evalBishopPair(bb, mgScore, egScore);
  evalBadBishop(bb, mgScore, egScore);
  evalRookFiles(bb, mgScore, egScore);
  evalRookOnSeventh(bb, mgScore, egScore);
  evalRookBehindPasser(bb, passedPawns, egScore);
  evalKnightOutposts(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore);
  evalKingSafety(bb, mgScore, egScore);
  evalSpace(bb, whitePawnAtk, blackPawnAtk, mgScore, egScore);
  evalTrappedPieces(bb, mgScore, egScore);

  evalMobility(bb, info, mgScore, egScore);
  evalThreats(bb, info, mgScore, egScore);
  evalKingDanger(bb, info, mgScore);

  int phase = (precomputedPhase >= 0) ? precomputedPhase
                                     : computeGamePhase(bb);
  int score = (mgScore * phase + egScore * (MAX_PHASE - phase)) / MAX_PHASE;

  score = applyOCBScaling(score, bb, phase);

  return score;
}

int evaluatePosition(const BitboardSet& bb,
                     PawnHashTable* pawnHash) {
  initPawnMasks();
  ensurePSQT();

  int mgScore = 0;
  int egScore = 0;

  for (int i = 0; i < 12; ++i) {
    Bitboard pieces = bb.byPiece[i];
    while (pieces) {
      Square sq = popLsb(pieces);
      mgScore += PSQT_MG[i][sq];
      egScore += PSQT_EG[i][sq];
    }
  }

  return evaluateImpl(bb, mgScore, egScore, pawnHash);
}

int evaluatePosition(const BitboardSet& bb, int mgMatPST, int egMatPST,
                     PawnHashTable* pawnHash) {
  initPawnMasks();
  return evaluateImpl(bb, mgMatPST, egMatPST, pawnHash);
}

int evaluatePosition(const BitboardSet& bb, int mgMatPST, int egMatPST,
                     int phase, PawnHashTable* pawnHash) {
  initPawnMasks();
  return evaluateImpl(bb, mgMatPST, egMatPST, pawnHash, phase);
}

#ifdef TUNING
void invalidatePSQT() { psqtReady_ = false; }
#endif

}  // namespace eval
}  // namespace LibreChess
