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
#include "piece.h"
#include "position.h"

namespace LibreChess {

// Forward declarations for hash table types owned by the eval layer.
namespace eval {
struct PawnHashTable;
struct EvalHashTable;
}  // namespace eval

namespace search {

// ---------------------------------------------------------------------------
// Packed move — lossless 16-bit encoding for TT, killers, PV table.
//
// Layout: from (6 bits) | to (6 bits) | move type (4 bits).
//
// Move type encodes the mutually-exclusive special-move classes:
//   0x0 = quiet, 0x1 = capture, 0x2 = EP capture, 0x3 = castling,
//   0x4..0x7 = quiet promotion (low 2 bits = promo index),
//   0x8..0xB = capture promotion (low 2 bits = promo index).
//
// This preserves all flags including the 2-bit promotion piece index
// (Knight=0, Bishop=1, Rook=2, Queen=3), which the prior 4-bit
// truncation was silently losing (bits 4-5 overflowed uint16_t).
//
// Reference: https://www.chessprogramming.org/Encoding_Moves
// ---------------------------------------------------------------------------

using PackedMove = uint16_t;

inline PackedMove packMove(Move m) {
  uint16_t packed = static_cast<uint16_t>(m.from)
                  | (static_cast<uint16_t>(m.to) << 6);
  uint16_t type = 0;
  if (m.isPromotion()) {
    type = m.isCapture() ? 0x8 : 0x4;
    type |= m.promoIndex();
  } else if (m.isEP()) {
    type = 0x2;
  } else if (m.isCastling()) {
    type = 0x3;
  } else if (m.isCapture()) {
    type = 0x1;
  }
  return packed | (type << 12);
}

inline Move unpackMove(PackedMove pm) {
  Move m;
  m.from = pm & 0x3F;
  m.to   = (pm >> 6) & 0x3F;
  uint8_t type = (pm >> 12) & 0x0F;
  switch (type) {
  case 0x0: m.flags = 0; break;
  case 0x1: m.flags = MOVE_CAPTURE; break;
  case 0x2: m.flags = MOVE_CAPTURE | MOVE_EP; break;
  case 0x3: m.flags = MOVE_CASTLING; break;
  default:
    // 0x4..0x7 = quiet promotion, 0x8..0xB = capture promotion.
    m.flags = (type >= 0x8 ? MOVE_CAPTURE : 0)
            | Move::promoFlags(type & 0x3);
    break;
  }
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

// Default TT size: 4096 entries (64 KiB).  LibreChessEngine may further
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
// Score classification helpers — eliminate raw MATE_SCORE comparisons.
//
// Mate scores are encoded as MATE_SCORE - plies (winning) or
// -MATE_SCORE + plies (losing).  These helpers centralise the range
// check and the conversion to "mate in N moves" for UCI output.
//
// Reference: https://www.chessprogramming.org/Mate_Score
// ---------------------------------------------------------------------------

/// True when the score indicates a forced win (mate-giver side).
constexpr bool isMateWin(int score) {
  return score >= MATE_SCORE - MAX_PLY;
}

/// True when the score indicates a forced loss (mated side).
constexpr bool isMateLoss(int score) {
  return score <= -MATE_SCORE + MAX_PLY;
}

/// True when the score is any kind of mate (win or loss).
constexpr bool isMateScore(int score) {
  return isMateWin(score) || isMateLoss(score);
}

/// Convert a mate score to signed "mate in N moves" for UCI output.
/// Positive = mating, negative = being mated.  Undefined for non-mate scores.
constexpr int mateMovesFromScore(int score) {
  if (isMateWin(score))  return  (MATE_SCORE - score + 1) / 2;
  /* isMateLoss */       return -(MATE_SCORE + score + 1) / 2;
}

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

  // Optional root move restriction. When set, findBestMove() searches only
  // legal root moves whose from/to squares match an entry in this list. Flags
  // are ignored so promotion alternatives for the same target square remain
  // available to the search.
  const Move* rootMoves = nullptr;
  int rootMoveCount = 0;

  // Optional per-root score sink. Filled after each completed iteration with
  // scores from the latest complete root search, in the current root move
  // order. Callers may pass nullptr to ignore root scores.
  ScoredMove* rootScores = nullptr;
  int rootScoreCapacity = 0;
  int* rootScoreCount = nullptr;
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

// ---------------------------------------------------------------------------
// SearchStack — per-ply search state (Stockfish/Ethereal "Stack" convention).
//
// Separates per-ply fields from per-search state (SearchState).  A fixed
// `SearchStack stack[MAX_PLY + 4]` array is stack-allocated in
// findBestMove().  Root search uses `ss = &stack[1]` with `stack[0]` as a
// sentinel so `(ss - 1)` at root safely yields Piece::NONE/0.  Recursion
// advances by `ss + 1`, and each ss already carries its own `ply` so
// callees rarely need to pass it explicitly.
//
// Reference: https://www.chessprogramming.org/Stack
// ---------------------------------------------------------------------------

struct SearchStack {
  int         ply           = 0;                   // distance from root
  int16_t     staticEval    = 0;                   // -INF_SCORE when in check
  PackedMove  excludedMove  = 0;                   // non-zero during SE probe
  PackedMove  killers[2]    = {0, 0};              // beta-cutoff quiet moves
  PackedMove  pv[MAX_PV_LEN]{};                    // PV line starting here
  int8_t      pvLength      = 0;                   // moves in pv[]
  // Identity of the move played FROM this ply to reach (ss+1).  Read by
  // the child via (ss-1)->movedPiece / (ss-1)->movedTo to index the
  // countermove table and drive recapture extensions.
  Piece       movedPiece    = Piece::NONE;
  int16_t     movedTo       = 0;
};

// ---------------------------------------------------------------------------
// SearchState — mutable per-search state (not per-ply).
//
// Holds infrastructure pointers, persistent heuristic tables, node counters,
// and time-management transients.  Per-ply fields (killers, static eval,
// PV, current move) now live in SearchStack, advanced by recursion.
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

  // --- Move ordering heuristics (per-search, not per-ply) ---

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

  // --- Opening book ---
  // When true, findBestMove() probes the internal opening book before
  // iterative deepening.  Opt-in to avoid interfering with search tests.
  bool useBook = false;

  // xorshift64 PRNG state for random book-move selection.  Seeded per-game
  // so that repeated positions yield varied play across games.
  uint64_t bookRng = 0x12345678ABCDEF01ULL;

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
