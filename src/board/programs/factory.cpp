#include "board/programs/factory.h"

#include "board/programs/diagnostics/diagnostics_program.h"
#include "board/programs/ids.h"
#include "board/services/program/factory.h"

namespace {

std::unique_ptr<BoardProgram> createDiagnosticsProgram(BoardProgramContext& context) {
  return std::unique_ptr<BoardProgram>(
      new BoardDiagnosticsProgram(*context.runtime, *context.animations));
}

}  // namespace

void registerBoardPrograms(BoardProgramFactory& factory) {
  factory.registerCreator(BoardProgramIds::DIAGNOSTICS, createDiagnosticsProgram);
}