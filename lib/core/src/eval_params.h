#ifndef LIBRECHESS_EVAL_PARAMS_H
#define LIBRECHESS_EVAL_PARAMS_H

// ---------------------------------------------------------------------------
// Evaluation parameters — all tunable constants for the evaluation function.
//
// Under TUNING, parameters have external linkage (mutable at runtime) so the
// offline tuner can read/write them.  Production builds keep them static
// constexpr for full compiler optimisation.  trace.h re-declares them as
// extern for multi-TU access (inline variables would be cleaner but require
// GCC 7+; the tuner toolchain is GCC 5.1).
//
// Reference: https://www.chessprogramming.org/Evaluation
// ---------------------------------------------------------------------------

#include "bitboard.h"

// ===========================================================================
// Tuning macros — control linkage and element types for all eval constants.
//
//   EVAL_CONST  — tunable parameters (mutable in tuning builds).
//   EVAL_FIXED  — non-tunable constants (const in tuning builds).
//   PST_ELEM    — element type for piece-square tables (int8_t or int).
//   MAT_ELEM    — element type for material arrays (int16_t or int).
// ===========================================================================

#ifdef TUNING
#define EVAL_CONST              // mutable, external linkage
#define EVAL_FIXED const        // immutable, internal linkage (const → per-TU copy)
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

namespace LibreChess {
namespace eval {

#ifdef TUNING
// Force external linkage for EVAL_FIXED symbols — `const` at namespace scope
// has internal linkage by default in C++.  trace.h re-declares these as
// extern so that trace.cpp and tune.cpp can reference them.
extern const int KING_DANGER_WEIGHT[];
#endif

// ===========================================================================
// Material values
// ===========================================================================

// Indexed by piece type offset (P=0 N=1 B=2 R=3 Q=4 K=5).
// Separate MG and EG tables allow the tuner to find phase-optimal piece
// values independently.  materialValue() returns MATERIAL[idx] for
// SEE, lazy eval, delta pruning.
//
// Pawn MG/EG pinned at 100 — search pruning margins (futility, razoring,
// RFP, delta, aspiration) and KING_SAFETY_TABLE are calibrated to the
// 100cp-per-pawn convention.  Pawn is excluded from the tuning registry
// (trace.cpp) to prevent K/param scale degeneracy.
//
// Starting values: CPW Simplified Evaluation Function (Michniewski).
// Reference: https://www.chessprogramming.org/Simplified_Evaluation_Function
// Reference: https://www.chessprogramming.org/Material
EVAL_CONST MAT_ELEM MATERIAL[] = {100, 320, 330, 500, 900, 0};
EVAL_CONST MAT_ELEM MATERIAL_EG[] = {100, 320, 330, 500, 900, 0};

// ===========================================================================
// Piece-square tables — centipawns, LERF order (a1=0, h8=63).
// White's perspective; black mirrors via (sq ^ 56).
// Based on the simplified evaluation function (CPW / Tomasz Michniewski).
//
// Midgame (MG) tables: king should hide behind pawns, minor pieces want
// center control. Endgame (EG) tables: king should be active and central,
// passed pawns and advancement matter more.
//
// Reference: https://www.chessprogramming.org/Piece-Square_Tables
// ===========================================================================

// clang-format off

// --- Midgame PSTs (CPW / Tomasz Michniewski, LERF order) ---

EVAL_CONST PST_ELEM PST_PAWN_MG[64] = {
       0,   0,   0,   0,   0,   0,   0,   0,
       5,  10,  10, -20, -20,  10,  10,   5,
       5,  -5, -10,   0,   0, -10,  -5,   5,
       0,   0,   0,  20,  20,   0,   0,   0,
       5,   5,  10,  25,  25,  10,   5,   5,
      10,  10,  20,  30,  30,  20,  10,  10,
      50,  50,  50,  50,  50,  50,  50,  50,
       0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KNIGHT_MG[64] = {
     -50, -40, -30, -30, -30, -30, -40, -50,
     -40, -20,   0,   5,   5,   0, -20, -40,
     -30,   5,  10,  15,  15,  10,   5, -30,
     -30,   0,  15,  20,  20,  15,   0, -30,
     -30,   5,  15,  20,  20,  15,   5, -30,
     -30,   0,  10,  15,  15,  10,   0, -30,
     -40, -20,   0,   0,   0,   0, -20, -40,
     -50, -40, -30, -30, -30, -30, -40, -50
};

EVAL_CONST PST_ELEM PST_BISHOP_MG[64] = {
     -20, -10, -10, -10, -10, -10, -10, -20,
     -10,   5,   0,   0,   0,   0,   5, -10,
     -10,  10,  10,  10,  10,  10,  10, -10,
     -10,   0,  10,  10,  10,  10,   0, -10,
     -10,   5,   5,  10,  10,   5,   5, -10,
     -10,   0,   5,  10,  10,   5,   0, -10,
     -10,   0,   0,   0,   0,   0,   0, -10,
     -20, -10, -10, -10, -10, -10, -10, -20
};

EVAL_CONST PST_ELEM PST_ROOK_MG[64] = {
       0,   0,   0,   5,   5,   0,   0,   0,
      -5,   0,   0,   0,   0,   0,   0,  -5,
      -5,   0,   0,   0,   0,   0,   0,  -5,
      -5,   0,   0,   0,   0,   0,   0,  -5,
      -5,   0,   0,   0,   0,   0,   0,  -5,
      -5,   0,   0,   0,   0,   0,   0,  -5,
       5,  10,  10,  10,  10,  10,  10,   5,
       0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_QUEEN_MG[64] = {
     -20, -10, -10,  -5,  -5, -10, -10, -20,
     -10,   0,   5,   0,   0,   0,   0, -10,
     -10,   5,   5,   5,   5,   5,   0, -10,
       0,   0,   5,   5,   5,   5,   0,  -5,
      -5,   0,   5,   5,   5,   5,   0,  -5,
     -10,   0,   5,   5,   5,   5,   0, -10,
     -10,   0,   0,   0,   0,   0,   0, -10,
     -20, -10, -10,  -5,  -5, -10, -10, -20
};

EVAL_CONST PST_ELEM PST_KING_MG[64] = {
      20,  30,  10,   0,   0,  10,  30,  20,
      20,  20,   0,   0,   0,   0,  20,  20,
     -10, -20, -20, -20, -20, -20, -20, -10,
     -20, -30, -30, -40, -40, -30, -30, -20,
     -30, -40, -40, -50, -50, -40, -40, -30,
     -30, -40, -40, -50, -50, -40, -40, -30,
     -30, -40, -40, -50, -50, -40, -40, -30,
     -30, -40, -40, -50, -50, -40, -40, -30
};

// --- Endgame PSTs (king from CPW, others zeroed — tuner discovers them) ---

EVAL_CONST PST_ELEM PST_PAWN_EG[64] = {
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KNIGHT_EG[64] = {
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_BISHOP_EG[64] = {
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_ROOK_EG[64] = {
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_QUEEN_EG[64] = {
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KING_EG[64] = {
     -50, -30, -30, -30, -30, -30, -30, -50,
     -30, -30,   0,   0,   0,   0, -30, -30,
     -30, -10,  20,  30,  30,  20, -10, -30,
     -30, -10,  30,  40,  40,  30, -10, -30,
     -30, -10,  30,  40,  40,  30, -10, -30,
     -30, -10,  20,  30,  30,  20, -10, -30,
     -30, -20, -10,   0,   0, -10, -20, -30,
     -50, -40, -30, -20, -20, -30, -40, -50
};

// clang-format on

// ===========================================================================
// Pawn structure
// ===========================================================================

// Passed pawn rank bonuses — exponential scaling: advanced passers are
// worth dramatically more.  Indexed by LERF rank (0=rank1, 7=rank8).
// Indices 0 and 7 unused (pawns can't occupy rank 1 or rank 8).
// Reference: https://www.chessprogramming.org/Passed_Pawn
EVAL_CONST int PASSED_RANK_BONUS_MG[] = {0, 0, 0, 0, 10, 20, 40, 0};
EVAL_CONST int PASSED_RANK_BONUS_EG[] = {0, 0, 0, 15, 50, 100, 175, 0};
EVAL_CONST int ISOLATED_PENALTY_MG = -15;
EVAL_CONST int ISOLATED_PENALTY_EG = -10;
EVAL_CONST int DOUBLED_PENALTY_EG  = -10;

// Backward pawn — no adjacent support, stop square controlled by enemy pawn.
// Exclusive with isolated (isolated pawns are not also backward).
// Reference: https://www.chessprogramming.org/Backward_Pawn
EVAL_CONST int BACKWARD_PENALTY_MG = -10;
EVAL_CONST int BACKWARD_PENALTY_EG =  -5;

// Connected pawns — rank-indexed bonus for chains (defended by own pawn)
// or phalanxes (adjacent file, same rank).  Higher ranks = larger bonus.
// Indices 0 and 7 unused (pawns can't occupy rank 1 or rank 8).
// Reference: https://www.chessprogramming.org/Connected_Pawns
EVAL_CONST int CONNECTED_BONUS_MG[] = {0, 5, 10, 15, 20, 40, 100, 0};
EVAL_CONST int CONNECTED_BONUS_EG[] = {0, 0, 5, 10, 20, 35, 50, 0};

// Candidate passed pawn — can become a passer (helpers ≥ sentries).
// Rank-indexed; roughly half the passed pawn bonus.
// Reference: https://www.chessprogramming.org/Candidate_Passed_Pawn
EVAL_CONST int CANDIDATE_PASSED_MG[] = {0, 0, 5, 10, 15, 25, 0, 0};
EVAL_CONST int CANDIDATE_PASSED_EG[] = {0, 0, 5, 10, 25, 50, 0, 0};

// Protected passed pawn — passed pawn defended by own pawn.
// Reference: https://www.chessprogramming.org/Protected_Passed_Pawn
EVAL_CONST int PROTECTED_PASSER_MG =  15;
EVAL_CONST int PROTECTED_PASSER_EG =  10;

// Passed pawn king proximity — Chebyshev distance, EG only.
// OWN_KING_DIST is negative (penalty when own king is far from passer).
// ENEMY_KING_DIST is positive (bonus when enemy king is far from passer).
// Reference: https://www.chessprogramming.org/King_Pawn_Tropism
EVAL_CONST int PASSER_OWN_KING_DIST_EG   =  -5;
EVAL_CONST int PASSER_ENEMY_KING_DIST_EG =   10;

// ===========================================================================
// Piece bonuses
// ===========================================================================

// Bishop pair — bonus when a side has both bishops.
// Reference: https://www.chessprogramming.org/Bishop_Pair
EVAL_CONST int BISHOP_PAIR_MG =  30;
EVAL_CONST int BISHOP_PAIR_EG =  50;

// Bad bishop — penalty for bishops blocked by own pawns on same color.
// Reference: https://www.chessprogramming.org/Bad_Bishop
EVAL_CONST int BAD_BISHOP_MG  =  -5;
EVAL_CONST int BAD_BISHOP_EG  =  -3;

// Knight outposts — bonus for knights on pawn-protected, unattackable
// squares.  Central outposts (d4/d5/e4/e5) receive double the bonus.
// Reference: https://www.chessprogramming.org/Outposts
EVAL_CONST int OUTPOST_BONUS_MG = 15;
EVAL_CONST int OUTPOST_BONUS_EG = 10;

// Bishop outposts — same criteria as knight, but only on enemy half
// (ranks 4-6 for white, 3-5 for black).  Smaller bonus than knight
// because bishops benefit less from a single fixed post.
// Reference: https://www.chessprogramming.org/Outposts
EVAL_CONST int BISHOP_OUTPOST_MG = 15;
EVAL_CONST int BISHOP_OUTPOST_EG =  5;

// ===========================================================================
// Threats — bonus for attacking poorly defended enemy pieces.
// Reference: https://www.chessprogramming.org/Threat_Move
// ===========================================================================

// Pawn attacking enemy minor/rook/queen.
EVAL_CONST int THREAT_BY_PAWN_MG  =  25;
EVAL_CONST int THREAT_BY_PAWN_EG  =  15;
// Minor piece (N/B) attacking enemy rook/queen.
EVAL_CONST int THREAT_BY_MINOR_MG =  20;
EVAL_CONST int THREAT_BY_MINOR_EG =  10;
// Rook attacking enemy queen.
EVAL_CONST int THREAT_BY_ROOK_MG  =  25;
EVAL_CONST int THREAT_BY_ROOK_EG  =  15;
// Attacked piece with no defender (hanging).
EVAL_CONST int THREAT_HANGING_MG  =  15;
EVAL_CONST int THREAT_HANGING_EG  =  10;

// ===========================================================================
// Rook bonuses
// ===========================================================================

// Rook on open/semi-open file.
// Reference: https://www.chessprogramming.org/Rook_on_Open_File
EVAL_CONST int ROOK_OPEN_FILE_MG      =  35;
EVAL_CONST int ROOK_OPEN_FILE_EG      =   5;
EVAL_CONST int ROOK_SEMI_OPEN_FILE_MG =  15;
EVAL_CONST int ROOK_SEMI_OPEN_FILE_EG =   5;

// Rook on 7th rank — bonus when rook is on opponent's second rank and
// enemy king is on back rank.
// Reference: https://www.chessprogramming.org/Rook_on_Seventh
EVAL_CONST int ROOK_7TH_MG = 10;
EVAL_CONST int ROOK_7TH_EG = 25;

// Rook behind passed pawn (Tarrasch Rule) — EG only.
// Reference: https://www.chessprogramming.org/Tarrasch_Rule
EVAL_CONST int ROOK_BEHIND_OWN_PASSER_EG  =  15;
EVAL_CONST int ROOK_BEHIND_ENEMY_PASSER_EG = -15;

// ===========================================================================
// Mobility — nonlinear lookup tables.
//
// Per-piece-type tables indexed by safe attack count (squares excluding
// friendly pieces and enemy pawn attacks).  Allows diminishing returns and
// piece-specific mobility curves — cannot be expressed by the previous
// linear model.
//
// Table sizes: Knight 9, Bishop 14, Rook 15, Queen 28 (max possible).
// Starting values: linear interpolation from tuned step/center baseline,
// to be re-tuned via Texel method.
//
// Reference: https://www.chessprogramming.org/Mobility
// ===========================================================================

// clang-format off

// Knight: max 8 safe squares.
EVAL_CONST int MOB_KNIGHT_MG[] = {-16, -12, -8, -4, 0, 4, 8, 12, 16};
EVAL_CONST int MOB_KNIGHT_EG[] = {-16, -12, -8, -4, 0, 4, 8, 12, 16};
// Bishop: max 13 safe squares.
EVAL_CONST int MOB_BISHOP_MG[] = {-18, -15, -12, -9, -6, -3, 0, 3, 6, 9, 12, 15, 18, 21};
EVAL_CONST int MOB_BISHOP_EG[] = {-18, -15, -12, -9, -6, -3, 0, 3, 6, 9, 12, 15, 18, 21};
// Rook: max 14 safe squares.
EVAL_CONST int MOB_ROOK_MG[] = {-14, -12, -10, -8, -6, -4, -2, 0, 2, 4, 6, 8, 10, 12, 14};
EVAL_CONST int MOB_ROOK_EG[] = {-14, -12, -10, -8, -6, -4, -2, 0, 2, 4, 6, 8, 10, 12, 14};
// Queen: max 27 safe squares.
EVAL_CONST int MOB_QUEEN_MG[] = {-14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
EVAL_CONST int MOB_QUEEN_EG[] = {-14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
// clang-format on

// Table sizes (max mobility + 1 per piece type).
EVAL_FIXED int MOB_KNIGHT_SIZE = 9;
EVAL_FIXED int MOB_BISHOP_SIZE = 14;
EVAL_FIXED int MOB_ROOK_SIZE   = 15;
EVAL_FIXED int MOB_QUEEN_SIZE  = 28;

// ===========================================================================
// King safety
// ===========================================================================

// ---------------------------------------------------------------------------
// Pawn shield — rank-indexed bonus per shield-file pawn (MG only).
//
// Index: 0 = pawn on home rank (ideal), 1 = one rank advanced,
//        2 = two ranks advanced, 3 = missing pawn.
// Negative values penalize weaker shields.
//
// Reference: https://www.chessprogramming.org/King_Safety#Pawn_Shield
// ---------------------------------------------------------------------------
EVAL_CONST int SHIELD_RANK[] = {25, 15, 0, -15};

// Additional penalty for a fully open file on the king file (no pawns at all).
EVAL_CONST int SHIELD_OPEN_FILE     = -40;

// ---------------------------------------------------------------------------
// Pawn storm — penalty for enemy pawns advancing toward the castled king.
//
// Indexed by enemy pawn rank from the attacked side's perspective
// (LERF for white, mirrored for black).  Lower index = closer to our king.
// MG only.
//
// Reference: https://www.chessprogramming.org/King_Safety#Pawn_Storm
// ---------------------------------------------------------------------------
EVAL_CONST int PAWN_STORM[] = {0, 0, -20, -10, 0, 0, 0, 0};

// ---------------------------------------------------------------------------
// King danger — zone-attack-based nonlinear penalty, MG only.
//
// Per-piece counting: each enemy piece whose attacks overlap the king zone
// adds its KING_DANGER_WEIGHT.  Safe check bonuses are added when an enemy
// piece can deliver a check on a square not defended by the attacked side.
// Total weight indexes into KING_SAFETY_TABLE (S-curve, capped at 500cp).
//
// Attacker threshold: danger = 0 if attackerCount < 2 OR no enemy queen.
//
// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
// Reference: https://www.chessprogramming.org/King_Safety#Attack_Units
// ---------------------------------------------------------------------------

// Per-piece-type zone attack weight (N=0, B=1, R=2, Q=3).
// Fixed — tuning shifts danger via safe check values and the safety table.
EVAL_FIXED int KING_DANGER_WEIGHT[] = {2, 2, 3, 5};

// Safe check bonuses — added to attack weight when an enemy piece can
// deliver a safe check (check square not defended by the attacked side).
EVAL_CONST int SAFE_CHECK_KNIGHT =  20;
EVAL_CONST int SAFE_CHECK_BISHOP =  10;
EVAL_CONST int SAFE_CHECK_ROOK   =  15;
EVAL_CONST int SAFE_CHECK_QUEEN  =  25;

// clang-format off
// Nonlinear safety table — maps total attack weight to MG penalty (cp).
// S-curve formula (rescaled to centipawns), capped at 500.
// Reference: https://www.chessprogramming.org/King_Safety#Attack_Units
EVAL_FIXED int KING_SAFETY_TABLE[] = {
    0,   0,   1,   2,   3,   5,   7,   9,  12,  15,
   18,  22,  26,  30,  35,  39,  44,  50,  56,  62,
   68,  75,  82,  85,  89,  97, 105, 113, 122, 131,
  140, 150, 169, 180, 191, 202, 213, 225, 237, 248,
  260, 272, 283, 295, 307, 319, 330, 342, 354, 366,
  377, 389, 401, 412, 424, 436, 448, 459, 471, 483,
  494, 500, 500, 500, 500, 500, 500, 500, 500, 500,
  500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
  500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
  500, 500, 500, 500, 500, 500, 500, 500, 500, 500
};
// clang-format on

// ===========================================================================
// Space
// ===========================================================================

// ---------------------------------------------------------------------------
// Space evaluation — Stockfish-style safe square counting on central files.
//
// Safe squares on c-f files, ranks 2-4 (white) / 5-7 (black), excluding
// own pawns and enemy pawn attacks.  Squares behind own pawns count double.
// Bonus is scaled by (piece_count - 2 * open_files) squared, divided by 16.
// MG only.
//
// Reference: https://www.chessprogramming.org/Space
// ---------------------------------------------------------------------------
EVAL_CONST int SPACE_WEIGHT = 5;

// ===========================================================================
// Tempo
// ===========================================================================

// ---------------------------------------------------------------------------
// Tempo bonus — small bonus for the side to move (initiative).
//
// Applied in the search layer's evaluate() function (where side-to-move is
// known), not in evaluatePosition() (which is white-relative and has no
// STM context).
//
// Reference: https://www.chessprogramming.org/Tempo
// ---------------------------------------------------------------------------
EVAL_CONST int TEMPO_BONUS = 15;

}  // namespace eval
}  // namespace LibreChess

#endif  // LIBRECHESS_EVAL_PARAMS_H
