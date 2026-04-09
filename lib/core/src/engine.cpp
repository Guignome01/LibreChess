#include "engine.h"

#include "fen.h"

// ---------------------------------------------------------------------------
// Engine — direct-call facade for the chess search engine.
//
// Thin wrapper over search::findBestMove() that owns a Position, TT,
// hash tables, and SearchState.  All per-search state is pre-allocated
// in the constructor, eliminating per-move heap fragmentation.
// ---------------------------------------------------------------------------

namespace LibreChess {

// ===========================================================================
// Construction / destruction
// ===========================================================================

Engine::Engine(int ttSize)
    : state_(nullptr, &tt_, &pawnHash_, &evalHash_) {
  tt_.resize(ttSize);
  pawnHash_.resize(eval::DEFAULT_PAWN_HASH_SIZE);
  evalHash_.resize(eval::DEFAULT_EVAL_HASH_SIZE);
  pos_.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

Engine::~Engine() {
  tt_.free();
  pawnHash_.free();
  evalHash_.free();
}

// ===========================================================================
// State management
// ===========================================================================

void Engine::newGame() {
  tt_.clear();
  pawnHash_.clear();
  evalHash_.clear();
  pos_.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

// ===========================================================================
// Search
// ===========================================================================

search::SearchResult Engine::calculateMove(const std::string& fen,
                                           const search::SearchLimits& limits) {
  pos_.loadFEN(fen);

  // Build internal limits with our stop flag wired in
  search::SearchLimits internalLimits;
  internalLimits.maxDepth = limits.maxDepth;
  internalLimits.softTimeMs = limits.softTimeMs;
  internalLimits.hardTimeMs = limits.hardTimeMs;

  stop_.store(false, std::memory_order_relaxed);
  internalLimits.stop = externalStop_ ? externalStop_ : &stop_;

  return search::findBestMove(pos_, internalLimits, state_);
}

}  // namespace LibreChess
