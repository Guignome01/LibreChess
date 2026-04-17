#include "engine.h"

// ---------------------------------------------------------------------------
// Engine — search resource ownership and invocation facade.
//
// Centralises transposition table, pawn hash, eval hash, and SearchState
// lifecycle.  Both UCIState and Game compose this class instead of
// duplicating resource management.
// ---------------------------------------------------------------------------

namespace LibreChess {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Engine::Engine(int ttSize)
    : searchState_(nullptr, &tt_, &pawnHash_, &evalHash_) {
  tt_.resize(ttSize);
  pawnHash_.resize(eval::DEFAULT_PAWN_HASH_SIZE);
  evalHash_.resize(eval::DEFAULT_EVAL_HASH_SIZE);
}

Engine::~Engine() {
  tt_.free();
  pawnHash_.free();
  evalHash_.free();
}

// ---------------------------------------------------------------------------
// Search invocation
// ---------------------------------------------------------------------------

search::SearchResult Engine::calculateMove(Position& pos,
                                           const search::SearchLimits& limits,
                                           search::InfoCallback info) {
  // Build internal limits with our stop flag wired in
  search::SearchLimits internalLimits;
  internalLimits.maxDepth = limits.maxDepth;
  internalLimits.softTimeMs = limits.softTimeMs;
  internalLimits.hardTimeMs = limits.hardTimeMs;

  searchStop_.store(false, std::memory_order_relaxed);
  internalLimits.stop = externalStop_ ? externalStop_ : &searchStop_;

  return search::findBestMove(pos, internalLimits, searchState_, info);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void Engine::setTimeFunc(search::TimeFunc fn) {
  searchState_.timeFunc = fn;
}

void Engine::setExternalStop(std::atomic<bool>* flag) {
  externalStop_ = flag;
}

// ---------------------------------------------------------------------------
// Resource management
// ---------------------------------------------------------------------------

void Engine::clearState() {
  tt_.clear();
  pawnHash_.clear();
  evalHash_.clear();
  searchState_.clearHeuristics();
}

void Engine::resizeTT(int entries) {
  tt_.free();
  tt_.resize(entries);
}

}  // namespace LibreChess
