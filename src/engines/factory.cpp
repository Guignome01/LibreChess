#include "engines/factory.h"

#include "engines/librechess/assistance.h"
#include "engines/librechess/engine.h"
#include "engines/lichess/engine.h"
#include "engines/stockfish/engine.h"
#include "logger.h"

namespace Engines {
namespace {

String normalized(String value) {
  value.toLowerCase();
  return value;
}

}  // namespace

EngineProvider* createOpponentEngine(const String& engineName,
                                     LibreChess::Game* game,
                                     int difficultyLevel,
                                     char playerColor,
                                     LibreChess::ILogger* logger) {
  const String id = normalized(engineName);
  if (id == "librechess") {
    return new LibreChessEngine(game, difficultyLevel, playerColor, logger);
  }
  return new StockfishEngine(difficultyLevel, playerColor, logger);
}

EngineProvider* createLichessEngine(const LichessConfig& config,
                                    LibreChess::ILogger* logger) {
  return new LichessEngine(config, logger);
}

std::unique_ptr<BoardAssistanceProvider> createAssistanceProvider(
    BoardAssistanceLevel level,
    const String& engineName,
    int difficultyLevel,
    LibreChess::Game* game,
    LibreChess::ILogger* logger) {
  if (level == BoardAssistanceLevel::NONE) {
    return std::unique_ptr<BoardAssistanceProvider>(new BoardNoAssistanceProvider());
  }
  if (level == BoardAssistanceLevel::LEGAL_MOVES) {
    return std::unique_ptr<BoardAssistanceProvider>(new BoardLegalMoveAssistanceProvider());
  }

  const String id = normalized(engineName);
  if (id == "librechess") {
    return std::unique_ptr<BoardAssistanceProvider>(
        new LibreChessAssistanceProvider(game, difficultyLevel, logger));
  }

  LibreChess::Log log(logger);
  log.errorf("Unsupported best-move assistance engine '%s'; assistance disabled", id.c_str());
  return std::unique_ptr<BoardAssistanceProvider>(new BoardNoAssistanceProvider());
}

}  // namespace Engines
