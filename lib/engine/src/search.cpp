#include "search.h"

#include <cstring>

#include "attacks.h"
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
  std::memset(countermoves, 0, sizeof(countermoves));
}

namespace {

// ---------------------------------------------------------------------------
// Node check interval — every 1024 nodes, poll time and external stop.
// ---------------------------------------------------------------------------

constexpr uint32_t CHECK_INTERVAL = 1024;

// ---------------------------------------------------------------------------
// Null Move Pruning constants.
//
// NMP_DEPTH_THRESHOLD: minimum remaining depth to attempt null move.
// NMP_REDUCTION: depth reduction for the null-move search (R).
// The null-move search uses a zero-window at (beta-1, beta) to test
// whether "doing nothing" still beats beta.
//
// Reference: https://www.chessprogramming.org/Null_Move_Pruning
// ---------------------------------------------------------------------------

static constexpr int NMP_DEPTH_THRESHOLD = 3;
static constexpr int NMP_REDUCTION       = 3;

// ---------------------------------------------------------------------------
// Late Move Reduction constants.
//
// LMR_FULL_DEPTH_MOVES: number of moves searched at full depth before
// applying reductions.  The first few moves (TT, captures, killers) are
// searched at full depth; later quiet moves get reduced.
// LMR_DEPTH_THRESHOLD: minimum remaining depth to apply LMR.
//
// Reference: https://www.chessprogramming.org/Late_Move_Reductions
// ---------------------------------------------------------------------------

static constexpr int LMR_FULL_DEPTH_MOVES = 4;
static constexpr int LMR_DEPTH_THRESHOLD  = 3;

// ---------------------------------------------------------------------------
// Aspiration window constants.
//
// After the first iteration (depth 1, always full window), subsequent
// iterations use a narrow window centred on the previous score.  When the
// search falls outside the window (fail-low or fail-high), we progressively
// widen until a valid score is obtained.
//
// ASPIRATION_DELTA: initial half-width of the window (centipawns).
//
// Reference: https://www.chessprogramming.org/Aspiration_Windows
// ---------------------------------------------------------------------------

static constexpr int ASPIRATION_DELTA = 50;

// ---------------------------------------------------------------------------
// Delta Pruning constant (quiescence search).
//
// In quiescence, if the standing-pat score plus the captured piece's value
// plus a safety margin still cannot reach alpha, the capture is hopeless
// and can be skipped.  This avoids expanding clearly losing capture lines.
//
// DELTA_MARGIN: safety margin in centipawns (accounts for positional
// compensation that the static eval might miss).
//
// Reference: https://www.chessprogramming.org/Delta_Pruning
// ---------------------------------------------------------------------------

static constexpr int DELTA_MARGIN = 200;

// ---------------------------------------------------------------------------
// Futility Pruning margins (negamax, shallow depths).
//
// At depth 1-2, if the static eval plus a depth-dependent margin is still
// below alpha, quiet moves (non-capture, non-promotion) are unlikely to
// raise alpha and can be skipped.  Captures/promotions and the TT move are
// never pruned.
//
// Guards: not in check, not a PV node, depth <= 2.
//
// Reference: https://www.chessprogramming.org/Futility_Pruning
// ---------------------------------------------------------------------------

static constexpr int FUTILITY_MARGIN[] = {0, 200, 500};  // indexed by depth

// ---------------------------------------------------------------------------
// Late Move Pruning (LMP) thresholds.
//
// At shallow depths (depth <= 3) in non-PV, non-check nodes, once enough
// moves have been searched without improving alpha, remaining quiet moves
// are skipped entirely.  More aggressive than LMR (which only reduces
// depth; LMP skips the move outright).
//
// LMP_THRESHOLD[depth] = max quiet moves to search before pruning the rest.
// Captures, promotions, and the first move are never pruned.
//
// Reference: https://www.chessprogramming.org/Late_Move_Pruning
// ---------------------------------------------------------------------------

static constexpr int LMP_THRESHOLD[] = {0, 5, 12, 20};  // indexed by depth

// ---------------------------------------------------------------------------
// Razoring margins.
//
// At shallow depths (depth 1-2) in non-PV, non-check nodes, if the static
// eval plus a depth-dependent margin falls below alpha, the position is
// likely unsalvageable by quiet moves.  Drop directly into quiescence
// search instead of expanding the full move tree.
//
// Complements futility pruning: futility skips individual quiet moves when
// static eval is low; razoring skips the entire subtree.
//
// Reference: https://www.chessprogramming.org/Razoring
// ---------------------------------------------------------------------------

static constexpr int RAZOR_MARGIN[] = {0, 300, 500};  // indexed by depth

// ---------------------------------------------------------------------------
// Tempo bonus (centipawns).
//
// A small bonus given to the side-to-move, reflecting the inherent
// advantage of having the initiative.  Applied in the evaluate() wrapper
// (not in evaluatePosition) because it depends on side-to-move context
// which the pure-bitboard evaluator does not track.
//
// Reference: https://www.chessprogramming.org/Tempo
// ---------------------------------------------------------------------------

static constexpr int TEMPO_BONUS = 10;

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
//   TT move:        30000
//   Good captures:  10000 + MVV-LVA (victim*16 - attacker)   [SEE >= 0]
//   Killer 1:        9000
//   Killer 2:        8000
//   Quiets:         history[color][from][to]  (0 .. ~7000)
//   Bad captures:   SEE value (negative)                      [SEE < 0]
//
// Losing captures (negative SEE) are demoted below quiet moves so the
// search tries better alternatives first, improving cutoff rates.
//
// References:
//   https://www.chessprogramming.org/Move_Ordering
//   https://www.chessprogramming.org/MVV-LVA
//   https://www.chessprogramming.org/Killer_Move
//   https://www.chessprogramming.org/History_Heuristic
//   https://www.chessprogramming.org/Static_Exchange_Evaluation
// ---------------------------------------------------------------------------

static constexpr int SCORE_TT_MOVE  = 30000;
static constexpr int SCORE_CAPTURE  = 10000;
static constexpr int SCORE_KILLER_1 = 9000;
static constexpr int SCORE_KILLER_2 = 8000;

// Countermove: quiet move that previously caused a beta cutoff in response
// to the opponent's last (piece, toSquare).  Ranked between killers and
// history for move ordering.
// Reference: https://www.chessprogramming.org/Countermove_Heuristic
static constexpr int SCORE_COUNTERMOVE = 7500;

// Simple piece value for MVV-LVA (indexed by PieceType).
// PieceType: NONE=0, PAWN=1, KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5, KING=6.
static constexpr int MVV_LVA_VALUE[] = {0, 1, 3, 3, 5, 9, 0};

// Assign ordering scores to all moves in the list.
// Uses a parallel `scores[]` array (caller must provide MAX_MOVES capacity).
// `prevPiece` and `prevTo` describe the opponent's last move (for countermove
// lookup).  Pass prevPiece=Piece::NONE at the root (no previous move).
void assignScores(const MoveList& moves, int scores[],
                  const BitboardSet& bb, const Piece mailbox[], Move ttMove,
                  int ply, Color side, const SearchState& state,
                  Piece prevPiece, int prevTo) {
  uint8_t c = raw(side);

  // Look up the countermove for the opponent's last (piece, toSquare).
  // A countermove of 0 (default) means no countermove recorded.
  PackedMove counterPM = 0;
  if (!isEmpty(prevPiece)) {
    int idx = pieceZobristIndex(prevPiece);
    if (idx >= 0) counterPM = state.countermoves[idx][prevTo];
  }
  Move counterMove = unpackMove(counterPM);

  for (int i = 0; i < moves.count; ++i) {
    const Move& m = moves.moves[i];

    // TT move — highest priority
    if (m.from == ttMove.from && m.to == ttMove.to &&
        m.flags == ttMove.flags) {
      scores[i] = SCORE_TT_MOVE;
      continue;
    }

    // Captures — MVV-LVA with SEE demotion for losing captures.
    // Good captures (SEE >= 0) are scored in the SCORE_CAPTURE band.
    // Bad captures (SEE < 0) are scored negative, below all quiet moves.
    if (m.isCapture()) {
      int seeScore = attacks::see(bb, mailbox, m);
      if (seeScore >= 0) {
        PieceType victim   = pieceType(mailbox[m.to]);
        PieceType attacker = pieceType(mailbox[m.from]);
        if (m.isEP()) victim = PieceType::PAWN;
        scores[i] = SCORE_CAPTURE +
                    MVV_LVA_VALUE[raw(victim)] * 16 -
                    MVV_LVA_VALUE[raw(attacker)];
      } else {
        // Losing capture — sort below quiets (negative score).
        scores[i] = seeScore;
      }
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

    // Countermove — the quiet move that previously refuted the opponent's
    // last move.  Ranked between killers and history.
    if (counterPM != 0 &&
        m.from == counterMove.from && m.to == counterMove.to &&
        m.flags == counterMove.flags) {
      scores[i] = SCORE_COUNTERMOVE;
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
  int stm = (pos.sideToMove() == Color::WHITE) ? score : -score;
  return stm + TEMPO_BONUS;
}

// ---------------------------------------------------------------------------
// Non-pawn material check — used by NMP to avoid null-move in endgame
// positions prone to zugzwang (e.g. K+P vs K).  Returns true if the side
// to move has at least one knight, bishop, rook, or queen.
// ---------------------------------------------------------------------------

bool hasNonPawnMaterial(const Position& pos) {
  uint8_t c = raw(pos.sideToMove());
  const BitboardSet& bb = pos.bitboards();
  // Zobrist indices: white N=1 B=2 R=3 Q=4, black offset +6
  int base = c * 6;
  return (bb.byPiece[base + 1] | bb.byPiece[base + 2] |
          bb.byPiece[base + 3] | bb.byPiece[base + 4]) != 0;
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

    // --- Delta Pruning ---
    // If the captured piece's value plus a safety margin cannot raise the
    // score to alpha, this capture is hopeless — skip it.
    // EP captures always take a pawn (100 cp).
    {
      int capturedValue = m.isEP()
          ? 100
          : MVV_LVA_VALUE[raw(pieceType(pos.mailbox()[m.to]))] * 100;
      if (standPat + capturedValue + DELTA_MARGIN < alpha) continue;
    }

    // --- SEE Pruning ---
    // Skip captures where the static exchange evaluation is negative
    // (the capture sequence loses material after recaptures).
    if (attacks::see(pos.bitboards(), pos.mailbox(), m) < 0) continue;

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
// References:
//   https://www.chessprogramming.org/Alpha-Beta
//   https://www.chessprogramming.org/Check_Extensions
//   https://www.chessprogramming.org/Null_Move_Pruning
//   https://www.chessprogramming.org/Principal_Variation_Search
//   https://www.chessprogramming.org/Late_Move_Reductions
// ---------------------------------------------------------------------------

int negamax(Position& pos, int depth, int alpha, int beta,
            int ply, SearchState& state, Piece prevPiece, int prevTo) {
  state.nodes++;

  // Periodic time / cancellation check
  if ((state.nodes & (CHECK_INTERVAL - 1)) == 0) state.checkTime();
  if (state.stopped) return 0;

  // --- Draw detection ---
  if (ply > 0 && (pos.isRepetition() || pos.isFiftyMoves()))
    return DRAW_SCORE;

  // --- Check extension ---
  // When the side to move is in check, extend search by one ply to avoid
  // misevaluating forced sequences that end at the horizon.  The check
  // status is also used by NMP (as a guard) and LMR (to skip reductions).
  bool inCheck = pos.inCheck();
  if (inCheck) ++depth;

  // --- Horizon: quiescence search ---
  if (depth <= 0) return quiescence(pos, alpha, beta, state);

  // PV node: the initial window is wider than a zero-window scout.
  // Non-PV nodes use a null window (beta == alpha + 1).
  bool pvNode = (beta - alpha) > 1;

  // --- Static evaluation (shared by razoring + futility pruning) ---
  // Computed once and reused.  Only needed outside PV / non-check nodes,
  // but the cost is negligible and simplifies the control flow.
  int staticEval = evaluate(pos);

  // --- Razoring ---
  // At shallow depths, if the static eval is far below alpha, the position
  // is unlikely to be rescued by quiet moves.  Fall back to quiescence
  // search to resolve captures and return immediately.
  //
  // Guards: not PV, not in check, shallow depth (1-2).
  //
  // Reference: https://www.chessprogramming.org/Razoring
  if (!pvNode && !inCheck && depth <= 2 && depth >= 1 &&
      staticEval + RAZOR_MARGIN[depth] <= alpha) {
    return quiescence(pos, alpha, beta, state);
  }

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
    if (entry)
      ttMove = unpackMove(entry->bestMove);
  }

  // --- Null Move Pruning (NMP) ---
  // If the position is so strong that even "passing" (null move) beats
  // beta, we can prune the entire subtree.
  //
  // Guards:
  //   - Not at root (ply > 0)
  //   - Sufficient depth remaining (avoids useless shallow null searches)
  //   - Not in check (null move while in check is illegal)
  //   - Not a PV node (PV nodes need accurate scores, not cut estimates)
  //   - Side has non-pawn material (avoids zugzwang in K+P endgames)
  //
  // Reference: https://www.chessprogramming.org/Null_Move_Pruning
  if (ply > 0 && depth >= NMP_DEPTH_THRESHOLD && !inCheck &&
      !pvNode && hasNonPawnMaterial(pos)) {
    UndoInfo nullUndo = pos.makeNullMove();
    // Zero-window search at reduced depth: just testing if score >= beta.
    int nullScore = -negamax(pos, depth - 1 - NMP_REDUCTION,
                             -beta, -beta + 1, ply + 1, state,
                             Piece::NONE, 0);
    pos.unmakeNullMove(nullUndo);

    if (state.stopped) return 0;

    // Null-move cutoff: the position is so good that even passing beats beta.
    if (nullScore >= beta) {
      // Don't return unproven mate scores from null-move searches —
      // they can be unreliable.  Clamp to beta instead.
      return (nullScore >= MATE_SCORE - MAX_PLY) ? beta : nullScore;
    }
  }

  // --- Generate all legal moves ---
  MoveList moves;
  movegen::generateAllMoves(pos.bitboards(), pos.mailbox(),
                            pos.sideToMove(), pos.positionState(), moves);

  // No legal moves: checkmate or stalemate
  if (moves.count == 0) {
    if (inCheck)
      return -MATE_SCORE + ply;  // Checkmate — worse the further from root
    return DRAW_SCORE;            // Stalemate
  }

  // --- Futility Pruning setup ---
  // At shallow depths in non-PV, non-check nodes, if the static eval plus
  // a depth-dependent margin still falls short of alpha, quiet moves are
  // unlikely to improve the position enough.  We flag this to skip them
  // in the move loop (captures/promotions and the first move are exempt).
  // Reuses the `staticEval` computed earlier (above razoring).
  //
  // Reference: https://www.chessprogramming.org/Futility_Pruning
  bool futilityPruning = false;
  if (!pvNode && !inCheck && depth <= 2 && depth >= 1) {
    if (staticEval + FUTILITY_MARGIN[depth] <= alpha)
      futilityPruning = true;
  }

  // --- Move ordering: score and pick-best during iteration ---
  int scores[MAX_MOVES];
  assignScores(moves, scores, pos.bitboards(), pos.mailbox(), ttMove,
               ply, pos.sideToMove(), state, prevPiece, prevTo);

  Move bestMove = moves.moves[0];
  int movesSearched = 0;

  for (int i = 0; i < moves.count; ++i) {
    pickBest(moves, scores, i);
    Move m = moves.moves[i];

    // --- Futility Pruning: skip hopeless quiet moves at shallow depth ---
    // Never prune the first move (we need at least one legal move searched)
    // and never prune captures or promotions (tactical moves can surprise).
    if (futilityPruning && movesSearched > 0 &&
        !m.isCapture() && !m.isPromotion()) {
      continue;
    }

    // --- Late Move Pruning (LMP): skip late quiet moves at shallow depth ---
    // Once enough moves have been searched without a beta cutoff, remaining
    // quiet moves at low depth are unlikely to improve — skip them entirely.
    // Guards: not PV, not in check, shallow depth, not the first move,
    //         not a capture or promotion (those are always searched).
    if (!pvNode && !inCheck && depth <= 3 && movesSearched > 0 &&
        movesSearched >= LMP_THRESHOLD[depth] &&
        !m.isCapture() && !m.isPromotion()) {
      continue;
    }

    // Capture the moving piece identity before make() alters the mailbox.
    // Used to index the countermove table in child nodes.
    Piece movingPiece = pos.mailbox()[m.from];

    UndoInfo undo = pos.make(m);

    int score;

    // --- Principal Variation Search (PVS) + Late Move Reductions (LMR) ---
    //
    // PVS: the first move that improves alpha is the presumed best (PV).
    // All subsequent moves are searched with a zero-window ("scout") to
    // confirm they are worse.  If a scout search fails high, we re-search
    // with the full window.
    //
    // LMR: quiet moves searched late in the move list are unlikely to be
    // good.  We search them at reduced depth first.  If the reduced search
    // surprises (fails high), we re-search at full depth.
    //
    // The combination: late quiet moves get both LMR and PVS zero-window.
    // If the reduced search fails high, we re-search at full depth with
    // a zero-window.  If that also fails high, we re-search with the full
    // PV window.  This three-tier approach minimizes wasted work.
    //
    // References:
    //   https://www.chessprogramming.org/Principal_Variation_Search
    //   https://www.chessprogramming.org/Late_Move_Reductions

    if (movesSearched == 0) {
      // First move: always search with full window and full depth.
      score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, state,
                       movingPiece, m.to);
    } else {
      // --- LMR: determine if this move should be reduced ---
      // Conditions for reduction:
      //   - Enough moves already searched at full depth
      //   - Sufficient depth remaining
      //   - Move is quiet (not a capture or promotion)
      //   - Side was not in check before the move
      bool doLMR = movesSearched >= LMR_FULL_DEPTH_MOVES &&
                   depth >= LMR_DEPTH_THRESHOLD &&
                   !m.isCapture() && !m.isPromotion() &&
                   !inCheck;

      if (doLMR) {
        // Reduced-depth zero-window scout search.
        // Reduction: 1 ply, or 2 for very late moves at high depth.
        int reduction = 1;
        if (movesSearched >= LMR_FULL_DEPTH_MOVES * 2 && depth >= 6)
          reduction = 2;

        score = -negamax(pos, depth - 1 - reduction,
                         -alpha - 1, -alpha, ply + 1, state,
                         movingPiece, m.to);
      } else {
        // Not reducing — force a full-depth scout below.
        score = alpha + 1;
      }

      // If the LMR search (or skip) suggests this move might be good,
      // re-search at full depth with a zero-window.
      if (score > alpha) {
        score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, state,
                         movingPiece, m.to);
      }

      // PVS re-search: if the zero-window scout failed high within the
      // PV window, we need an exact score — re-search with full window.
      if (score > alpha && score < beta) {
        score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, state,
                         movingPiece, m.to);
      }
    }

    pos.unmake(m, undo);

    if (state.stopped) return 0;

    ++movesSearched;

    if (score > alpha) {
      alpha = score;
      bestMove = m;
      if (alpha >= beta) {
        // Beta cutoff — update move ordering heuristics for quiet moves
        if (!m.isCapture()) {
          updateKillers(m, ply, state);
          updateHistory(m, depth, pos.sideToMove(), state);

          // Store as countermove for the opponent's previous (piece, toSq).
          // On future visits, this move will be tried earlier when the same
          // previous move is encountered.
          if (!isEmpty(prevPiece)) {
            int idx = pieceZobristIndex(prevPiece);
            if (idx >= 0)
              state.countermoves[idx][prevTo] = packMove(m);
          }
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
  Piece movingPiece = pos.mailbox()[m.from];
  UndoInfo undo = pos.make(m);
  int score = -negamax(pos, depth - 1, -beta, -alpha, 1, state,
                       movingPiece, m.to);
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

  // --- Iterative deepening loop with aspiration windows ---
  //
  // Depth 1 always uses a full window.  Subsequent depths use a narrow
  // aspiration window centred on the previous iteration's score.  On
  // fail-low or fail-high the window is progressively widened, falling
  // back to a full-width search if necessary.
  //
  // After each completed iteration, the best move is promoted to index 0
  // of rootMoves so it is searched first in the next iteration (root move
  // reordering).
  //
  // References:
  //   https://www.chessprogramming.org/Aspiration_Windows
  //   https://www.chessprogramming.org/Iterative_Deepening

  int prevScore = 0;  // score from the last completed iteration

  for (int depth = 1; depth <= maxDepth; ++depth) {
    // --- Aspiration window setup ---
    int alpha, beta;
    if (depth == 1) {
      alpha = -INF_SCORE;
      beta  =  INF_SCORE;
    } else {
      alpha = prevScore - ASPIRATION_DELTA;
      beta  = prevScore + ASPIRATION_DELTA;
    }

    Move iterBestMove = rootMoves.moves[0];
    int iterBestScore = -INF_SCORE;

    // Aspiration re-search loop: widen the window on fail-low / fail-high.
    //
    // IMPORTANT: the root move loop updates `alpha` as better moves are
    // found (standard alpha-beta).  We must compare the final score against
    // the *original* aspiration bounds, not the modified alpha, to decide
    // whether a re-search is needed.
    while (true) {
      int aspAlpha = alpha;  // snapshot before root search modifies alpha
      int aspBeta  = beta;
      iterBestMove  = rootMoves.moves[0];
      iterBestScore = -INF_SCORE;

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

      if (state.stopped) break;

      // Fail-low: every root move scored at or below the aspiration lower
      // bound.  Widen the lower bound to -INF and re-search.
      if (iterBestScore <= aspAlpha) {
        alpha = -INF_SCORE;
        continue;
      }
      // Fail-high: the best score met or exceeded the upper bound.
      // Widen the upper bound to +INF and re-search.
      if (iterBestScore >= aspBeta) {
        beta = INF_SCORE;
        continue;
      }
      // Score falls within the window — iteration is complete.
      break;
    }

    // If stopped mid-iteration, discard partial results
    if (state.stopped) break;

    // Commit completed iteration
    result.bestMove = iterBestMove;
    result.score    = iterBestScore;
    result.depth    = depth;
    result.nodes    = state.nodes;
    prevScore       = iterBestScore;

    // --- Root move reordering ---
    // Move the best move to index 0 so it is searched first at the next
    // depth, improving alpha-beta cutoffs.
    for (int i = 1; i < rootMoves.count; ++i) {
      if (rootMoves.moves[i].from == iterBestMove.from &&
          rootMoves.moves[i].to   == iterBestMove.to   &&
          rootMoves.moves[i].flags == iterBestMove.flags) {
        Move tmp = rootMoves.moves[0];
        rootMoves.moves[0] = rootMoves.moves[i];
        rootMoves.moves[i] = tmp;
        break;
      }
    }

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
