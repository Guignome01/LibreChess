#ifndef ENGINES_FACTORY_H
#define ENGINES_FACTORY_H

#include "board/assistance_provider.h"
#include "engines/types.h"
#include "provider.h"

#include <Arduino.h>
#include <memory>

namespace LibreChess {
class Game;
class ILogger;
}  // namespace LibreChess

struct LichessConfig;

namespace Engines {

EngineProvider* createOpponentEngine(const String& engineName,
                                     LibreChess::Game* game,
                                     int difficultyLevel,
                                     char playerColor,
                                     LibreChess::ILogger* logger = nullptr);

EngineProvider* createLichessEngine(const LichessConfig& config,
                                    LibreChess::ILogger* logger = nullptr);

std::unique_ptr<BoardAssistanceProvider> createAssistanceProvider(
    BoardAssistanceLevel level,
    const String& engineName,
    int difficultyLevel,
    LibreChess::Game* game,
    LibreChess::ILogger* logger = nullptr);

}  // namespace Engines

#endif  // ENGINES_FACTORY_H
