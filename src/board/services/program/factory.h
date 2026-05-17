#ifndef BOARD_SERVICES_PROGRAM_FACTORY_H
#define BOARD_SERVICES_PROGRAM_FACTORY_H

#include "board/services/program/program.h"
#include "board/services/registry.h"

#include <memory>
#include <stdint.h>

class BoardAnimations;
class BoardAssistanceProvider;
class BoardMenuRunner;
class BoardRuntime;

// ---------------------------------------------------------------------------
// BoardProgramContext — construction inputs for board program factories.
// ---------------------------------------------------------------------------
// `assistanceProvider` is the board-owned active assistance provider. The
// factory passes it to the game program at construction so the program can
// service hints from the engine and surface them on the assistance surface.
// ---------------------------------------------------------------------------

struct BoardProgramContext {
  BoardRuntime* runtime;
  BoardAnimations* animations;
  BoardMenuRunner* menuRunner;
  BoardAssistanceProvider* assistanceProvider;
};

// ---------------------------------------------------------------------------
// BoardProgramFactory — fixed registry of named program creators.
// ---------------------------------------------------------------------------

using BoardProgramFactory = BoardRegistry<BoardProgram, BoardProgramContext>;
using BoardProgramCreator = BoardProgramFactory::Creator;

#endif  // BOARD_SERVICES_PROGRAM_FACTORY_H