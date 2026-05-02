#ifndef LIBRECHESS_PROVIDER_H
#define LIBRECHESS_PROVIDER_H

#include "engine/engine_provider.h"
#include "game.h"

// ---------------------------------------------------------------------------
// EngineProvider implementation for the built-in LibreChess engine.
//
// Runs the search on-board via a FreeRTOS background task.  Uses
// Game::calculateMove() — the Game class owns the Position, TT, hash
// tables, and SearchState.  This preserves the firmware firewall: the
// provider never imports core search internals directly.
//
// Game::initSearch() is called once during initialize(), allocating search
// resources that persist for the game's lifetime.  This eliminates heap
// fragmentation from per-move alloc/free cycles and enables cross-move
// transposition table reuse for stronger play.
// ---------------------------------------------------------------------------

class LibreChessProvider : public EngineProvider {
 public:
  /// Provider identity stored in GameMeta for game resume.
  static constexpr uint8_t ENGINE_ID = 1;

  /// 8 difficulty levels for the on-board engine (depths 1–8).
  static constexpr int LEVEL_COUNT = 8;
  static constexpr DifficultyLevel LEVELS[LEVEL_COUNT] = {
      {"Beginner", 1},  {"Easy", 2},         {"Intermediate", 3},
      {"Medium", 4},    {"Advanced", 5},      {"Hard", 6},
      {"Expert", 7},    {"Master", 8},
  };
  static constexpr int DEFAULT_LEVEL = 4;  // Medium

  /// Construct from a 1-based difficulty level (1–8). Clamped to valid range.
  /// game must outlive this provider.
  explicit LibreChessProvider(LibreChess::Game* game,
                              int level = DEFAULT_LEVEL,
                              char playerColor = 'w',
                              ILogger* logger = nullptr);
  ~LibreChessProvider() override;

  bool initialize(EngineInitResult& result) override;
  void requestMove(const std::string& fen) override;
  bool checkResult(EngineResult& result) override;
  int getEvaluation() override;

 private:
  struct TaskContext : BaseTaskContext {
    std::string fen;
    int depth;
    LibreChess::Game* game;  // Non-owning — points to game_ below
  };

  static void taskFunction(void* param);

  LibreChess::Game* game_;  // Owned by BotMode, passed at construction
  int level_;
  int depth_;
  char playerColor_;
  int currentEvaluation_ = 0;
};

#endif  // LIBRECHESS_PROVIDER_H
