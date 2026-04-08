#ifndef LIBRECHESS_EVAL_PARAMS_H
#define LIBRECHESS_EVAL_PARAMS_H

// ---------------------------------------------------------------------------
// Evaluation parameters — all tunable constants for the evaluation function.
//
// This header is an implementation detail of evaluation.cpp, not a public API.
// It isolates parameter data (values, tables) from evaluation logic (functions,
// hashing, phase interpolation), making parameters easy to find, review, and
// tune in one place.
//
// Under TUNING, parameters have external linkage (mutable at runtime) so the
// offline tuner can read/write them.  Production builds keep them static
// constexpr for full compiler optimisation.
//
// Reference: https://www.chessprogramming.org/Evaluation
// ---------------------------------------------------------------------------

#include "bitboard.h"

// ===========================================================================
// Tuning macros — control linkage and element types for all eval constants.
//
//   EVAL_CONST  — tunable parameters (mutable in tuning builds).
//   EVAL_FIXED  — non-tunable constants that trace.cpp must see.
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

// ===========================================================================
// Material values
// ===========================================================================

// Indexed by piece type offset (P=0 N=1 B=2 R=3 Q=4 K=5).
// Separate MG and EG tables allow the tuner to find phase-optimal piece
// values independently.  MATERIAL (= MATERIAL_MG) defines the centipawn
// unit (PAWN MG = 100 fixed).  materialValue() returns MATERIAL[idx] for
// SEE, lazy eval, delta pruning.
// Reference: https://www.chessprogramming.org/Material
EVAL_CONST MAT_ELEM MATERIAL[]    = {87, 386, 419, 549, 1093, 0};
EVAL_CONST MAT_ELEM MATERIAL_EG[] = {80, 230, 250, 468,  956, 0};

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

// ===========================================================================
// Pawn structure
// ===========================================================================

// Passed pawn rank bonuses — exponential scaling: advanced passers are
// worth dramatically more.  Indexed by LERF rank (0=rank1, 7=rank8).
// Indices 0 and 7 unused (pawns can't occupy rank 1 or rank 8).
// Reference: https://www.chessprogramming.org/Passed_Pawn
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

// ===========================================================================
// Piece bonuses
// ===========================================================================

// Bishop pair — bonus when a side has both bishops.
// Reference: https://www.chessprogramming.org/Bishop_Pair
EVAL_CONST int BISHOP_PAIR_MG = 33;
EVAL_CONST int BISHOP_PAIR_EG = 46;

// Bad bishop — penalty for bishops blocked by own pawns on same color.
// Reference: https://www.chessprogramming.org/Bad_Bishop
EVAL_CONST int BAD_BISHOP_MG = -5;
EVAL_CONST int BAD_BISHOP_EG = -3;

// Knight outposts — bonus for knights on pawn-protected, unattackable
// squares.  Central outposts (d4/d5/e4/e5) receive double the bonus.
// Reference: https://www.chessprogramming.org/Outposts
EVAL_CONST int OUTPOST_BONUS_MG = 15;
EVAL_CONST int OUTPOST_BONUS_EG = 14;

// Trapped pieces — penalty for pieces stuck where pawns block retreat.
// Reference: https://www.chessprogramming.org/Trapped_Pieces
EVAL_CONST int TRAPPED_BISHOP_PENALTY = -120;
EVAL_CONST int TRAPPED_ROOK_PENALTY   =  -34;

// ===========================================================================
// Rook bonuses
// ===========================================================================

// Rook on open/semi-open file.
// Reference: https://www.chessprogramming.org/Rook_on_Open_File
EVAL_CONST int ROOK_OPEN_FILE_MG      = 41;
EVAL_CONST int ROOK_OPEN_FILE_EG      =  0;
EVAL_CONST int ROOK_SEMI_OPEN_FILE_MG = 17;
EVAL_CONST int ROOK_SEMI_OPEN_FILE_EG =  4;

// Rook on 7th rank — bonus when rook is on opponent's second rank and
// enemy king is on back rank.
// Reference: https://www.chessprogramming.org/Rook_on_Seventh
EVAL_CONST int ROOK_7TH_MG = 5;
EVAL_CONST int ROOK_7TH_EG = 26;

// Rook behind passed pawn (Tarrasch Rule) — EG only.
// Reference: https://www.chessprogramming.org/Tarrasch_Rule
EVAL_CONST int ROOK_BEHIND_OWN_PASSER_EG   =  5;
EVAL_CONST int ROOK_BEHIND_ENEMY_PASSER_EG  = -40;

// ===========================================================================
// Mobility — per-piece attack count bonus.
// Reference: https://www.chessprogramming.org/Mobility
// ===========================================================================

EVAL_CONST int MOBILITY_KNIGHT_MG = 7;
EVAL_CONST int MOBILITY_KNIGHT_EG = 5;
EVAL_CONST int MOBILITY_BISHOP_MG = 4;
EVAL_CONST int MOBILITY_BISHOP_EG = 4;
EVAL_CONST int MOBILITY_ROOK_MG   = 2;
EVAL_CONST int MOBILITY_ROOK_EG   = 3;
EVAL_CONST int MOBILITY_QUEEN_MG  = 1;
EVAL_CONST int MOBILITY_QUEEN_EG  = 4;

// ===========================================================================
// King safety
// ===========================================================================

// Pawn shield — penalty for missing or advanced shield pawns.
// Reference: https://www.chessprogramming.org/King_Safety#Pawn_Shield
EVAL_CONST int SHIELD_MISSING_PAWN  = -24;
EVAL_CONST int SHIELD_ADV_RANK3     =   0;  // tuned to 0 — kept as tuning placeholder
EVAL_CONST int SHIELD_ADV_RANK4PLUS =  -9;
EVAL_CONST int SHIELD_OPEN_FILE     = -42;

// King danger — nonlinear penalty based on coordinated attacker weight.
// KING_DANGER_WEIGHT: per-piece-type zone attack weight (N=0, B=1, R=2, Q=3).
// Fixed (not tunable) — tuning shifts danger via TABLE entries instead.
// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
EVAL_FIXED int KING_DANGER_WEIGHT[] = {2, 2, 3, 5};

// Nonlinear danger table — maps total attacker weight to MG penalty (cp).
// TABLE[0] stays fixed at 0; entries 1..12 are tunable.
EVAL_CONST int KING_DANGER_TABLE[] = {0, 0, 8, 8, 11, 11, 26, 40, 60, 105, 167, 167, 231};

// ===========================================================================
// Passed pawn king proximity (EG only)
// ===========================================================================

// Bonus for own king near passer / enemy king far from passer.
// Reference: https://www.chessprogramming.org/King_Distance#Passed_Pawn
EVAL_CONST int PASSER_OWN_KING   = 0;  // tuned to 0 — kept as tuning placeholder
EVAL_CONST int PASSER_ENEMY_KING = 7;

// ===========================================================================
// Space
// ===========================================================================

// Bonus per safe square on files c–f behind own pawn chain.
// Reference: https://www.chessprogramming.org/Space
EVAL_CONST int SPACE_BONUS_MG = 8;

// Space zones — files c–f, ranks 2–4 (White) / ranks 5–7 (Black).
EVAL_FIXED Bitboard WHITE_SPACE_ZONE =
    (FILE_C | FILE_D | FILE_E | FILE_F) & (RANK_2 | RANK_3 | RANK_4);

EVAL_FIXED Bitboard BLACK_SPACE_ZONE =
    (FILE_C | FILE_D | FILE_E | FILE_F) & (RANK_5 | RANK_6 | RANK_7);

// ===========================================================================
// Threats — bonus for lower-value pieces attacking higher-value enemy pieces.
// Reference: https://www.chessprogramming.org/Evaluation#Threats
// ===========================================================================

EVAL_CONST int THREAT_PAWN_VS_MINOR_MG  = 23;
EVAL_CONST int THREAT_PAWN_VS_ROOK_MG   = 13;
EVAL_CONST int THREAT_PAWN_VS_QUEEN_MG  = 15;
EVAL_CONST int THREAT_MINOR_VS_ROOK_MG  = 22;
EVAL_CONST int THREAT_MINOR_VS_QUEEN_MG = 13;
EVAL_CONST int THREAT_ROOK_VS_QUEEN_MG  = 24;

}  // namespace eval
}  // namespace LibreChess

#endif  // LIBRECHESS_EVAL_PARAMS_H
