#ifndef LIBRECHESS_TIME_MANAGEMENT_H
#define LIBRECHESS_TIME_MANAGEMENT_H

// ---------------------------------------------------------------------------
// Time management — compute search time limits from clock parameters.
//
// Pure function, header-only.  Used by the UCI handler to convert
// wtime/btime/winc/binc/movestogo into soft/hard time limits that
// SearchLimits understands.
//
// Formula (no movestogo):
//   softTime = remaining / 30 + increment / 2
//   hardTime = min(remaining / 4, softTime * 4)
//
// Formula (with movestogo):
//   softTime = remaining / movestogo + increment
//   hardTime = min(remaining / 4, softTime * 4)
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

  uint32_t remaining = (sideToMove == Color::WHITE) ? wtime : btime;
  uint32_t increment = (sideToMove == Color::WHITE) ? winc : binc;

  // Safety margin — never use all remaining time
  uint32_t safeRemaining = (remaining > 50) ? remaining - 50 : 1;

  uint32_t softTime;
  if (movestogo > 0) {
    softTime = safeRemaining / static_cast<uint32_t>(movestogo) + increment;
  } else {
    softTime = safeRemaining / 30 + increment / 2;
  }

  // Hard limit: never exceed 1/4 of remaining time or 4× soft time
  uint32_t hardTime = std::min(safeRemaining / 4, softTime * 4);

  // Ensure soft ≤ hard ≤ remaining
  softTime = std::min(softTime, safeRemaining);
  hardTime = std::min(hardTime, safeRemaining);
  softTime = std::min(softTime, hardTime);

  limits.softTimeMs = softTime;
  limits.hardTimeMs = hardTime;

  return limits;
}

}  // namespace time_management
}  // namespace LibreChess

#endif  // LIBRECHESS_TIME_MANAGEMENT_H
