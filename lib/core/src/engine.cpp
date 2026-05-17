#include "engine.h"

// ---------------------------------------------------------------------------
// Engine — search resource ownership and invocation facade.
//
// Centralises transposition table, pawn hash, eval hash, and SearchState
// lifecycle.  Both UCIState and Game compose this class instead of
// duplicating resource management.
// ---------------------------------------------------------------------------

namespace LibreChess {

namespace {

// RAII guard around the engine's busy_ flag.  Converts the acquire-CAS /
// release-store pair into a scoped lock so we can use early-return on
// contention without forgetting to clear the flag.  `owned()` tells the
// caller whether acquisition succeeded; if not, the method must bail out.
class BusyGuard {
 public:
  explicit BusyGuard(std::atomic<bool>& flag) : flag_(flag) {
    bool expected = false;
    owned_ = flag_.compare_exchange_strong(expected, true,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed);
  }
  ~BusyGuard() {
    if (owned_) flag_.store(false, std::memory_order_release);
  }
  BusyGuard(const BusyGuard&) = delete;
  BusyGuard& operator=(const BusyGuard&) = delete;

  bool owned() const { return owned_; }

 private:
  std::atomic<bool>& flag_;
  bool owned_;
};

}  // namespace

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
  BusyGuard guard(busy_);
  if (!guard.owned()) return search::SearchResult{};

  // Build internal limits with our stop flag wired in
  search::SearchLimits internalLimits;
  internalLimits.maxDepth = limits.maxDepth;
  internalLimits.softTimeMs = limits.softTimeMs;
  internalLimits.hardTimeMs = limits.hardTimeMs;
  internalLimits.rootMoves = limits.rootMoves;
  internalLimits.rootMoveCount = limits.rootMoveCount;
  internalLimits.rootScores = limits.rootScores;
  internalLimits.rootScoreCapacity = limits.rootScoreCapacity;
  internalLimits.rootScoreCount = limits.rootScoreCount;

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
  // Refuse to nuke state out from under an in-flight search.  Caller must
  // wait for calculateMove() to return before clearing.
  BusyGuard guard(busy_);
  if (!guard.owned()) return;
  tt_.clear();
  pawnHash_.clear();
  evalHash_.clear();
  searchState_.clearHeuristics();
}

void Engine::resizeTT(int entries) {
  // Same rationale as clearState(): reallocating the TT while a search holds
  // pointers into it would free memory from under findBestMove().
  BusyGuard guard(busy_);
  if (!guard.owned()) return;
  tt_.free();
  tt_.resize(entries);
}

bool Engine::hashTablesReady() const {
  return tt_.isAllocated() && pawnHash_.isAllocated() && evalHash_.isAllocated();
}

bool Engine::hashTableAllocationFailed() const {
  return tt_.allocationFailed() || pawnHash_.allocationFailed() ||
         evalHash_.allocationFailed();
}

}  // namespace LibreChess
