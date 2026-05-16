#ifndef BOARD_SERVICES_PROGRAM_FACTORY_H
#define BOARD_SERVICES_PROGRAM_FACTORY_H

#include "board/services/program/program.h"
#include "board/services/registry.h"

#include <memory>
#include <stdint.h>

class BoardAnimations;
class BoardMenuRunner;
class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardProgramContext — construction inputs for board program factories.
// ---------------------------------------------------------------------------

struct BoardProgramContext {
  BoardRuntime* runtime;
  BoardAnimations* animations;
  BoardMenuRunner* menuRunner;
};

// ---------------------------------------------------------------------------
// BoardProgramFactory — fixed registry of named program creators.
// ---------------------------------------------------------------------------

using BoardProgramFactory = BoardRegistry<BoardProgram, BoardProgramContext>;
using BoardProgramCreator = BoardProgramFactory::Creator;

#endif  // BOARD_SERVICES_PROGRAM_FACTORY_H