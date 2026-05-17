#ifndef BOARD_PROGRAMS_IDS_H
#define BOARD_PROGRAMS_IDS_H

// ---------------------------------------------------------------------------
// BoardProgramIds — stable string ids accepted by Board::startProgram(id).
// ---------------------------------------------------------------------------

namespace BoardProgramIds {
static constexpr const char* GAME = "game";
static constexpr const char* DIAGNOSTICS = "diagnostics";
static constexpr const char* CALIBRATION = "calibration";
}  // namespace BoardProgramIds

#endif  // BOARD_PROGRAMS_IDS_H