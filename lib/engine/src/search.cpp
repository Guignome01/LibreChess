#include "search.h"

#include <cstring>

#include "evaluation.h"
#include "movegen.h"
#include "piece.h"

// ---------------------------------------------------------------------------
// LibreChess search engine — negamax with alpha-beta, quiescence, ID, TT.
//
// Negamax convention: the score returned is always relative to the side to
// move.  evaluatePosition() returns white-relative centipawns, so at the
// leaf we negate for Black: score = (WHITE) ? eval : -eval.
//
// References:
//   https://www.chessprogramming.org/Negamax
//   https://www.chessprogramming.org/Alpha-Beta
//   https://www.chessprogramming.org/Quiescence_Search
//   https://www.chessprogramming.org/Transposition_Table
// ---------------------------------------------------------------------------

namespace LibreChess {
namespace search {

using namespace piece;

// ===========================================================================
// TranspositionTable implementation
// ===========================================================================

// Round down to the nearest power of 2.
static int roundDownPow2(int n) {
  if (n <= 0) return 0;
  int v = 1;
  while (v * 2 <= n) v *= 2;
  return v;
}

void TranspositionTable::resize(int numEntries) {
  free();
  size = roundDownPow2(numEntries);
  if (size == 0) return;
  mask = size - 1;
  entries = new TTEntry[size]();  // value-initialized (zeroed)
}

void TranspositionTable::free() {
  delete[] entries;
  entries = nullptr;
  size = 0;
  mask = 0;
}

void TranspositionTable::clear() {
  if (entries && size > 0)
    std::memset(entries, 0, sizeof(TTEntry) * size);
}

const TTEntry* TranspositionTable::probe(uint64_t hash) const {
  if (!entries) return nullptr;
  int index = static_cast<int>(hash & mask);
  const TTEntry& e = entries[index];
  if (e.key32 == static_cast<uint32_t>(hash >> 32))
    return &e;
  return nullptr;
}

void TranspositionTable::store(uint64_t hash, int score, Move bestMove,
                               int depth, TTFlag flag) {
  if (!entries) return;
  int index = static_cast<int>(hash & mask);
  TTEntry& e = entries[index];
  e.key32    = static_cast<uint32_t>(hash >> 32);
  e.score    = static_cast<int16_t>(score);
  e.bestMove = packMove(bestMove);
  e.depth    = static_cast<int8_t>(depth);
  e.flag     = flag;
}

void SearchState::clearHeuristics() {
  std::memset(killers, 0, sizeof(killers));
  std::memset(history, 0, sizeof(history));
}

namespace {

// ---------------------------------------------------------------------------
// Node check interval — every 1024 nodes, poll time and external stop.
// ---------------------------------------------------------------------------

constexpr uint32_t CHECK_INTERVAL = 1024;

// ---------------------------------------------------------------------------
// Mate score adjustment for TT storage.
//
// Mate scores are relative to the root: -MATE_SCORE + ply.  When storing
// in the TT we convert to "distance from this node" so the entry is valid
// regardless of the root's ply.  On retrieval, we convert back.
//
// Reference: https://www.chessprogramming.org/Transposition_Table#Mate_Scores
// ---------------------------------------------------------------------------

// Convert root-relative mate score to TT-storable form.
inline int scoreToTT(int score, int ply) {
  if (score >= MATE_SCORE - MAX_PLY) return score + ply;
  if (score <= -MATE_SCORE + MAX_PLY) return score - ply;
  return score;
}

// Convert TT-stored mate score back to root-relative.
inline int scoreFromTT(int score, int ply) {
  if (score >= MATE_SCORE - MAX_PLY) return score - ply;
  if (score <= -MATE_SCORE + MAX_PLY) return score + ply;
  return score;
}

// ---------------------------------------------------------------------------
// Move ordering — MVV-LVA, TT move, killers, history.
//
// Score priority bands (highest first):
//   TT move:    30000
//   Captures:   10000 + MVV-LVA (victim*16 - attacker)
//   Killer 1:    9000
//   Killer 2:    8000
//   Quiets:     history[color][from][to]  (0 .. ~7000)
//
// References:
//   https://www.chessprogramming.org/Move_Ordering
//   https://www.chessprogramming.org/MVV-LVA
//   https://www.chessprogramming.org/Killer_Move
//   https://www.chessprogramming.org/History_Heuristic
// ---------------------------------------------------------------------------

static constexpr int SCORE_TT_MOVE  = 30000;
static constexpr int SCORE_CAPTURE  = 10000;
static constexpr int SCORE_KILLER_1 = 9000;
static constexpr int SCORE_KILLER_2 = 8000;

// Simple piece value for MVV-LVA (indexed by PieceType).
// PieceType: NONE=0, PAWN=1, KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5, KING=6.
static constexpr int MVV_LVA_VALUE[] = {0, 1, 3, 3, 5, 9, 0};

// Assign ordering scores to all moves in the list.
// Uses a parallel `scores[]` array (caller must provide MAX_MOVES capacity).
void assignScores(const MoveList& moves, int scores[],
                  const Piece mailbox[], Move ttMove,
                  int ply, Color side, const SearchState& state) {
  uint8_t c = raw(side);
  for (int i = 0; i < moves.count; ++i) {
    const Move& m = moves.moves[i];

    // TT move — highest priority
    if (m.from == ttMove.from && m.to == ttMove.to &&
        m.flags == ttMove.flags) {
      scores[i] = SCORE_TT_MOVE;
      continue;
    }

    // Captures — MVV-LVA
    if (m.isCapture()) {
      PieceType victim  = pieceType(mailbox[m.to]);
      PieceType attacker = pieceType(mailbox[m.from]);
      // EP: victim is on a different square, but type is always pawn
      if (m.isEP()) victim = PieceType::PAWN;
      scores[i] = SCORE_CAPTURE +
                  MVV_LVA_VALUE[raw(victim)] * 16 -
                  MVV_LVA_VALUE[raw(attacker)];
      continue;
    }

    // Killer moves
    if (m == state.killers[ply][0]) {
      scores[i] = SCORE_KILLER_1;
      continue;
    }
    if (m == state.killers[ply][1]) {
      scores[i] = SCORE_KILLER_2;
      continue;
    }

    // Quiets — history heuristic
    scores[i] = state.history[c][m.from][m.to];
  }
}

// Partial selection sort: find the best-scored move in [start, count) and
// swap it to position `start`.
inline void pickBest(MoveList& moves, int scores[], int start) {
  int bestIdx = start;
  for (int i = start + 1; i < moves.count; ++i) {
    if (scores[i] > scores[bestIdx]) bestIdx = i;
  }
  if (bestIdx != start) {
    Move tmpM = moves.moves[start];
    moves.moves[start] = moves.moves[bestIdx];
    moves.moves[bestIdx] = tmpM;
    int tmpS = scores[start];
    scores[start] = scores[bestIdx];
    scores[bestIdx] = tmpS;
  }
}

// Update killer moves: slot the new killer into position 0, shifting the
// old one to position 1.  Avoids duplicates.
inline void updateKillers(Move m, int ply, SearchState& state) {
  if (!(m == state.killers[ply][0])) {
    state.killers[ply][1] = state.killers[ply][0];
    state.killers[ply][0] = m;
  }
}

// History heuristic depth bonus — deeper cutoffs get more weight.
static constexpr int HISTORY_MAX = 7000;

inline void updateHistory(Move m, int depth, Color side, SearchState& state) {
  int bonus = depth * depth;
  int16_t& h = state.history[raw(side)][m.from][m.to];
  h += bonus;
  if (h > HISTORY_MAX) h = HISTORY_MAX;
}

// ---------------------------------------------------------------------------
// Static evaluation from the side-to-move perspective (negamax convention).
// ---------------------------------------------------------------------------

int evaluate(const Position& pos) {
  int score = eval::evaluatePosition(pos.bitboards());
  return (pos.sideToMove() == Color::WHITE) ? score : -score;
}

// ---------------------------------------------------------------------------
// Quiescence search — resolve captures so the static eval is reliable.
//
// At the horizon (depth 0), instead of returning the static eval directly,
// we search all capture moves to avoid the horizon effect.  The standing-pat
// score provides a lower bound: the side to move can always choose not to
// capture (fail-soft).
//
// Reference: https://www.chessprogramming.org/Quiescence_Search
// ---------------------------------------------------------------------------

int quiescence(Position& pos, int alpha, int beta, SearchState& state) {
  state.nodes++;

  // Periodic time / cancellation check
  if ((state.nodes & (CHECK_INTERVAL - 1)) == 0) state.checkTime();
  if (state.stopped) return 0;

  // Standing pat — assume we can do at least as well as the static eval.
  int standPat = evaluate(pos);
  if (standPat >= beta) return beta;
  if (standPat > alpha) alpha = standPat;

  // Generate capture moves only
  MoveList captures;
  movegen::generateCaptures(pos.bitboards(), pos.mailbox(),
                            pos.sideToMove(), pos.positionState(), captures);

  for (int i = 0; i < captures.count; ++i) {
    Move m = captures.moves[i];

    UndoInfo undo = pos.make(m);
    int score = -quiescence(pos, -beta, -alpha, state);
    pos.unmake(m, undo);

    if (state.stopped) return 0;

    if (score >= beta) return beta;
    if (score > alpha) alpha = score;
  }

  return alpha;
}

// ---------------------------------------------------------------------------
// Negamax with alpha-beta pruning.
//
// At depth 0, delegates to quiescence search.  Detects draws (repetition,
// 50-move rule) and terminal nodes (checkmate, stalemate) within the tree.
//
// Reference: https://www.chessprogramming.org/Alpha-Beta
// ---------------------------------------------------------------------------

int negamax(Position& pos, int depth, int alpha, int beta,
            int ply, SearchState& state) {
  state.nodes++;

  // Periodic time / cancellation check
  if ((state.nodes & (CHECK_INTERVAL - 1)) == 0) state.checkTime();
  if (state.stopped) return 0;

  // --- Draw detection ---
  if (ply > 0 && (pos.isRepetition() || pos.isFiftyMoves()))
    return DRAW_SCORE;

  // --- Horizon: quiescence search ---
  if (depth <= 0) return quiescence(pos, alpha, beta, state);

  // --- TT probe ---
  const int origAlpha = alpha;
  Move ttMove;
  ttMove.from = 0;
  ttMove.to = 0;
  ttMove.flags = 0;

  if (state.tt) {
    const TTEntry* entry = state.tt->probe(pos.hash());
    if (entry && entry->depth >= depth) {
      int ttScore = scoreFromTT(entry->score, ply);
      if (entry->flag == TTFlag::EXACT)
        return ttScore;
      if (entry->flag == TTFlag::LOWER_BOUND && ttScore > alpha)
        alpha = ttScore;
      else if (entry->flag == TTFlag::UPPER_BOUND && ttScore < beta)
        beta = ttScore;
      if (alpha >= beta)
        return ttScore;
    }
    // Extract TT best move for ordering (used in Phase 4)
    if (entry)
      ttMove = unpackMove(entry->bestMove);
  }

  // --- Generate all legal moves ---
  MoveList moves;
  movegen::generateAllMoves(pos.bitboards(), pos.mailbox(),
                            pos.sideToMove(), pos.positionState(), moves);

  // No legal moves: checkmate or stalemate
  if (moves.count == 0) {
    if (pos.inCheck())
      return -MATE_SCORE + ply;  // Checkmate — worse the further from root
    return DRAW_SCORE;            // Stalemate
  }

  // --- Move ordering: score and pick-best during iteration ---
  int scores[MAX_MOVES];
  assignScores(moves, scores, pos.mailbox(), ttMove,
               ply, pos.sideToMove(), state);

  Move bestMove = moves.moves[0];
  for (int i = 0; i < moves.count; ++i) {
    pickBest(moves, scores, i);
    Move m = moves.moves[i];

    UndoInfo undo = pos.make(m);
    int score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, state);
    pos.unmake(m, undo);

    if (state.stopped) return 0;

    if (score > alpha) {
      alpha = score;
      bestMove = m;
      if (alpha >= beta) {
        // Beta cutoff — update move ordering heuristics for quiet moves
        if (!m.isCapture()) {
          updateKillers(m, ply, state);
          updateHistory(m, depth, pos.sideToMove(), state);
        }
        break;
      }
    }
  }

  // --- TT store ---
  if (state.tt) {
    TTFlag flag;
    if (alpha <= origAlpha)
      flag = TTFlag::UPPER_BOUND;
    else if (alpha >= beta)
      flag = TTFlag::LOWER_BOUND;
    else
      flag = TTFlag::EXACT;
    state.tt->store(pos.hash(), scoreToTT(alpha, ply), bestMove, depth, flag);
  }

  return alpha;
}

// ---------------------------------------------------------------------------
// Search a single root move and return its score.
// ---------------------------------------------------------------------------

int searchRootMove(Position& pos, Move m, int depth,
                   int alpha, int beta, SearchState& state) {
  UndoInfo undo = pos.make(m);
  int score = -negamax(pos, depth - 1, -beta, -alpha, 1, state);
  pos.unmake(m, undo);
  return score;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public entry point — iterative deepening search.
//
// Searches depth 1 → maxDepth, keeping the best completed result.  If the
// time limit or external stop fires mid-iteration, the partially-complete
// iteration is discarded and the last fully-completed result is returned.
//
// An optional InfoCallback is invoked after each completed iteration,
// enabling UCI "info" output during search.
//
// Reference: https://www.chessprogramming.org/Iterative_Deepening
// ---------------------------------------------------------------------------

SearchResult findBestMove(Position& pos, const SearchLimits& limits,
                          TimeFunc timeFunc, InfoCallback info,
                          TranspositionTable* tt) {
  SearchState state;
  state.timeFunc     = timeFunc;
  state.startTime    = timeFunc ? timeFunc() : 0;
  state.maxTimeMs    = limits.maxTimeMs;
  state.externalStop = limits.stop;
  state.tt           = tt;
  state.clearHeuristics();

  int maxDepth = limits.maxDepth;
  if (maxDepth <= 0) maxDepth = 1;

  // Generate root moves once (legal moves don't change between iterations)
  MoveList rootMoves;
  movegen::generateAllMoves(pos.bitboards(), pos.mailbox(),
                            pos.sideToMove(), pos.positionState(), rootMoves);

  SearchResult result;
  if (rootMoves.count == 0) return result;  // No legal moves

  // If only one legal move, return it immediately
  if (rootMoves.count == 1) {
    result.bestMove = rootMoves.moves[0];
    result.depth = 1;
    result.nodes = 1;
    return result;
  }

  // --- Iterative deepening loop ---
  for (int depth = 1; depth <= maxDepth; ++depth) {
    int alpha = -INF_SCORE;
    int beta  =  INF_SCORE;
    Move iterBestMove = rootMoves.moves[0];
    int iterBestScore = -INF_SCORE;

    for (int i = 0; i < rootMoves.count; ++i) {
      int score = searchRootMove(pos, rootMoves.moves[i], depth,
                                 alpha, beta, state);
      if (state.stopped) break;

      if (score > iterBestScore) {
        iterBestScore = score;
        iterBestMove = rootMoves.moves[i];
        if (score > alpha) alpha = score;
      }
    }

    // If stopped mid-iteration, discard partial results
    if (state.stopped) break;

    // Commit completed iteration
    result.bestMove = iterBestMove;
    result.score    = iterBestScore;
    result.depth    = depth;
    result.nodes    = state.nodes;

    // Notify caller (e.g. UCI "info" line)
    if (info) info(result);

    // Early exit: found a forced mate — no need to search deeper
    if (iterBestScore >= MATE_SCORE - MAX_PLY) break;
  }

  result.nodes = state.nodes;
  return result;
}

}  // namespace search
}  // namespace LibreChess
