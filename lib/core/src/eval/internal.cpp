// ---------------------------------------------------------------------------
// eval/internal.cpp — single-definition storage for the shared eval/detail
// data tables declared `extern` in internal.h.
//
// Rationale: g++ 5.1 (native test toolchain) does not support C++17 `inline`
// variables, so we cannot define `inline constexpr` tables in the header.
// Instead the header exposes `extern const` declarations and this TU owns
// the single definition using `constexpr` (which implies internal linkage
// for variables, so we drop `static` and keep it plain `constexpr`).
// All other eval TUs refer to these via external linkage declarations in
// internal.h.
// ---------------------------------------------------------------------------

#include "internal.h"

namespace LibreChess {
namespace eval {
namespace detail {

// Constexpr constructors run at compile time; the storage lives in .rodata.
extern const PawnMasks     PAWN_MASKS      = PawnMasks{};
extern const PawnRankMasks PAWN_RANK_MASKS = PawnRankMasks{};
extern const int           SIDE_SIGN[2]    = {1, -1};
extern const Color         COLORS[2]       = {Color::WHITE, Color::BLACK};

}  // namespace detail
}  // namespace eval
}  // namespace LibreChess
