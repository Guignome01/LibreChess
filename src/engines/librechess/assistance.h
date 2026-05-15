#ifndef ENGINES_LIBRECHESS_BOARD_ASSISTANCE_H
#define ENGINES_LIBRECHESS_BOARD_ASSISTANCE_H

#include "board/assistance_provider.h"
#include "engines/librechess/engine.h"
#include "game.h"

#include <string>

// ---------------------------------------------------------------------------
// LibreChessAssistanceProvider — board best-move callback backed by LibreChess.
// ---------------------------------------------------------------------------
// This is independent from BotMode's opponent EngineProvider. It exposes only
// the board hint callback contract and never touches LEDs, sensors, or board
// runtime internals.
// ---------------------------------------------------------------------------

class LibreChessAssistanceProvider final : public BoardAssistanceProvider {
 public:
  explicit LibreChessAssistanceProvider(LibreChess::Game* game,
                                        int level = LibreChessEngine::DEFAULT_LEVEL,
                                        LibreChess::ILogger* logger = nullptr);
  ~LibreChessAssistanceProvider() override;

  BoardAssistanceLevel level() const override { return BoardAssistanceLevel::BEST_MOVE; }
  bool service(BoardBestMoveHint& hint) override;
  void cancel() override;

 private:
  enum class State : uint8_t { IDLE, PENDING, DISPLAYED };

  bool initializeIfNeeded();
  bool requestBestMove();
  bool pollBestMove(BoardBestMoveHint& hint);
  BoardBestMoveHint mapResult(const std::string& coordinateMove) const;

  LibreChess::Game* game_;
  LibreChessEngine provider_;
  bool initialized_ = false;
  State state_ = State::IDLE;
  std::string requestedFen_;
};

#endif  // ENGINES_LIBRECHESS_BOARD_ASSISTANCE_H
