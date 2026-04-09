#ifndef LIBRECHESS_SEARCH_H
#define LIBRECHESS_SEARCH_H

// ---------------------------------------------------------------------------
// Chess search engine — alpha-beta with iterative deepening.
//
// Pure C++, no hardware dependencies.  Platform-agnostic timing via
// TimeFunc function pointer (firmware passes millis(), tests pass a mock).
//
// Public entry point: search::findBestMove(pos, limits, state, info).
//
// Also defines the Transposition Table (PackedMove, TTEntry, TTFlag,
// TranspositionTable) — search-internal infrastructure used by SearchState,
// MovePicker, and the Engine facade.
//
// References:
//   https://www.chessprogramming.org/Alpha-Beta
//   https://www.chessprogramming.org/Negamax
//   https://www.chessprogramming.org/Quiescence_Search
//   https://www.chessprogramming.org/Iterative_Deepening
//   https://www.chessprogramming.org/Transposition_Table
// ---------------------------------------------------------------------------

#include <atomic>
#include <cstdint>

#include "hash_table.h"
#include "move.h"
#include "position.h"

namespace LibreChess {

// Forward declarations for hash table types owned by the eval layer.
namespace eval {
struct PawnHashTable;
struct EvalHashTable;
}  // namespace eval

namespace search {

// ---------------------------------------------------------------------------
// Packed move — 16-bit encoding for compact storage in TT, killers, and PV.
//
// Layout: from (6 bits) | to (6 bits) | flags (4 bits).
// Reconstructed into a Move for use.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// TT node type — determines how the stored score relates to alpha/beta.
// ---------------------------------------------------------------------------

enum class TTFlag : uint8_t {
  EXACT,         // PV node — score is exact
  LOWER_BOUND,   // Beta cutoff — score is a lower bound (>= beta)
  UPPER_BOUND,   // All-node — score is an upper bound (<= alpha)
};

// ---------------------------------------------------------------------------
// TT entry — 12 bytes (4+2+2+1+1+1 = 11, padded to 12).
//
// Truncated key (upper 32 bits) avoids full 64-bit comparison.
// Reference: https://www.chessprogramming.org/Transposition_Table#Entry
// ---------------------------------------------------------------------------

struct TTEntry {
  uint32_t   key32;       // Upper 32 bits of Zobrist hash (collision guard)
  int16_t    score;       // Stored score (mate-adjusted for ply distance)
  PackedMove bestMove;    // Best move from this position
  int8_t     depth;       // Search depth that produced this entry
  TTFlag     flag;        // EXACT / LOWER_BOUND / UPPER_BOUND
  uint8_t    generation;  // Search generation — stale entries replaced cheaply
};

static_assert(sizeof(TTEntry) <= 16, "TTEntry should fit in 16 bytes");

// Default TT size: 4096 entries (64 KiB).  LibreChessProvider may further
// cap TT size dynamically based on available heap.
static constexpr int DEFAULT_TT_SIZE = 4096;

// ---------------------------------------------------------------------------
// Transposition Table — power-of-2 array with depth-preferred replacement.
//
// Inherits resize / free / clear from HashTableBase<TTEntry>.
// Adds generation tracking and TT-specific probe / store.
//
// Reference: https://www.chessprogramming.org/Transposition_Table
//            https://www.chessprogramming.org/Replacement_Strategy
// ---------------------------------------------------------------------------

struct TranspositionTable : HashTableBase<TTEntry> {
  uint8_t generation = 0;  // Current search generation

  // Advance the generation counter.  Called at the start of each search.
  // Stale entries (from previous generations) are replaced cheaply.
  void newGeneration() { generation = static_cast<uint8_t>(generation + 1); }

  // Probe the table for a matching entry.  Returns nullptr on miss.
  inline const TTEntry* probe(uint64_t hash) const {
    if (!entries) return nullptr;
    int index = static_cast<int>(hash & mask);
    const TTEntry& e = entries[index];
    if (e.key32 == static_cast<uint32_t>(hash >> 32))
      return &e;
    return nullptr;
  }

  // Store an entry with depth-preferred replacement.
  // Replaces existing entries when: slot is empty, same position (update),
  // entry is from a stale generation, new entry is exact, or new depth
  // is >= existing depth.
  // Reference: https://www.chessprogramming.org/Replacement_Strategy
  inline void store(uint64_t hash, int score, Move bestMove,
                    int depth, TTFlag flag) {
    if (!entries) return;
    int index = static_cast<int>(hash & mask);
    uint32_t key32 = static_cast<uint32_t>(hash >> 32);
    TTEntry& e = entries[index];

    bool replace = (e.key32 == 0 && e.depth == 0)    // empty slot
                || (e.key32 == key32)                 // same position (update)
                || (e.generation != generation)       // stale entry from old search
                || (flag == TTFlag::EXACT)            // exact scores always preferred
                || (depth >= e.depth);                // deeper search preferred
    if (!replace) return;

    e.key32      = key32;
    e.score      = static_cast<int16_t>(score);
    e.bestMove   = packMove(bestMove);
    e.depth      = static_cast<int8_t>(depth);
    e.flag       = flag;
    e.generation = generation;
  }
};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int MATE_SCORE = 30000;
static constexpr int MAX_PLY    = 48;

// Maximum PV line length stored per ply.  Practical search depths rarely
// exceed 25-30 plies including extensions; 24 provides headroom
// while keeping the PV table at ~2.3 KiB (48 × 24 × 2B).
static constexpr int MAX_PV_LEN = 24;

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
  uint32_t softTimeMs    = 0;         // Soft time limit: stop after current
                                      //   iteration completes (0 = no limit)
  uint32_t hardTimeMs    = 0;         // Hard time limit: abort mid-search
                                      //   (0 = no limit)
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

  // Principal variation — the best line of play from the last completed
  // iteration.  pv[0] == bestMove.  Populated by the triangular PV table
  // inside negamax.
  // Reference: https://www.chessprogramming.org/Triangular_PV-Table
  Move pv[MAX_PV_LEN];
  int pvLength = 0;
};

// ---------------------------------------------------------------------------
// SearchState — mutable state for a single search invocation.
//
// Owns the transposition table, killers, and history heuristic (added
// incrementally).  Currently: node counter, stop control, TT.
// ---------------------------------------------------------------------------

struct SearchState {
  // Construct with infrastructure pointers (set once, persist across calls).
  // All parameters optional; omitted pointers default to nullptr.
  explicit SearchState(TimeFunc tf = nullptr,
                       TranspositionTable* ttPtr = nullptr,
                       eval::PawnHashTable* ph = nullptr,
                       eval::EvalHashTable* eh = nullptr);

  uint32_t nodes = 0;       // Node counter (incremented per negamax call)
  bool stopped   = false;   // Internal stop flag (set when time/depth exceeded)

  // Time control
  TimeFunc timeFunc = nullptr;
  uint32_t startTime = 0;
  uint32_t hardTimeMs = 0;
  std::atomic<bool>* externalStop = nullptr;

  // Transposition table (externally owned, nullable).
  TranspositionTable* tt = nullptr;

  // Pawn hash table (externally owned, nullable).
  // Caches pawn structure MG/EG scores — ~95%+ hit rate in typical searches.
  eval::PawnHashTable* pawnHash = nullptr;

  // Evaluation hash table (externally owned, nullable).
  // Caches full evaluatePosition() results — avoids redundant evaluations.
  eval::EvalHashTable* evalHash = nullptr;

  // --- Move ordering heuristics ---
  // Killer moves: two per ply, quiet moves that caused beta cutoffs.
  // Stored as PackedMove (2 bytes) for compactness; unpacked in MovePicker.
  // Reference: https://www.chessprogramming.org/Killer_Move
  PackedMove killers[MAX_PLY][2];

  // History heuristic: [color][pieceType-1][toSquare] — piece-to history.
  // Compact alternative to butterfly [from][to] boards.  ~1.5 KiB.
  // Reference: https://www.chessprogramming.org/History_Heuristic
  int16_t history[2][6][64];

  // Capture history: [attackerType-1][victimType-1][toSquare] — scores
  // for captures, distinguished by attacker and victim piece types.
  // Indexed as captureHistory[raw(attackerType)-1][raw(victimType)-1][to].
  // ~4.5 KiB (compact: color-agnostic attacker dimension).
  // Reference: https://www.chessprogramming.org/History_Heuristic#Capture_History
  int16_t captureHistory[6][6][64];

  // Countermove heuristic: for each (piece, toSquare) of the previous move,
  // stores the quiet move that caused a beta cutoff in response.  Used as a
  // 3rd-tier ordering hint (between killers and history).
  // Indexed by [pieceIndex(0..11)][toSquare(0..63)].  ~1.5 KiB.
  // Reference: https://www.chessprogramming.org/Countermove_Heuristic
  PackedMove countermoves[12][64];

  // Static eval at each ply — used to compute the "improving" flag.
  // A position is improving if its static eval exceeds the eval from 2
  // plies ago, informing RFP, LMP, and LMR decisions.
  // Reference: https://www.chessprogramming.org/Improving
  int16_t staticEvals[MAX_PLY];

  // Triangular PV table — collects the principal variation during search.
  // pv[ply] holds the PV line starting at that ply; pvLength[ply] holds
  // the number of moves in that line.  Updated in negamax when alpha
  // improves; copied to SearchResult after each completed iteration.
  // Stored as PackedMove (2 bytes) to reduce heap footprint.
  // Memory: MAX_PLY × MAX_PV_LEN × sizeof(PackedMove) ≈ 2.3 KiB (heap-allocated).
  // Reference: https://www.chessprogramming.org/Triangular_PV-Table
  PackedMove pv[MAX_PLY][MAX_PV_LEN];
  int8_t pvLength[MAX_PLY];

  // Reset heuristic tables to zero.  Called by findBestMove() at start.
  void clearHeuristics();

  // Check time periodically (every 512 nodes).  Sets `stopped` if limit
  // exceeded or external cancellation requested.
  void checkTime();
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Find the best move for the current side to move.
// Iterative deepening: searches depth 1 → maxDepth, returning the best
// result from the last completed iteration.
// `pos` is modified during search (make/unmake) but restored before returning.
// `state` is the pre-allocated SearchState (owned by the caller), constructed
//   with infrastructure pointers (timeFunc, TT, pawnHash, evalHash).
//   findBestMove resets per-search transients (nodes, stopped, startTime,
//   hardTimeMs, externalStop, heuristics) from `limits` each call.
// `info` is called after each completed iteration (nullptr to skip).
SearchResult findBestMove(Position& pos, const SearchLimits& limits,
                          SearchState& state,
                          InfoCallback info = nullptr);

}  // namespace search
}  // namespace LibreChess

#endif  // LIBRECHESS_SEARCH_H
