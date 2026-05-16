#ifndef BOARD_PROGRAMS_FACTORY_H
#define BOARD_PROGRAMS_FACTORY_H

#include "board/services/program/factory.h"

/// Register the board's built-in primary programs with the provided factory.
void registerBoardPrograms(BoardProgramFactory& factory);

#endif  // BOARD_PROGRAMS_FACTORY_H