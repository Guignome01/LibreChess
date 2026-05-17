#ifndef ENGINES_LIBRECHESS_BOARD_ASSISTANCE_H
#define ENGINES_LIBRECHESS_BOARD_ASSISTANCE_H

#include "board/assistance_provider.h"
#include "engines/librechess/engine.h"
#include "game.h"

// ---------------------------------------------------------------------------
// LibreChessAssistanceProvider — lifted-piece ranking backed by Game search.
// ---------------------------------------------------------------------------
// This is independent from BotMode's opponent EngineProvider. It ranks the
// already-generated legal targets for the piece the player lifted and returns
// board DTO data only; LEDs/sensors remain owned by the board program.
// ---------------------------------------------------------------------------

class LibreChessAssistanceProvider final : public BoardAssistanceProvider {
 public:
  explicit LibreChessAssistanceProvider(LibreChess::Game* game,
                                        int level = LibreChessEngine::DEFAULT_LEVEL,
                                        LibreChess::ILogger* logger = nullptr);

  BoardAssistanceLevel level() const override { return BoardAssistanceLevel::BEST_MOVE; }
  bool rankTargets(int fromRow, int fromCol, const BoardMoveTargetList& targets,
                   BoardMoveTargetRanking& ranking) override;

 private:
  bool ensureSearchReady();

  LibreChess::Game* game_;
  LibreChess::ILogger* logger_;
};

#endif  // ENGINES_LIBRECHESS_BOARD_ASSISTANCE_H
