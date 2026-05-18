#include "engines/librechess/assistance.h"

#include <Arduino.h>
#include <atomic>
#include <new>

namespace {

static constexpr uint32_t ASSISTANCE_SEARCH_STACK_BYTES = 65536;

struct AssistanceSearchProfile {
  uint32_t timeMs;
  int maxDepth;
  int ttEntries;
};

static constexpr AssistanceSearchProfile ASSISTANCE_LEVELS[] = {
    {300, 2, 512},    {600, 3, 512},    {1000, 4, 1024},
    {1500, 5, 1024},  {2200, 6, 2048},  {3000, 7, 2048},
    {4000, 8, 4096},  {5000, 10, 4096},
};

AssistanceSearchProfile profileForLevel(int level) {
  if (level < 1) level = 1;
  if (level > 8) level = 8;
  return ASSISTANCE_LEVELS[level - 1];
}

struct AssistanceSearchContext {
  LibreChess::Game* game = nullptr;
  int fromRow = -1;
  int fromCol = -1;
  uint32_t timeLimitMs = 0;
  int maxDepth = 0;
  LibreChess::Game::CandidateTargetList targets;
  LibreChess::Game::CandidateTargetScoreList scores;
  std::atomic<bool> cancel{false};
  std::atomic<bool> ready{false};
  bool ok = false;
};

void assistanceSearchTask(void* param) {
  auto* ctx = static_cast<AssistanceSearchContext*>(param);
  ctx->game->setExternalStop(&ctx->cancel);
  ctx->ok = ctx->game->rankCandidateTargets(ctx->fromRow, ctx->fromCol,
                                            ctx->targets, ctx->timeLimitMs,
                                            ctx->scores, ctx->maxDepth);
  ctx->game->setExternalStop(nullptr);
  ctx->ready.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

bool runSearchTask(LibreChess::Game* game, int fromRow, int fromCol,
                   const LibreChess::Game::CandidateTargetList& targets,
                   uint32_t timeLimitMs,
                   int maxDepth,
                   LibreChess::Game::CandidateTargetScoreList& scores,
                   LibreChess::ILogger* logger) {
  scores.clear();

  auto* ctx = new (std::nothrow) AssistanceSearchContext();
  if (!ctx) {
    if (logger) logger->error("LibreChess assistance: failed to allocate search task context");
    return false;
  }

  ctx->game = game;
  ctx->fromRow = fromRow;
  ctx->fromCol = fromCol;
  ctx->timeLimitMs = timeLimitMs;
  ctx->maxDepth = maxDepth;
  ctx->targets = targets;

  if (xTaskCreate(assistanceSearchTask, "lcAssist", ASSISTANCE_SEARCH_STACK_BYTES,
                  ctx, 1, nullptr) != pdPASS) {
    if (logger) logger->error("LibreChess assistance: xTaskCreate failed");
    delete ctx;
    return false;
  }

  while (!ctx->ready.load(std::memory_order_acquire)) delay(1);

  const bool ok = ctx->ok;
  if (ok) scores = ctx->scores;
  delete ctx;
  return ok;
}

bool fillRankingFromScores(const LibreChess::Game::CandidateTargetScoreList& scores,
                           BoardMoveTargetRanking& ranking) {
  if (scores.count <= 0) return false;

  int bestScore = scores.scores[0].score;
  int worstScore = scores.scores[0].score;
  int bestIndex = 0;
  int worstIndex = 0;
  for (int scoreIndex = 1; scoreIndex < scores.count; ++scoreIndex) {
    const int score = scores.scores[scoreIndex].score;
    if (score > bestScore) {
      bestScore = score;
      bestIndex = scoreIndex;
    }
    if (score < worstScore) {
      worstScore = score;
      worstIndex = scoreIndex;
    }
  }

  ranking.valid = true;
  ranking.bestRow = scores.scores[bestIndex].row;
  ranking.bestCol = scores.scores[bestIndex].col;
  ranking.worstRow = scores.scores[worstIndex].row;
  ranking.worstCol = scores.scores[worstIndex].col;
  return true;
}

bool fillRankingFromStaticEval(LibreChess::Game* game, int fromRow, int fromCol,
                               const BoardMoveTargetList& targets,
                               BoardMoveTargetRanking& ranking) {
  bool haveScore = false;
  int bestScore = 0;
  int worstScore = 0;
  uint8_t bestIndex = 0;
  uint8_t worstIndex = 0;

  for (uint8_t targetIndex = 0; targetIndex < targets.count; ++targetIndex) {
    const BoardMoveTarget& target = targets.targets[targetIndex];
    int score = 0;
    if (!game->scoreCandidateMove(fromRow, fromCol, target.row, target.col, score)) continue;

    if (!haveScore) {
      haveScore = true;
      bestScore = score;
      worstScore = score;
      bestIndex = targetIndex;
      worstIndex = targetIndex;
      continue;
    }
    if (score > bestScore) {
      bestScore = score;
      bestIndex = targetIndex;
    }
    if (score < worstScore) {
      worstScore = score;
      worstIndex = targetIndex;
    }
  }

  if (!haveScore) return false;

  const BoardMoveTarget& best = targets.targets[bestIndex];
  const BoardMoveTarget& worst = targets.targets[worstIndex];
  ranking.valid = true;
  ranking.bestRow = best.row;
  ranking.bestCol = best.col;
  ranking.worstRow = worst.row;
  ranking.worstCol = worst.col;
  return true;
}

}  // namespace

LibreChessAssistanceProvider::LibreChessAssistanceProvider(LibreChess::Game* game, int level,
                                                           LibreChess::ILogger* logger)
    : game_(game), logger_(logger) {
  const AssistanceSearchProfile profile = profileForLevel(level);
  searchTimeMs_ = profile.timeMs;
  maxDepth_ = profile.maxDepth;
  ttEntries_ = profile.ttEntries;
}

bool LibreChessAssistanceProvider::ensureSearchReady() {
  if (!game_) return false;
  if (!game_->searchInitialized()) {
    game_->initSearch(ttEntries_);
    if (!game_->searchInitialized()) {
      if (logger_) logger_->error("LibreChess assistance: search init failed");
      return false;
    }
  }
  game_->setTimeFunc([]() -> uint32_t { return millis(); });
  return true;
}

bool LibreChessAssistanceProvider::rankTargets(int fromRow, int fromCol,
                                               const BoardMoveTargetList& targets,
                                               BoardMoveTargetRanking& ranking) {
  ranking = BoardMoveTargetRanking{};
  if (!game_ || game_->isGameOver() || targets.count == 0) return false;

  LibreChess::Game::CandidateTargetList candidateTargets;
  for (uint8_t targetIndex = 0; targetIndex < targets.count; ++targetIndex) {
    const BoardMoveTarget& target = targets.targets[targetIndex];
    candidateTargets.add(target.row, target.col);
  }

  LibreChess::Game::CandidateTargetScoreList searchedScores;
  if (ensureSearchReady() &&
      runSearchTask(game_, fromRow, fromCol, candidateTargets,
            searchTimeMs_, maxDepth_, searchedScores, logger_) &&
      fillRankingFromScores(searchedScores, ranking)) {
    return true;
  }

  return fillRankingFromStaticEval(game_, fromRow, fromCol, targets, ranking);
}
