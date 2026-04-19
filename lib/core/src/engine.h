#ifndef LIBRECHESS_ENGINE_H
#define LIBRECHESS_ENGINE_H

// ---------------------------------------------------------------------------
// Engine — search resource ownership and invocation facade.
//
// Owns the transposition table, pawn hash table, evaluation hash table,
// and SearchState.  Provides a minimal API to run the search engine on
// an externally-owned Position.
//
// Composed by both UCIState (core, CLI/SPRT) and Game (game lib, firmware).
// Eliminates duplicated resource management between those two consumers.
//
// Engine does NOT own Position — callers own their Position instances and
// pass them by reference to calculateMove().
//
// Reference: https://www.chessprogramming.org/UCI
// ---------------------------------------------------------------------------

#include <atomic>

#include "evaluation.h"
#include "search.h"

namespace LibreChess {

class Engine {
 public:
  // Construct with TT size (in entries).  Allocates TT, pawn hash,
  // eval hash, and SearchState.  All resources persist until destruction.
  explicit Engine(int ttSize = search::DEFAULT_TT_SIZE);
  ~Engine();

  // Non-copyable, non-movable (owns heap resources via hash tables).
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // --- Search invocation ---

  // Run the search on the given position with the specified limits.
  // Wires the stop flag automatically.  Thread-safe stop via setExternalStop.
  // `info` callback is invoked after each completed iteration (nullable).
  search::SearchResult calculateMove(Position& pos,
                                     const search::SearchLimits& limits,
                                     search::InfoCallback info = nullptr);

  // --- Configuration ---

  // Set the platform time function (firmware passes millis()).
  void setTimeFunc(search::TimeFunc fn);

  // Wire an external stop flag for cooperative search cancellation.
  void setExternalStop(std::atomic<bool>* flag);

  // --- Resource management ---

  // Clear all hash tables and search heuristics.  Call between games
  // (UCI `ucinewgame`, Game::newGame).
  void clearState();

  // Resize the transposition table (frees old, allocates new).
  // Used by UCI `setoption name Hash value <MB>`.
  void resizeTT(int entries);

  // --- Direct access (for consumers that need fine-grained control) ---

  search::SearchState& searchState() { return searchState_; }
  const search::SearchState& searchState() const { return searchState_; }

 private:
  search::TranspositionTable tt_;
  eval::PawnHashTable pawnHash_;
  eval::EvalHashTable evalHash_;
  search::SearchState searchState_;
  std::atomic<bool> searchStop_{false};
  std::atomic<bool>* externalStop_ = nullptr;
  // Re-entry guard — flipped to true for the duration of calculateMove() and
  // any resource-mutating call (clearState, resizeTT).  A concurrent call
  // from another task is a programming error, not a supported use case; the
  // offender fails fast with an empty SearchResult or no-op instead of
  // corrupting the TT / hash tables in flight.  Cheaper than std::mutex
  // (1 byte RAM, no FreeRTOS handle) and keeps the single-task discipline
  // explicit.
  std::atomic<bool> busy_{false};
};

}  // namespace LibreChess

#endif  // LIBRECHESS_ENGINE_H
