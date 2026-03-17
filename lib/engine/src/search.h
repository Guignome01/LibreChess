#ifndef LIBRECHESS_SEARCH_H
#define LIBRECHESS_SEARCH_H

// ---------------------------------------------------------------------------
// Chess search engine — alpha-beta with iterative deepening.
//
// Pure C++, no hardware dependencies.  Platform-agnostic timing via
// TimeFunc function pointer (firmware passes millis(), tests pass a mock).
//
// Public entry point: search::findBestMove(pos, limits, timeFunc, info).
//
// References:
//   https://www.chessprogramming.org/Alpha-Beta
//   https://www.chessprogramming.org/Negamax
//   https://www.chessprogramming.org/Quiescence_Search
//   https://www.chessprogramming.org/Iterative_Deepening
// ---------------------------------------------------------------------------

#include <atomic>
#include <cstdint>

#include "move.h"
#include "position.h"

namespace LibreChess {
namespace search {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int MATE_SCORE = 30000;
static constexpr int INF_SCORE  = 31000;
static constexpr int DRAW_SCORE = 0;
static constexpr int MAX_PLY    = 128;

// ---------------------------------------------------------------------------
// Platform-agnostic time function — returns milliseconds.
// Firmware passes millis(); tests pass a mock or nullptr.
// ---------------------------------------------------------------------------

using TimeFunc = uint32_t (*)(void);

// ---------------------------------------------------------------------------
// Iteration callback — invoked after each completed ID iteration.
// Used by UCI to emit "info depth ... score ..." lines during search.
// nullptr if no callback is needed.
// ---------------------------------------------------------------------------

struct SearchResult;  // forward declaration for InfoCallback
using InfoCallback = void (*)(const SearchResult&);

// ---------------------------------------------------------------------------
// SearchLimits — caller-specified constraints on the search.
// ---------------------------------------------------------------------------

struct SearchLimits {
  int maxDepth           = MAX_PLY;   // Depth limit (1-based)
  uint32_t maxTimeMs     = 0;         // Time limit in ms (0 = no limit)
  std::atomic<bool>* stop = nullptr;  // External cancellation flag (nullable)
};

// ---------------------------------------------------------------------------
// SearchResult — output from findBestMove().
// ---------------------------------------------------------------------------

struct SearchResult {
  Move bestMove;        // Best move found (from=0, to=0 if no legal moves)
  int score    = 0;     // Score in centipawns (side-to-move relative)
  int depth    = 0;     // Deepest completed iteration
  uint32_t nodes = 0;   // Total nodes searched
};

// ---------------------------------------------------------------------------
// SearchState — mutable state for a single search invocation.
//
// Owns the transposition table, killers, and history heuristic (added
// incrementally).  Currently: node counter, stop control, TT.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Transposition Table types.
//
// Each TTEntry is 16 bytes.  Default 8192 entries = 128 KiB — fits ESP32
// comfortably.  Always-replace scheme (simple, good enough with ID rewriting
// entries each iteration).
//
// Reference: https://www.chessprogramming.org/Transposition_Table
// ---------------------------------------------------------------------------

// TT node type — determines how the stored score relates to alpha/beta.
enum class TTFlag : uint8_t {
  EXACT,         // PV node — score is exact
  LOWER_BOUND,   // Beta cutoff — score is a lower bound (>= beta)
  UPPER_BOUND,   // All-node — score is an upper bound (<= alpha)
};

// Packed move for TT storage — from (6 bits) + to (6 bits) + flags (4 bits).
// Fits in 16 bits; reconstructed into a Move for use.
using PackedMove = uint16_t;

inline PackedMove packMove(Move m) {
  return static_cast<PackedMove>(m.from)
       | (static_cast<PackedMove>(m.to) << 6)
       | (static_cast<PackedMove>(m.flags) << 12);
}

inline Move unpackMove(PackedMove pm) {
  Move m;
  m.from  = pm & 0x3F;
  m.to    = (pm >> 6) & 0x3F;
  m.flags = (pm >> 12) & 0x0F;
  return m;
}

// A single transposition table entry — 16 bytes.
// Truncated key (upper 32 bits) avoids full 64-bit comparison.
struct TTEntry {
  uint32_t   key32;       // Upper 32 bits of Zobrist hash (collision guard)
  int16_t    score;       // Stored score (mate-adjusted for ply distance)
  PackedMove bestMove;    // Best move from this position
  int8_t     depth;       // Search depth that produced this entry
  TTFlag     flag;        // EXACT / LOWER_BOUND / UPPER_BOUND
};

static_assert(sizeof(TTEntry) <= 16, "TTEntry should fit in 16 bytes");

// Default TT size: 8192 entries × 16 bytes = 128 KiB.
static constexpr int DEFAULT_TT_SIZE = 8192;

// The transposition table — a power-of-2 array with index = key & mask.
struct TranspositionTable {
  TTEntry* entries = nullptr;
  int size  = 0;  // Number of entries (power of 2)
  int mask  = 0;  // size - 1

  // Allocate entries.  `numEntries` is rounded down to the nearest power of 2.
  void resize(int numEntries);

  // Release memory.
  void free();

  // Clear all entries (zero-fill).
  void clear();

  // Probe the table for a matching entry.  Returns nullptr on miss.
  const TTEntry* probe(uint64_t hash) const;

  // Store an entry (always-replace).
  void store(uint64_t hash, int score, Move bestMove, int depth, TTFlag flag);
};

struct SearchState {
  uint32_t nodes = 0;       // Node counter (incremented per negamax call)
  bool stopped   = false;   // Internal stop flag (set when time/depth exceeded)

  // Time control
  TimeFunc timeFunc = nullptr;
  uint32_t startTime = 0;
  uint32_t maxTimeMs = 0;
  std::atomic<bool>* externalStop = nullptr;

  // Transposition table (externally owned, nullable).
  TranspositionTable* tt = nullptr;

  // --- Move ordering heuristics ---
  // Killer moves: two per ply, quiet moves that caused beta cutoffs.
  // Reference: https://www.chessprogramming.org/Killer_Move
  Move killers[MAX_PLY][2];

  // History heuristic: [color][from][to] — accumulated quiet move scores.
  // Reference: https://www.chessprogramming.org/History_Heuristic
  int16_t history[2][64][64];

  // Initialize killer and history tables to zero.
  void clearHeuristics();

  // Check time every 1024 nodes.  Sets `stopped` if limit exceeded or
  // external cancellation requested.
  void checkTime() {
    if (stopped) return;
    if (externalStop && externalStop->load(std::memory_order_relaxed)) {
      stopped = true;
      return;
    }
    if (maxTimeMs > 0 && timeFunc) {
      uint32_t elapsed = timeFunc() - startTime;
      if (elapsed >= maxTimeMs) stopped = true;
    }
  }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Find the best move for the current side to move.
// Iterative deepening: searches depth 1 → maxDepth, returning the best
// result from the last completed iteration.
// `pos` is modified during search (make/unmake) but restored before returning.
// `timeFunc` may be nullptr if no time limit is needed.
// `info` is called after each completed iteration (nullptr to skip).
// `tt` is an optional transposition table (nullptr to skip TT).
SearchResult findBestMove(Position& pos, const SearchLimits& limits,
                          TimeFunc timeFunc = nullptr,
                          InfoCallback info = nullptr,
                          TranspositionTable* tt = nullptr);

}  // namespace search
}  // namespace LibreChess

#endif  // LIBRECHESS_SEARCH_H
