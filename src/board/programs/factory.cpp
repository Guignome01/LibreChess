#include "board/programs/factory.h"

#include "board/programs/calibration/program.h"
#include "board/programs/diagnostics/program.h"
#include "board/programs/game/program.h"
#include "board/programs/ids.h"
#include "board/services/program/factory.h"

namespace {

std::unique_ptr<BoardProgram> createDiagnosticsProgram(BoardProgramContext& context) {
  return std::unique_ptr<BoardProgram>(
      new BoardDiagnostics(*context.runtime, *context.animations));
}

std::unique_ptr<BoardProgram> createGameProgram(BoardProgramContext& context) {
  std::unique_ptr<BoardGame> game(
      new BoardGame(*context.runtime, *context.animations, *context.menuRunner));
  // Seed the program with the board's currently-installed assistance provider.
  // `Board::setAssistanceProvider()` re-binds the live provider afterwards.
  game->setAssistanceProvider(context.assistanceProvider);
  return std::unique_ptr<BoardProgram>(game.release());
}

std::unique_ptr<BoardProgram> createCalibrationProgram(BoardProgramContext& context) {
  (void)context;
  return std::unique_ptr<BoardProgram>(new BoardCalibration());
}

}  // namespace

void registerBoardPrograms(BoardProgramFactory& factory) {
  factory.registerCreator(BoardProgramIds::GAME, createGameProgram);
  factory.registerCreator(BoardProgramIds::DIAGNOSTICS, createDiagnosticsProgram);
  factory.registerCreator(BoardProgramIds::CALIBRATION, createCalibrationProgram);
}