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

namespace LibreChess {
namespace eval {

#ifdef TUNING
// Force external linkage for EVAL_FIXED symbols — `const` at namespace scope
// has internal linkage by default in C++.  trace.h re-declares these as
// extern so that trace.cpp and tune.cpp can reference them.
extern const int KING_DANGER_WEIGHT[];
extern const int STARTING_PHASE_ONE_SIDE;
extern const Bitboard WHITE_SPACE_ZONE, BLACK_SPACE_ZONE;
#endif

// ===========================================================================
// Material values
// ===========================================================================

// Indexed by piece type offset (P=0 N=1 B=2 R=3 Q=4 K=5).
// Separate MG and EG tables allow the tuner to find phase-optimal piece
// values independently.  MATERIAL (= MATERIAL_MG) defines the centipawn
// unit (PAWN MG = 100 fixed).  materialValue() returns MATERIAL[idx] for
// SEE, lazy eval, delta pruning.
// Reference: https://www.chessprogramming.org/Material
EVAL_CONST MAT_ELEM MATERIAL[] = {80, 382, 398, 497, 1077, 0};
EVAL_CONST MAT_ELEM MATERIAL_EG[] = {80, 232, 257, 453, 850, 0};

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

// --- Midgame PSTs ---

EVAL_CONST PST_ELEM PST_PAWN_MG[64] = {
       0,   0,   0,   0,   0,   0,   0,   0,
      -4,  -4,  -4,  -1,   2,  19,  15,   4,
      -4,  -8,   1,  -1,   7,  -5,   6,  -1,
      -7, -10,  -4,   4,   2,  -1, -10, -14,
       2,  -1,  -2,   2,   6,   2,  -1,  -1,
       1,   0,   1,  -1,   0,   2,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KNIGHT_MG[64] = {
       0, -13,  -2,  -2,  -3,   0, -12,   0,
      -1,  -1,  -1,   5,   4,   0,   0,   0,
      -5,  -2,   6,   2,   3,   7,   3,  -4,
      -3,   0,   4,   3,   7,   3,   0,  -4,
       0,   2,   0,   7,   1,   1,  -5,   0,
       0,   0,   1,   1,   1,   1,   0,   0,
      -2,  -1,   2,   0,   1,   1,   0,  -1,
      -2,   0,   0,   0,   0,  -1,   0,  -1
};

EVAL_CONST PST_ELEM PST_BISHOP_MG[64] = {
       0,  -1,  -5,  -2,  -1, -14,   0,  -1,
      -1,   8,   0,   1,   3,   1,  16,   0,
       0,   1,   3,   1,   5,   3,   1,  -1,
      -1,  -1,  -1,   2,   3,   0,   0,  -2,
      -2,  -4,   0,   1,   2,  -1,  -1,  -2,
      -2,   0,   1,   0,   1,   1,   0,   0,
      -3,  -2,  -1,   0,   0,   0,  -1,  -2,
      -1,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_ROOK_MG[64] = {
       0,   0,   2,   4,   5,  14,  -5,  -7,
      -2,  -1,  -1,  -1,  -1,   0,   0,  -2,
      -2,   0,  -1,   0,   0,   0,   0,   0,
      -1,  -1,  -1,  -1,  -1,   0,   0,   0,
      -1,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
      -1,  -2,   0,   0,  -1,   0,   0,   0,
       1,   0,   1,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_QUEEN_MG[64] = {
       0,  -2,  -1,  10,   0,  -1,   0,   0,
      -1,   0,   2,   6,   8,   1,   0,   0,
      -2,   0,   0,   1,   1,   2,   1,  -1,
      -5,  -1,  -2,  -1,   0,   0,   1,   0,
      -2,  -2,  -1,  -3,   0,   0,   0,  -2,
      -2,  -1,   0,   0,   1,   1,   1,   2,
      -3,  -8,  -1,   0,   0,   1,   0,   1,
      -1,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KING_MG[64] = {
      -1,   2,   1,  -8,  -7,  -2,  17,  -1,
       0,   0,   0,  -6,  -6,   0,   4,   2,
       0,   0,   0,  -1,  -1,   1,   1,  -1,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   1,   0,   0,
       0,   0,   0,   0,   0,   0,   1,   0,
       0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0
};

// --- Endgame PSTs ---

EVAL_CONST PST_ELEM PST_PAWN_EG[64] = {
       0,   0,   0,   0,   0,   0,   0,   0,
      -2,  -2,   2,  -1,   1,   2,  -3,  -7,
      -5,  -3,  -2,   0,   1,  -1,  -4,  -6,
       0,   0,  -3,  -3,  -2,  -3,  -1,  -2,
       5,   2,   1,  -4,  -2,   0,   2,   2,
       5,   2,   1,  -2,  -2,   0,   0,   1,
       1,   1,   0,  -1,  -1,  -1,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KNIGHT_EG[64] = {
       0,  -2,  -1,   0,  -1,   0,  -3,   0,
       0,   0,  -1,   0,   1,   0,   0,   0,
      -1,   0,   0,   1,   1,   0,   0,  -1,
       0,   0,   2,   2,   3,   1,   0,   0,
       0,   1,   1,   3,   2,   1,   0,   0,
       0,  -1,   1,   1,   0,   0,   0,   0,
      -1,  -1,   0,   0,   0,   0,   0,  -1,
      -1,   0,   0,   0,   0,  -1,   0,  -1
};

EVAL_CONST PST_ELEM PST_BISHOP_EG[64] = {
      -1,  -1,  -3,  -1,   0,  -3,   0,  -1,
       0,   0,  -1,   1,   1,   0,   1,  -1,
       0,   1,   2,   2,   4,   1,   0,   0,
       0,   0,   1,   1,   1,   2,   0,  -1,
       0,   1,   1,   1,   1,   0,   0,  -1,
      -1,   0,   1,   1,   1,   1,   0,   0,
      -1,  -1,  -1,   0,   0,   0,   0,  -1,
       0,  -1,   0,   0,  -1,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_ROOK_EG[64] = {
       1,   0,   1,   2,   1,   3,  -1,  -4,
      -1,  -1,   0,  -1,  -1,   0,   0,  -1,
      -1,  -1,  -1,  -1,   0,   0,   0,  -1,
       0,   0,   0,   0,   0,   0,   0,  -1,
       0,   0,   1,   1,   0,   0,   0,   0,
       1,   1,   0,   1,   0,   0,   1,  -1,
      -1,   0,   0,   0,  -1,   0,   0,  -1,
       2,   1,   2,   1,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_QUEEN_EG[64] = {
       0,  -1,   0,  -1,   0,   0,   0,   0,
       0,   0,   0,   1,   1,   0,   0,   0,
      -1,  -1,   0,   0,   1,   1,   1,   0,
      -1,   0,   0,   0,   0,   0,   1,   0,
      -1,   0,  -1,   0,   1,   1,   0,   0,
      -1,  -1,   0,   0,   1,   1,   1,   1,
      -1,  -2,   0,   0,   0,   1,   0,   0,
      -1,   0,   0,   0,   0,   0,   0,   0
};

EVAL_CONST PST_ELEM PST_KING_EG[64] = {
      -1,   0,   0,  -6,  -5,  -2,  -5,  -7,
      -1,   0,   1,  -1,   0,   5,   4,  -2,
      -1,   0,   2,   1,   1,   5,   2,  -1,
      -2,   0,   2,   0,   1,   4,   1,  -2,
      -1,   0,   1,   0,   0,   3,   2,   0,
      -1,   1,   1,   0,   0,   2,   3,   0,
       0,   0,   0,  -1,   0,   1,   1,   0,
       0,   0,   0,   0,  -1,   0,   0,   0
};

// clang-format on

// ===========================================================================
// Pawn structure
// ===========================================================================

// Passed pawn rank bonuses — exponential scaling: advanced passers are
// worth dramatically more.  Indexed by LERF rank (0=rank1, 7=rank8).
// Indices 0 and 7 unused (pawns can't occupy rank 1 or rank 8).
// Reference: https://www.chessprogramming.org/Passed_Pawn
EVAL_CONST int PASSED_RANK_BONUS_MG[] = {0, 0, 0, 0, 5, 10, 22, 0};
EVAL_CONST int PASSED_RANK_BONUS_EG[] = {0, 0, 0, 4, 33, 97, 158, 0};
EVAL_CONST int CONNECTED_PASSED_MG =   0;
EVAL_CONST int CONNECTED_PASSED_EG =   0;
EVAL_CONST int ISOLATED_PENALTY_MG = -17;
EVAL_CONST int ISOLATED_PENALTY_EG =  -6;
EVAL_CONST int DOUBLED_PENALTY_MG  =   0;
EVAL_CONST int DOUBLED_PENALTY_EG  = -14;
EVAL_CONST int BACKWARD_PENALTY_MG =  -4;
EVAL_CONST int BACKWARD_PENALTY_EG =  -3;

// Protected passed pawn — extra bonus when a passer is defended by a pawn.
// Reference: https://www.chessprogramming.org/Passed_Pawn#Protected
EVAL_CONST int PROTECTED_PASSER_MG = 8;

// ===========================================================================
// Piece bonuses
// ===========================================================================

// Bishop pair — bonus when a side has both bishops.
// Reference: https://www.chessprogramming.org/Bishop_Pair
EVAL_CONST int BISHOP_PAIR_MG =  37;
EVAL_CONST int BISHOP_PAIR_EG =  32;

// Bad bishop — penalty for bishops blocked by own pawns on same color.
// Reference: https://www.chessprogramming.org/Bad_Bishop
EVAL_CONST int BAD_BISHOP_MG = -5;
EVAL_CONST int BAD_BISHOP_EG = -5;

// Knight outposts — bonus for knights on pawn-protected, unattackable
// squares.  Central outposts (d4/d5/e4/e5) receive double the bonus.
// Reference: https://www.chessprogramming.org/Outposts
EVAL_CONST int OUTPOST_BONUS_MG =  18;
EVAL_CONST int OUTPOST_BONUS_EG =  14;

// Trapped pieces — penalty for pieces stuck where pawns block retreat.
// Reference: https://www.chessprogramming.org/Trapped_Pieces
EVAL_CONST int TRAPPED_BISHOP_PENALTY = -120;
EVAL_CONST int TRAPPED_ROOK_PENALTY   = -30;

// ===========================================================================
// Rook bonuses
// ===========================================================================

// Rook on open/semi-open file.
// Reference: https://www.chessprogramming.org/Rook_on_Open_File
EVAL_CONST int ROOK_OPEN_FILE_MG      =  45;
EVAL_CONST int ROOK_OPEN_FILE_EG      =   0;
EVAL_CONST int ROOK_SEMI_OPEN_FILE_MG =  16;
EVAL_CONST int ROOK_SEMI_OPEN_FILE_EG =  17;

// Rook on 7th rank — bonus when rook is on opponent's second rank and
// enemy king is on back rank.
// Reference: https://www.chessprogramming.org/Rook_on_Seventh
EVAL_CONST int ROOK_7TH_MG =   5;
EVAL_CONST int ROOK_7TH_EG =  27;

// Rook behind passed pawn (Tarrasch Rule) — EG only.
// Reference: https://www.chessprogramming.org/Tarrasch_Rule
EVAL_CONST int ROOK_BEHIND_OWN_PASSER_EG  =  19;
EVAL_CONST int ROOK_BEHIND_ENEMY_PASSER_EG =   10;

// ===========================================================================
// Mobility — nonlinear per-piece lookup tables indexed by safe attack count.
//
// Safe mobility = attacked squares excluding friendly pieces and enemy pawn
// attacks.  Tables initialized to linear (count × prior weight) to match
// prior eval; the tuner optimizes to a diminishing-returns shape.
//
// Table sizes: max possible attack count per piece type + 1.
//   Knight: max 8, Bishop: max 13, Rook: max 14, Queen: max 27.
//
// Reference: https://www.chessprogramming.org/Mobility
// ===========================================================================

// clang-format off
EVAL_CONST int MOBILITY_KNIGHT_MG[] = {0, -28, -12, -6, 1, 5, 7, 9, 14};
EVAL_CONST int MOBILITY_KNIGHT_EG[] = {0, -30, -27, -17, -9, 8, 6, 13, 1};

EVAL_CONST int MOBILITY_BISHOP_MG[] = {0, 1, 8, 12, 17, 19, 22, 26, 32, 32, 33, 39, 42, 47};
EVAL_CONST int MOBILITY_BISHOP_EG[] = {0, -15, -21, -17, -13, -9, -3, -8, 2, -4, 6, 1, 11, 4};

EVAL_CONST int MOBILITY_ROOK_MG[] = {0, 12, 15, 17, 17, 20, 19, 21, 21, 24, 24, 28, 25, 26, 43};
EVAL_CONST int MOBILITY_ROOK_EG[] = {0, -50, -42, -37, -28, -27, -26, -23, -19, -18, -15, -12, -6, -5, -12};

EVAL_CONST int MOBILITY_QUEEN_MG[] = {0, 7, 9, 11, 12, 16, 16, 13, 14, 13, 8, 7, 7, 4, 5, 3, -6, 1, -1, 6, 23, 11, 18, 58, 29, -24, 40, 20};
EVAL_CONST int MOBILITY_QUEEN_EG[] = {0, -60, -80, -80, -80, -75, -67, -40, -31, -11, 0, 8, 8, 21, 27, 27, 44, 37, 51, 47, 42, 46, 44, 22, 49, 79, 67, 63};
// clang-format on

// ===========================================================================
// King safety
// ===========================================================================

// Pawn shield — penalty for missing or advanced shield pawns.
// Reference: https://www.chessprogramming.org/King_Safety#Pawn_Shield
EVAL_CONST int SHIELD_MISSING_PAWN  = -20;
EVAL_CONST int SHIELD_ADV_RANK3     =   0;
EVAL_CONST int SHIELD_ADV_RANK4PLUS =  -8;
EVAL_CONST int SHIELD_OPEN_FILE     = -40;

// Pawn storm — penalty for enemy pawns advancing toward our castled king.
// Indexed by the enemy pawn's relative rank from the defender's perspective
// (0 = back rank, 7 = starting rank).  Lower rank = closer to king = more
// dangerous.  Values should be lower than open-file penalties per CPW:
// "Penalties for storming enemy pawns must be lower than penalties for
//  (semi)open files, otherwise the pawn storm might backfire."
// Reference: https://www.chessprogramming.org/King_Safety#Pawn_Storm
// clang-format off
EVAL_CONST int PAWN_STORM[] = {0, 0, -16, 0, 0, 0, 0, 0};
// clang-format on

// King danger — nonlinear penalty based on coordinated attacker weight.
// KING_DANGER_WEIGHT: per-piece-type zone attack weight (N=0, B=1, R=2, Q=3).
// Fixed (not tunable) — tuning shifts danger via TABLE entries instead.
// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
EVAL_FIXED int KING_DANGER_WEIGHT[] = {2, 2, 3, 5};

// Nonlinear danger table — maps total attacker weight to MG penalty (cp).
// TABLE[0] stays fixed at 0; entries 1..12 are tunable.
EVAL_CONST int KING_DANGER_TABLE[] = {0, 0, 13, 13, 23, 23, 31, 46, 60, 118, 182, 182, 246};

// Starting phase for one side (full army: 2×N + 2×B + 2×R + Q = 12).
// Used to scale king danger by opponent material fraction.
// Reference: https://www.chessprogramming.org/King_Safety#Scaling
EVAL_FIXED int STARTING_PHASE_ONE_SIDE = 12;

// ===========================================================================
// Passed pawn king proximity (EG only)
// ===========================================================================

// Bonus for own king near passer / enemy king far from passer.
// Reference: https://www.chessprogramming.org/King_Distance#Passed_Pawn
EVAL_CONST int PASSER_OWN_KING   = 0;
EVAL_CONST int PASSER_ENEMY_KING = 5;

// ===========================================================================
// Space
// ===========================================================================

// Bonus per safe square on files c–f behind own pawn chain.
// Reference: https://www.chessprogramming.org/Space
EVAL_CONST int SPACE_BONUS_MG = 5;

// Space zones — files c–f, ranks 2–4 (White) / ranks 5–7 (Black).
EVAL_FIXED Bitboard WHITE_SPACE_ZONE =
    (FILE_C | FILE_D | FILE_E | FILE_F) & (RANK_2 | RANK_3 | RANK_4);

EVAL_FIXED Bitboard BLACK_SPACE_ZONE =
    (FILE_C | FILE_D | FILE_E | FILE_F) & (RANK_5 | RANK_6 | RANK_7);

}  // namespace eval
}  // namespace LibreChess

#endif  // LIBRECHESS_EVAL_PARAMS_H
