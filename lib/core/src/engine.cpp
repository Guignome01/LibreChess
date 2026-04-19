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
  // Re-entry guard: if another task is already inside the engine, bail out
  // rather than racing on TT / hash tables.  See busy_ comment in engine.h.
  bool expected = false;
  if (!busy_.compare_exchange_strong(expected, true,
                                     std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
    return search::SearchResult{};
  }

  // Build internal limits with our stop flag wired in
  search::SearchLimits internalLimits;
  internalLimits.maxDepth = limits.maxDepth;
  internalLimits.softTimeMs = limits.softTimeMs;
  internalLimits.hardTimeMs = limits.hardTimeMs;

  searchStop_.store(false, std::memory_order_relaxed);
  internalLimits.stop = externalStop_ ? externalStop_ : &searchStop_;

  search::SearchResult result =
      search::findBestMove(pos, internalLimits, searchState_, info);
  busy_.store(false, std::memory_order_release);
  return result;
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
  // Refuse to nuke state out from under an in-flight search.  Caller must
  // wait for calculateMove() to return before clearing.
  bool expected = false;
  if (!busy_.compare_exchange_strong(expected, true,
                                     std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
    return;
  }
  tt_.clear();
  pawnHash_.clear();
  evalHash_.clear();
  searchState_.clearHeuristics();
  busy_.store(false, std::memory_order_release);
}

void Engine::resizeTT(int entries) {
  // Same rationale as clearState(): reallocating the TT while a search holds
  // pointers into it would free memory from under findBestMove().
  bool expected = false;
  if (!busy_.compare_exchange_strong(expected, true,
                                     std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
    return;
  }
  tt_.free();
  tt_.resize(entries);
  busy_.store(false, std::memory_order_release);
}

}  // namespace LibreChess
