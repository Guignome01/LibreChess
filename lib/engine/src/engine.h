#ifndef LIBRECHESS_ENGINE_H
#define LIBRECHESS_ENGINE_H

// ---------------------------------------------------------------------------
// Engine — direct-call facade for the chess search engine.
//
// Replaces the UCI string-streaming layer (UCIHandler / UCIStream) with a
// structured API.  Owns a Position, TranspositionTable, and stop control.
//
// Usage (LibreChessProvider):
//   Engine engine(ttEntries);
//   engine.setTimeFunc(millis);
//   auto result = engine.calculateMove(fen, limits);
//   // result.bestMove, result.score, result.depth, result.nodes
//
// Usage (tests):
//   Engine engine(64);
//   SearchLimits limits; limits.maxDepth = 4;
//   auto r = engine.calculateMove("startpos fen...", limits);
//   TEST_ASSERT(r.bestMove.from != 0 || r.bestMove.to != 0);
// ---------------------------------------------------------------------------

#include <atomic>
#include <string>

#include "position.h"
#include "search.h"

namespace LibreChess {

class Engine {
 public:
  // Construct with optional TT size (number of entries).
  explicit Engine(int ttSize = search::DEFAULT_TT_SIZE);
  ~Engine();

  // Reset engine state: clear TT, reset position to startpos.
  void newGame();

  // Run search on the given FEN position with the specified limits.
  // Loads the FEN, wires the stop flag, calls findBestMove(), and
  // returns the structured result (bestMove, score, depth, nodes).
  search::SearchResult calculateMove(const std::string& fen,
                                     const search::SearchLimits& limits);

  // Set the time function (firmware passes millis(), tests pass a mock).
  void setTimeFunc(search::TimeFunc fn) { timeFunc_ = fn; }

  // Signal the engine to stop searching (thread-safe).
  void stop() { stop_.store(true, std::memory_order_relaxed); }

  // Wire an external stop flag (e.g. ctx->cancel in EngineProvider).
  // When set, calculateMove() will use this flag for SearchLimits::stop
  // instead of the internal one.  The caller must ensure the pointer
  // outlives any in-progress search.
  void setExternalStop(std::atomic<bool>* flag) { externalStop_ = flag; }

  // Read-only access to the internal position (for testing).
  const Position& position() const { return pos_; }

 private:
  Position pos_;
  search::TranspositionTable tt_;
  std::atomic<bool> stop_{false};
  std::atomic<bool>* externalStop_ = nullptr;
  search::TimeFunc timeFunc_ = nullptr;
};

}  // namespace LibreChess

#endif  // LIBRECHESS_ENGINE_H
