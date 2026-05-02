#ifndef LIBRECHESS_TIME_MANAGEMENT_H
#define LIBRECHESS_TIME_MANAGEMENT_H

// ---------------------------------------------------------------------------
// Time management — compute search time limits from clock parameters.
//
// Pure function, header-only.  Used by the UCI handler to convert
// wtime/btime/winc/binc/movestogo into soft/hard time limits that
// SearchLimits understands.
//
// Formula:
//   safeRemaining = max(1, remaining - 50)        // 50 ms move overhead
//
//   With movestogo:
//     softTime = safeRemaining / movestogo + increment
//   Without movestogo (sudden death):
//     softTime = safeRemaining / 30 + increment / 2
//
//   hardTime = min(safeRemaining / 4, softTime * 4)
//
//   Final clamp:
//     softTime = min(softTime, hardTime, safeRemaining)
//     hardTime = min(hardTime, safeRemaining)
//
// Reference: https://www.chessprogramming.org/Time_Management
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstdint>

#include "search.h"

namespace LibreChess {
namespace time_management {

// ---------------------------------------------------------------------------
// Compute search time limits from UCI clock parameters.
//
// Parameters:
//   wtime, btime    — milliseconds remaining for white/black (0 = unused)
//   winc, binc      — increment per move in ms (0 = none)
//   movestogo       — moves until next time control (0 = sudden death)
//   sideToMove      — whose clock to use (Color::WHITE or Color::BLACK)
//
// Returns:
//   SearchLimits with softTimeMs and hardTimeMs set.
//   maxDepth is left at MAX_PLY (caller can override).
//   stop pointer is left null (caller must wire it).
// ---------------------------------------------------------------------------
inline search::SearchLimits computeTimeLimits(uint32_t wtime, uint32_t btime,
                                              uint32_t winc, uint32_t binc,
                                              int movestogo, Color sideToMove) {
  search::SearchLimits limits;

  uint64_t remaining = (sideToMove == Color::WHITE) ? wtime : btime;
  uint64_t increment = (sideToMove == Color::WHITE) ? winc : binc;

  // Safety margin — never use all remaining time
  uint64_t safeRemaining = (remaining > 50) ? remaining - 50 : 1;

  uint64_t softTime;
  if (movestogo > 0) {
    softTime = safeRemaining / static_cast<uint32_t>(movestogo) + increment;
  } else {
    softTime = safeRemaining / 30 + increment / 2;
  }
  if (softTime == 0) softTime = 1;

  // Hard limit: never exceed 1/4 of remaining time or 4× soft time
  uint64_t quarterRemaining = safeRemaining / 4;
  if (quarterRemaining == 0) quarterRemaining = 1;
  uint64_t hardTime = std::min(quarterRemaining, softTime * 4);
  if (hardTime == 0) hardTime = 1;

  // Ensure soft ≤ hard ≤ remaining
  softTime = std::min(softTime, safeRemaining);
  hardTime = std::min(hardTime, safeRemaining);
  softTime = std::min(softTime, hardTime);

  limits.softTimeMs = static_cast<uint32_t>(softTime);
  limits.hardTimeMs = static_cast<uint32_t>(hardTime);

  return limits;
}

}  // namespace time_management
}  // namespace LibreChess

#endif  // LIBRECHESS_TIME_MANAGEMENT_H
