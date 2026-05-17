#include "engines/librechess/assistance.h"

#include <Arduino.h>

namespace {

static constexpr uint32_t BEST_MOVE_SEARCH_TIME_MS = 1000;
static constexpr int ASSISTANCE_TT_ENTRIES = 1024;

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
  (void)level;
}

bool LibreChessAssistanceProvider::ensureSearchReady() {
  if (!game_) return false;
  if (!game_->searchInitialized()) {
    game_->initSearch(ASSISTANCE_TT_ENTRIES);
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
      game_->rankCandidateTargets(fromRow, fromCol, candidateTargets,
                                  BEST_MOVE_SEARCH_TIME_MS, searchedScores) &&
      fillRankingFromScores(searchedScores, ranking)) {
    return true;
  }

  return fillRankingFromStaticEval(game_, fromRow, fromCol, targets, ranking);
}
