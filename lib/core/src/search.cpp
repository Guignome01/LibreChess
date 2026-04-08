#include "search.h"
#include "search_params.h"

#include <algorithm>
#include <cstring>

#include "attacks.h"
#include "evaluation.h"
#include "move_picker.h"
#include "movegen.h"
#include "piece.h"
#include "stats.h"
#include "utils.h"

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

// ---------------------------------------------------------------------------
// Search statistics — guarded by -DSTATS (native test builds only).
// ---------------------------------------------------------------------------

#ifdef STATS
SearchStats g_stats;
void resetStats() { g_stats = SearchStats{}; }
SearchStats getStats() { return g_stats; }
#else
void resetStats() {}
SearchStats getStats() { return SearchStats{}; }
#endif

// ===========================================================================
// TranspositionTable — resize / free / clear / probe / store are now
// defined in search.h (base from hash_table.h).
// ===========================================================================

void SearchState::clearHeuristics() {
  std::memset(killers, 0, sizeof(killers));
  std::memset(history, 0, sizeof(history));
  std::memset(captureHistory, 0, sizeof(captureHistory));
  std::memset(countermoves, 0, sizeof(countermoves));
  std::memset(staticEvals, 0, sizeof(staticEvals));
  std::memset(pvLength, 0, sizeof(pvLength));
}

namespace {

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

// ===========================================================================
// MovePicker, heuristic helpers — defined in move_picker.h.
// ===========================================================================

// ---------------------------------------------------------------------------
// Collect principal variation: store the current move at `pv[ply][0]` and
// copy the child's PV line from `pv[ply+1]` into `pv[ply][1..]`.  Bounds-
// checked to prevent writing past MAX_PV_LEN.
//
// Called from both negamax (interior nodes) and findBestMove (root node)
// whenever a new best move is found.  Consolidating PV collection in one
// place eliminates the duplicated memcpy + clamp pattern.
//
// Reference: https://www.chessprogramming.org/Triangular_PV-Table
// ---------------------------------------------------------------------------

inline void collectPV(SearchState& state, int ply, PackedMove move) {
  state.pv[ply][0] = move;
  int childLen = (ply + 1 < MAX_PLY) ? state.pvLength[ply + 1] : 0;
  if (childLen < 0) childLen = 0;
  if (childLen > MAX_PV_LEN - 1) childLen = MAX_PV_LEN - 1;
  std::memcpy(&state.pv[ply][1], &state.pv[ply + 1][0],
              childLen * sizeof(PackedMove));
  state.pvLength[ply] = childLen + 1;
}

// ---------------------------------------------------------------------------
// Compute the LMR (Late Move Reduction) for a quiet move in negamax.
//
// Base reduction comes from the logarithmic LMR table, with adjustments:
//   - Bad history (< -500) → reduce more (+1).
//   - Good history (> 1500) → reduce less (−1).
//   - Not improving over 2 plies ago → reduce more (+1).
//   - Non-PV node → reduce more (+1).
//
// The result is clamped to [1, newDepth − 2] to guarantee at least a
// minimal search.
//
// Reference: https://www.chessprogramming.org/Late_Move_Reductions
// ---------------------------------------------------------------------------

inline int computeLMRReduction(int depth, int moveIndex, int16_t hist,
                               bool improving, bool pvNode) {
  int mi = moveIndex < LMR_MAX_MOVES ? moveIndex : LMR_MAX_MOVES - 1;
  int reduction = LMR_TABLE[depth][mi];

  if (hist < -500)
    ++reduction;           // Bad history → reduce more
  else if (hist > 1500)
    --reduction;           // Good history → reduce less
  if (!improving)
    ++reduction;           // Not improving → reduce more
  if (!pvNode)
    ++reduction;           // Non-PV nodes → reduce more

  int newDepth = depth - 1;
  if (reduction < 1) reduction = 1;
  if (reduction > newDepth - 2) reduction = newDepth - 2;
  if (reduction < 1) reduction = 1;
  return reduction;
}

// ---------------------------------------------------------------------------
// Promote the iteration's best move to index 0 in the root move list so
// it is searched first at the next depth, improving alpha-beta cutoffs.
//
// Reference: https://www.chessprogramming.org/Move_Ordering
// ---------------------------------------------------------------------------

inline void reorderRootMoves(MoveList& rootMoves, const Move& bestMove) {
  for (int i = 1; i < rootMoves.count; ++i) {
    if (rootMoves.moves[i].from  == bestMove.from &&
        rootMoves.moves[i].to    == bestMove.to   &&
        rootMoves.moves[i].flags == bestMove.flags) {
      Move tmp = rootMoves.moves[0];
      rootMoves.moves[0] = rootMoves.moves[i];
      rootMoves.moves[i] = tmp;
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Material-only evaluation from the side-to-move perspective.
// Uses Position's incremental material accumulator — no popcount calls.
// Reference: https://www.chessprogramming.org/Incremental_Updates
// Reference: https://www.chessprogramming.org/Lazy_Evaluation
// ---------------------------------------------------------------------------

int lazyEval(const Position& pos) {
  return (pos.sideToMove() == Color::WHITE) ? pos.material() : -pos.material();
}

// ---------------------------------------------------------------------------
// Static evaluation from the side-to-move perspective (negamax convention).
// Uses the eval hash table (if available) to avoid redundant evaluations.
// The cached value is the final STM-relative score including tempo.
// ---------------------------------------------------------------------------

int evaluate(const Position& pos, SearchState& state) {
  uint64_t posHash = pos.hash();

  // --- Eval hash probe ---
  if (state.evalHash) {
    const eval::EvalEntry* cached = state.evalHash->probe(posHash);
    if (cached) return cached->score;
  }

  int score = eval::evaluatePosition(pos.bitboards(), pos.mgPST(), pos.egPST(),
                                     pos.phase(), state.pawnHash);
  int stm = (pos.sideToMove() == Color::WHITE) ? score : -score;
  int result = stm + TEMPO_BONUS;

  // --- Eval hash store ---
  if (state.evalHash) {
    state.evalHash->store(posHash, static_cast<int16_t>(result));
  }

  return result;
}

// ---------------------------------------------------------------------------
// Non-pawn material check — used by NMP to avoid null-move in endgame
// positions prone to zugzwang (e.g. K+P vs K).  Returns true if the side
// to move has at least one knight, bishop, rook, or queen.
// ---------------------------------------------------------------------------

bool hasNonPawnMaterial(const Position& pos) {
  Color color = pos.sideToMove();
  const BitboardSet& bb = pos.bitboards();
  return (bb.byPiece[pieceIndex(color, PieceType::KNIGHT)] |
          bb.byPiece[pieceIndex(color, PieceType::BISHOP)] |
          bb.byPiece[pieceIndex(color, PieceType::ROOK)]   |
          bb.byPiece[pieceIndex(color, PieceType::QUEEN)]) != 0;
}

// ---------------------------------------------------------------------------
// Quiescence search — resolve captures so the static eval is reliable.
//
// At the horizon (depth 0), instead of returning the static eval directly,
// we search all capture moves to avoid the horizon effect.  The standing-pat
// score provides a lower bound: the side to move can always choose not to
// capture (fail-soft).
//
// When the side to move is **in check**, standing pat is not valid (the
// position might be checkmate), so all evasion moves are generated and
// searched.  Delta and SEE pruning are also skipped under check — we
// must find any legal escape or confirm checkmate.
//
// Reference: https://www.chessprogramming.org/Quiescence_Search
// ---------------------------------------------------------------------------

int quiescence(Position& pos, int alpha, int beta, int ply, int qsPly,
               SearchState& state) {
  state.nodes++;
  STAT_INC(qNodes);

  // --- Ply / QS depth overflow guard ---
  // Check evasions generate all moves (including quiets), which can cause
  // deep recursion in positions with repeated checks.  Bound QS depth
  // independently to prevent stack overflow on the ESP32 FreeRTOS task.
  // Reference: https://www.chessprogramming.org/Quiescence_Search
  if (ply >= MAX_PLY - 1 || qsPly >= MAX_QS_DEPTH)
    return evaluate(pos, state);

  // Periodic time / cancellation check
  if ((state.nodes & (CHECK_INTERVAL - 1)) == 0) state.checkTime();
  if (state.stopped) return 0;

  bool inCheck = pos.inCheck();

  // Standing pat — assume we can do at least as well as the static eval.
  // Not valid when in check: the side to move *must* escape, so standing
  // pat cannot be used as a lower bound.
  // Fail-soft: track bestScore separately from alpha so TT gets accurate
  // scores.  Reference: https://www.chessprogramming.org/Fail-Soft
  int standPat = 0;
  if (!inCheck) {
    standPat = evaluate(pos, state);
    if (standPat >= beta) return standPat;
    if (standPat > alpha) alpha = standPat;
  }

  // bestScore starts at -MATE_SCORE when in check (assume mated until we
  // find a legal move), or at standPat when not in check.
  int bestScore = inCheck ? (-MATE_SCORE) : standPat;

  // In check: generate all evasion moves (not just captures).
  // Not in check: generate capture moves only.
  // Uses QSMoveList (cap 128) — sufficient for real-game positions, saves
  // ~540 bytes/ply vs full MoveList (cap 218).
  QSMoveList moves;
  if (inCheck)
    movegen::generateAllMoves(pos.bitboards(), pos.mailbox(),
                              pos.sideToMove(), pos.positionState(), moves);
  else
    movegen::generateCaptures(pos.bitboards(), pos.mailbox(),
                              pos.sideToMove(), pos.positionState(), moves);

  // --- MVV-LVA ordering for captures ---
  // Score captures by Most Valuable Victim - Least Valuable Aggressor so
  // the best trades are tried first, improving beta-cutoff rates.
  // Non-captures (evasions under check) get a neutral score.
  // Reference: https://www.chessprogramming.org/MVV-LVA
  int16_t capScores[QS_MAX_MOVES];
  for (int j = 0; j < moves.count; ++j) {
    const Move& cm = moves.moves[j];
    if (cm.isCapture()) {
      PieceType victim   = cm.isEP() ? PieceType::PAWN
                                     : pieceType(pos.mailbox()[cm.to]);
      PieceType attacker = pieceType(pos.mailbox()[cm.from]);
      capScores[j] = static_cast<int16_t>(scoreMVVLVA(victim, attacker));
    } else {
      capScores[j] = 0;
    }
  }

  for (int i = 0; i < moves.count; ++i) {
    pickBest(moves, capScores, i);
    Move m = moves.moves[i];

    // Delta and SEE pruning only apply to captures when NOT in check.
    if (!inCheck && m.isCapture()) {
      // --- Delta Pruning ---
      // If the captured piece's value plus a safety margin cannot raise the
      // score to alpha, this capture is hopeless — skip it.
      // Uses eval::materialValue() for consistent piece values.
      {
        PieceType capType = m.isEP()
            ? PieceType::PAWN
            : pieceType(pos.mailbox()[m.to]);
        int capturedValue = eval::materialValue(capType);
        if (standPat + capturedValue + DELTA_MARGIN < alpha) continue;
      }

      // --- Pawn-defended-pawn pruning ---
      // When a non-pawn captures a pawn that is defended by an enemy pawn,
      // the exchange is almost always losing (trade a piece for a pawn).
      // Skip it without the cost of a full SEE computation.
      //
      // To find defender pawns that attack m.to: the squares from which an
      // enemy pawn can reach m.to are PAWN[sideToMove][m.to] (reversed
      // direction of enemy pawn attacks).
      //
      // Reference: https://www.chessprogramming.org/Quiescence_Search
      if (!m.isEP()) {
        PieceType attacker = pieceType(pos.mailbox()[m.from]);
        PieceType victim   = pieceType(pos.mailbox()[m.to]);
        if (attacker != PieceType::PAWN && victim == PieceType::PAWN) {
          Color defender = ~pos.sideToMove();
          Bitboard defenderPawns = pos.bitboards().byPiece[raw(
              makePiece(defender, PieceType::PAWN))];
          if (attacks::PAWN[raw(pos.sideToMove())][m.to] & defenderPawns)
            continue;
        }
      }

      // --- SEE Pruning ---
      // Skip captures where the static exchange evaluation is negative
      // (the capture sequence loses material after recaptures).
      if (attacks::see(pos.bitboards(), pos.mailbox(), m) < 0) continue;
    }

    UndoInfo undo = pos.make(m);
    int score = -quiescence(pos, -beta, -alpha, ply + 1, qsPly + 1, state);
    pos.unmake(m, undo);

    if (state.stopped) return 0;

    if (score > bestScore) bestScore = score;
    if (score >= beta) return score;
    if (score > alpha) alpha = score;
  }

  return bestScore;
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
            int ply, SearchState& state, Piece prevPiece, int prevTo,
            Move excludedMove = Move()) {
  state.nodes++;
  STAT_INC(mainNodes);

  // --- Ply overflow guard ---
  // Check extensions can push ply beyond MAX_PLY.  All per-ply arrays
  // (pvLength, staticEvals, killers, pv) are sized MAX_PLY, so we must
  // bail out before writing out of bounds.
  // Reference: https://www.chessprogramming.org/Maximum_Search_Depth
  if (ply >= MAX_PLY - 1) return evaluate(pos, state);

  state.pvLength[ply] = 0;  // no PV line yet at this ply

  // Periodic time / cancellation check
  if ((state.nodes & (CHECK_INTERVAL - 1)) == 0) state.checkTime();
  if (state.stopped) return 0;

  // --- Draw detection ---
  if (ply > 0 && (pos.isRepetition() || pos.isFiftyMoves()))
    return DRAW_SCORE;

  // --- Mate Distance Pruning ---
  // Tighten the window to the best possible mate from this ply.
  // If a shorter mate is already guaranteed in another branch, prune.
  // Reference: https://www.chessprogramming.org/Mate_Distance_Pruning
  if (ply > 0) {
    int matingScore = MATE_SCORE - ply;
    if (matingScore < beta) {
      beta = matingScore;
      if (alpha >= beta) return beta;
    }
    int matedScore = -MATE_SCORE + ply;
    if (matedScore > alpha) {
      alpha = matedScore;
      if (alpha >= beta) return alpha;
    }
  }

  // --- Check extension ---
  // When the side to move is in check, extend search by one ply to avoid
  // misevaluating forced sequences that end at the horizon.  The check
  // status is also used by NMP (as a guard) and LMR (to skip reductions).
  bool inCheck = pos.inCheck();
  if (inCheck) {
    ++depth;
    STAT_INC(checkExtensions);
  }

  // --- Horizon: quiescence search ---
  if (depth <= 0) return quiescence(pos, alpha, beta, ply, 0, state);

  // PV node: the initial window is wider than a zero-window scout.
  // Non-PV nodes use a null window (beta == alpha + 1).
  bool pvNode = (beta - alpha) > 1;

  // --- Lazy Evaluation ---
  // In non-PV, non-check nodes, compute a cheap material-only score first.
  // If the material score is far outside the alpha-beta window, the
  // expensive positional evaluation won't change the outcome — use the
  // material score directly.  Otherwise, compute the full evaluation.
  //
  // This skips mobility, king safety, pawn structure, and other costly
  // terms in positions where material imbalance already dominates.
  //
  // Reference: https://www.chessprogramming.org/Lazy_Evaluation
  int staticEval;
  if (!pvNode && !inCheck) {
    int materialScore = lazyEval(pos);
    if (materialScore - LAZY_EVAL_MARGIN >= beta ||
        materialScore + LAZY_EVAL_MARGIN <= alpha) {
      staticEval = materialScore;
    } else {
      staticEval = evaluate(pos, state);
    }
  } else {
    staticEval = evaluate(pos, state);
  }

  // Store static eval for the improving heuristic.
  // In check, store a sentinel so children know this ply had no eval.
  state.staticEvals[ply] = inCheck ? static_cast<int16_t>(-INF_SCORE)
                                   : static_cast<int16_t>(staticEval);

  // --- Improving heuristic ---
  // A position is "improving" if its static eval is higher than the eval
  // 2 plies ago (our previous move).  Falling back to 4 plies if 2 plies
  // ago was in check (no eval available).
  //
  // Used to adjust aggressiveness of RFP, LMP, and LMR.
  //
  // Reference: https://www.chessprogramming.org/Improving
  bool improving = false;
  if (!inCheck) {
    if (ply >= 2 && state.staticEvals[ply - 2] > -INF_SCORE)
      improving = staticEval > state.staticEvals[ply - 2];
    else if (ply >= 4 && state.staticEvals[ply - 4] > -INF_SCORE)
      improving = staticEval > state.staticEvals[ply - 4];
  }

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
    STAT_INC(razoringPrunes);
    return quiescence(pos, alpha, beta, ply, 0, state);
  }

  // --- Reverse Futility Pruning (Static Null Move Pruning) ---
  // If the static eval minus a safety margin still exceeds beta, the
  // position is overwhelmingly good — no move search needed.
  //
  // Guards: not PV, not in check, shallow depth (avoids bad pruning deep
  // in the tree where eval reliability drops).
  //
  // Reference: https://www.chessprogramming.org/Reverse_Futility_Pruning
  if (!pvNode && !inCheck && depth <= 6 &&
      staticEval - RFP_MARGIN * depth / (1 + improving) >= beta) {
    STAT_INC(rfpPrunes);
    return staticEval;
  }

  // --- TT probe ---
  const int origAlpha = alpha;
  Move ttMove;
  ttMove.from = 0;
  ttMove.to = 0;
  ttMove.flags = 0;
  bool hasExcluded = (excludedMove.from != 0 || excludedMove.to != 0);

  // TT entry pointer retained for singular extension check below.
  const TTEntry* ttEntry = nullptr;

  if (state.tt) {
    STAT_INC(ttProbes);
    const TTEntry* entry = state.tt->probe(pos.hash());
    ttEntry = entry;  // save for SE probe
    if (entry) STAT_INC(ttHits);
    // Skip TT cutoffs when inside an exclusion search — we need to
    // search all non-excluded moves regardless of TT score.
    if (entry && entry->depth >= depth && !hasExcluded) {
      int ttScore = scoreFromTT(entry->score, ply);
      if (entry->flag == TTFlag::EXACT) {
        STAT_INC(ttExactCutoffs);
        return ttScore;
      }
      if (entry->flag == TTFlag::LOWER_BOUND && ttScore > alpha)
        alpha = ttScore;
      else if (entry->flag == TTFlag::UPPER_BOUND && ttScore < beta)
        beta = ttScore;
      if (alpha >= beta) {
        STAT_INC(ttLowerCutoffs);
        return ttScore;
      }
    }
    if (entry)
      ttMove = unpackMove(entry->bestMove);
  }

  // --- Internal Iterative Reductions (IIR) ---
  // When no TT move is available at sufficient depth, the move ordering is
  // blind — the first move searched is arbitrary.  Rather than running a
  // full shallow search (IID), we simply reduce depth by one ply.  This is
  // cheaper and achieves the same effect: the reduced search will populate
  // the TT, so the next iteration's full-depth search will have a hash move.
  //
  // Guards: TT available, no hash move found, sufficient depth.
  //
  // Reference: https://www.chessprogramming.org/Internal_Iterative_Reductions
  if (state.tt && depth >= IID_DEPTH_THRESHOLD &&
      ttMove.from == 0 && ttMove.to == 0) {
    --depth;
  }

  // --- Singular Extensions ---
  // At non-root, non-check nodes with a reliable TT entry, test whether
  // the TT move is "singular" — significantly better than all alternatives.
  // Performs a reduced-depth exclusion search (all moves except the TT
  // move) with a narrow window.  If nothing reaches singularBeta, the TT
  // move is singular and gets a one-ply extension.
  //
  // Guards:
  //   - Not at root (ply > 0)
  //   - Not in check (check extension already applied)
  //   - Sufficient depth (avoids overhead at shallow nodes)
  //   - TT available with a reliable entry (depth >= current - 3)
  //   - TT score is EXACT or LOWER_BOUND (not an upper bound)
  //   - Not already inside an exclusion search (prevents recursion)
  //
  // Reference: https://www.chessprogramming.org/Singular_Extensions
  int singularExtension = 0;
  if (ply > 0 && !inCheck && depth >= SE_DEPTH_THRESHOLD && state.tt &&
      !hasExcluded && ttEntry &&
      ttEntry->depth >= depth - 3 &&
      (ttEntry->flag == TTFlag::EXACT ||
       ttEntry->flag == TTFlag::LOWER_BOUND)) {
    int ttScore = scoreFromTT(ttEntry->score, ply);
    int singularBeta = ttScore - SE_MARGIN_SCALE * depth;
    int halfDepth = depth / 2;
    int seScore = negamax(pos, halfDepth, singularBeta - 1, singularBeta,
                          ply, state, prevPiece, prevTo, ttMove);
    if (seScore < singularBeta) {
      singularExtension = 1;  // TT move is singular — extend it
      STAT_INC(singularExtensions);
    }
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
    // Adaptive reduction: deeper positions get more aggressive pruning,
    // and a large eval surplus over beta adds further reduction.
    // Reference: https://www.chessprogramming.org/Null_Move_Pruning#Adaptive
    int evalSurplus = (staticEval > beta) ? (staticEval - beta) : 0;
    int evalBonus = evalSurplus / 200;
    if (evalBonus > 3) evalBonus = 3;
    int R = NMP_REDUCTION + depth / 4 + evalBonus;
    if (R > depth - 1) R = depth - 1;

    UndoInfo nullUndo = pos.makeNullMove();
    // Zero-window search at reduced depth: just testing if score >= beta.
    int nullScore = -negamax(pos, depth - 1 - R,
                             -beta, -beta + 1, ply + 1, state,
                             Piece::NONE, 0);
    pos.unmakeNullMove(nullUndo);

    if (state.stopped) return 0;

    // Null-move cutoff: the position is so good that even passing beats beta.
    if (nullScore >= beta) {
      STAT_INC(nullMovePrunes);
      // Don't return unproven mate scores from null-move searches —
      // they can be unreliable.  Clamp to beta instead.
      return (nullScore >= MATE_SCORE - MAX_PLY) ? beta : nullScore;
    }
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

  // --- Staged move generation via MovePicker ---
  // Moves are generated lazily in priority order: TT move → good captures
  // → killers → countermove → quiets → bad captures.  A beta cutoff in an
  // early stage skips the cost of generating later stages.
  //
  // Reference: https://www.chessprogramming.org/Move_Ordering#Staged_Move_Generation
  MovePicker picker;
  picker.init(pos.bitboards(), pos.mailbox(), pos.sideToMove(),
              pos.positionState(), state, ply, ttMove, prevPiece, prevTo);

  Move bestMove;
  int bestScore = -INF_SCORE;
  int movesSearched = 0;

  // Track quiet moves searched for history gravity (penalize on cutoff).
  // Stored as PackedMove to reduce per-ply stack footprint; capped at 32
  // entries (cutoffs beyond 32 moves are rare and penalty accuracy loss is
  // negligible).
  PackedMove quietsSearched[32];
  int quietCount = 0;

  // Track captures searched for capture history gravity (penalize on cutoff).
  PackedMove capturesSearched[32];
  int captureCount = 0;

  while (true) {
    Move m = picker.next();
    if (!m.from && !m.to) break;  // all stages exhausted

    // Skip the excluded move during a singular extension exclusion search.
    if (hasExcluded && m == excludedMove) continue;

    // --- Futility Pruning: skip hopeless quiet moves at shallow depth ---
    // Never prune the first move (we need at least one legal move searched)
    // and never prune captures or promotions (tactical moves can surprise).
    if (futilityPruning && movesSearched > 0 &&
        !m.isCapture() && !m.isPromotion()) {
      STAT_INC(futilityPrunes);
      continue;
    }

    // --- Late Move Pruning (LMP): skip late quiet moves at shallow depth ---
    // Once enough moves have been searched without a beta cutoff, remaining
    // quiet moves at low depth are unlikely to improve — skip them entirely.
    // Guards: not PV, not in check, shallow depth, not the first move,
    //         not a capture or promotion (those are always searched).
    if (!pvNode && !inCheck && depth <= 5 && movesSearched > 0 &&
        movesSearched >= LMP_THRESHOLD[depth] + (improving ? 2 : 0) &&
        !m.isCapture() && !m.isPromotion()) {
      STAT_INC(lmpPrunes);
      continue;
    }

    // --- History Pruning: skip quiet moves with terrible history ---
    // Before making a move, if the history score is deeply negative the
    // engine has consistently found this move to be bad.  Skip it entirely,
    // saving the make/unmake overhead.
    // Guards: not PV, not in check, shallow depth, not first move,
    //         quiet move only.
    //
    // Reference: https://www.chessprogramming.org/History_Leaf_Pruning
    if (!pvNode && !inCheck && depth <= HISTORY_PRUNE_DEPTH &&
        movesSearched > 0 && !m.isCapture() && !m.isPromotion()) {
      uint8_t mc = raw(pos.sideToMove());
      int16_t hist = state.history[mc][raw(pieceType(pos.mailbox()[m.from])) - 1][m.to];
      if (hist < -HISTORY_PRUNE_THRESHOLD * depth) {
        STAT_INC(historyPrunes);
        continue;
      }
    }

    // Capture the moving piece identity before make() alters the mailbox.
    // Used to index the countermove table in child nodes.
    Piece movingPiece = pos.mailbox()[m.from];

    // --- Extensions ---
    // Singular extension: extend the TT move by 1 ply when the exclusion
    // search confirmed it is singular (significantly better than
    // alternatives).  Only applied to the TT move itself.
    // Recapture extension: extend captures on the same square as the
    // previous move to avoid cutting off mid-exchange.
    //
    // At most one extension is applied per move to avoid search explosion.
    //
    // References:
    //   https://www.chessprogramming.org/Singular_Extensions
    //   https://www.chessprogramming.org/Recapture_Extensions
    int extension = 0;
    if (singularExtension && m == ttMove) {
      extension = 1;  // Singular: TT move is clearly best
    } else if (!inCheck && depth >= 2 && m.isCapture() &&
        m.to == prevTo && ply < MAX_PLY - 10) {
      // Use the picker's cached SEE if available; otherwise compute.
      int seVal = picker.lastSee != SEE_NOT_COMPUTED
                     ? picker.lastSee
                     : attacks::see(pos.bitboards(), pos.mailbox(), m);
      if (seVal >= 0) {
        extension = 1;
        STAT_INC(recaptureExtensions);
      }
    }

    UndoInfo undo = pos.make(m);

    int newDepth = depth - 1 + extension;

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
      score = -negamax(pos, newDepth, -beta, -alpha, ply + 1, state,
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
        STAT_INC(lmrSearches);
        // Reduced-depth zero-window scout search.
        // pos is in post-make state; toggle side to get the move maker.
        uint8_t mc = raw(pos.sideToMove()) ^ 1;
        int16_t hist = state.history[mc][raw(pieceType(movingPiece)) - 1][m.to];
        int reduction = computeLMRReduction(depth, movesSearched,
                                            hist, improving, pvNode);

        score = -negamax(pos, newDepth - reduction,
                         -alpha - 1, -alpha, ply + 1, state,
                         movingPiece, m.to);
      } else {
        // Not reducing — force a full-depth scout below.
        score = alpha + 1;
      }

      // If the LMR search (or skip) suggests this move might be good,
      // re-search at full depth with a zero-window.
      if (score > alpha) {
        if (doLMR) STAT_INC(lmrReSearches);
        score = -negamax(pos, newDepth, -alpha - 1, -alpha, ply + 1, state,
                         movingPiece, m.to);
      }

      // PVS re-search: if the zero-window scout failed high within the
      // PV window, we need an exact score — re-search with full window.
      if (score > alpha && score < beta) {
        STAT_INC(pvsReSearches);
        score = -negamax(pos, newDepth, -beta, -alpha, ply + 1, state,
                         movingPiece, m.to);
      }
    }

    pos.unmake(m, undo);

    if (state.stopped) return 0;

    // Track quiet moves for history gravity.
    if (!m.isCapture() && !m.isPromotion() && quietCount < 32)
      quietsSearched[quietCount++] = packMove(m);

    // Track captures for capture history gravity.
    if (m.isCapture() && captureCount < 32)
      capturesSearched[captureCount++] = packMove(m);

    ++movesSearched;

    if (score > bestScore) {
      bestScore = score;
      bestMove = m;
      if (score > alpha) {
        alpha = score;

        // Collect principal variation: this move + the child's PV line.
        collectPV(state, ply, packMove(m));

        if (alpha >= beta) {
          STAT_INC(betaCutoffs);
          if (movesSearched == 1) STAT_INC(firstMoveCutoffs);
          int bonus = depth * depth;
          // Beta cutoff — update move ordering heuristics
          if (m.isCapture()) {
            updateCaptureCutoffHistory(m, pos, state, bonus,
                                       capturesSearched, captureCount);
          } else {
            updateQuietCutoffHeuristics(m, pos, state, ply, bonus,
                                        quietsSearched, quietCount,
                                        prevPiece, prevTo);
          }
          break;
        }
      }
    }
  }

  // No moves searched: checkmate or stalemate.  During exclusion searches
  // (singular extensions), the only legal move may have been excluded —
  // return alpha so the caller correctly identifies it as singular.
  if (movesSearched == 0) {
    if (hasExcluded) return alpha;
    if (inCheck) return -MATE_SCORE + ply;
    return DRAW_SCORE;
  }

  // --- TT store ---
  // Fail-soft: store bestScore (the actual score found), not alpha.
  // Skip TT store during exclusion searches — the result is partial
  // (one move was excluded) and would pollute the TT.
  // Reference: https://www.chessprogramming.org/Fail-Soft
  if (state.tt && !hasExcluded) {
    TTFlag flag;
    if (bestScore <= origAlpha)
      flag = TTFlag::UPPER_BOUND;
    else if (bestScore >= beta)
      flag = TTFlag::LOWER_BOUND;
    else
      flag = TTFlag::EXACT;
    state.tt->store(pos.hash(), scoreToTT(bestScore, ply), bestMove, depth, flag);
  }

  return bestScore;
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
                          SearchState& state,
                          InfoCallback info) {
  // One-time initialization of the logarithmic LMR reduction table.
  initLMR();

  // Advance TT generation so stale entries from previous searches are
  // replaced cheaply by the depth-preferred replacement policy.
  if (state.tt) state.tt->newGeneration();

  SearchResult result;

  // Per-search reset — infrastructure fields (timeFunc, tt, pawnHash,
  // evalHash) are set by the caller and persist across calls.
  state.nodes        = 0;
  state.stopped      = false;
  state.startTime    = state.timeFunc ? state.timeFunc() : 0;
  state.hardTimeMs   = limits.hardTimeMs;
  state.externalStop = limits.stop;
  state.clearHeuristics();

  int maxDepth = limits.maxDepth;
  if (maxDepth <= 0) maxDepth = 1;

  // Generate root moves once (legal moves don't change between iterations)
  MoveList rootMoves;
  movegen::generateAllMoves(pos.bitboards(), pos.mailbox(),
                            pos.sideToMove(), pos.positionState(), rootMoves);

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

  // --- Time management state ---
  // Tracks best move stability and second-best score for easy move
  // detection and move instability time extensions.
  // References:
  //   https://www.chessprogramming.org/Time_Management#Easy_Move
  //   https://www.chessprogramming.org/Time_Management
  Move prevIterBest = {};
  int stableCount = 0;
  int bestMoveChanges = 0;  // total best-move changes across iterations
  uint32_t effectiveSoftTime = limits.softTimeMs;

  for (int depth = 1; depth <= maxDepth; ++depth) {
    // --- Aspiration window setup ---
    int alpha, beta;
    int delta = ASPIRATION_DELTA;
    if (depth == 1) {
      alpha = -INF_SCORE;
      beta  =  INF_SCORE;
    } else {
      alpha = prevScore - delta;
      beta  = prevScore + delta;
    }

    Move iterBestMove = rootMoves.moves[0];
    int iterBestScore = -INF_SCORE;
    int secondBestScore = -INF_SCORE;

    // Aspiration re-search loop: widen the window progressively on
    // fail-low / fail-high, doubling `delta` each time until the score
    // falls within bounds (or overflows to full width).
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
      secondBestScore = -INF_SCORE;

      for (int i = 0; i < rootMoves.count; ++i) {
        Piece movingPiece = pos.mailbox()[rootMoves.moves[i].from];
        UndoInfo undo = pos.make(rootMoves.moves[i]);
        int score = -negamax(pos, depth - 1, -beta, -alpha, 1, state,
                             movingPiece, rootMoves.moves[i].to);
        pos.unmake(rootMoves.moves[i], undo);
        if (state.stopped) break;

        if (score > iterBestScore) {
          secondBestScore = iterBestScore;
          iterBestScore = score;
          iterBestMove = rootMoves.moves[i];

          // Build root PV: this root move + child PV from ply 1.
          collectPV(state, 0, packMove(rootMoves.moves[i]));

          if (score > alpha) alpha = score;
        } else if (score > secondBestScore) {
          secondBestScore = score;
        }
      }

      if (state.stopped) break;

      // Fail-low: every root move scored at or below the aspiration lower
      // bound.  Widen the lower bound and re-search.
      if (iterBestScore <= aspAlpha) {
        delta = (delta < INF_SCORE / 2) ? delta * 2 : INF_SCORE;
        alpha = prevScore - delta;
        if (alpha < -INF_SCORE) alpha = -INF_SCORE;
        continue;
      }
      // Fail-high: the best score met or exceeded the upper bound.
      // Widen the upper bound and re-search.
      if (iterBestScore >= aspBeta) {
        delta = (delta < INF_SCORE / 2) ? delta * 2 : INF_SCORE;
        beta = prevScore + delta;
        if (beta > INF_SCORE) beta = INF_SCORE;
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

    // Unpack principal variation from this iteration's PV table.
    result.pvLength = state.pvLength[0];
    if (result.pvLength < 0) result.pvLength = 0;
    if (result.pvLength > MAX_PV_LEN) result.pvLength = MAX_PV_LEN;
    for (int i = 0; i < result.pvLength; ++i)
      result.pv[i] = unpackMove(state.pv[0][i]);

    // --- Root move reordering ---
    reorderRootMoves(rootMoves, iterBestMove);

    // Notify caller (e.g. UCI "info" line)
    if (info) info(result);

    // Early exit: found a forced mate — no need to search deeper
    if (iterBestScore >= MATE_SCORE - MAX_PLY) break;

    // --- Time management: stability tracking + instability extension ---
    // Track how many consecutive iterations had the same best move.
    if (iterBestMove.from == prevIterBest.from &&
        iterBestMove.to   == prevIterBest.to   &&
        iterBestMove.flags == prevIterBest.flags) {
      ++stableCount;
    } else {
      // Move instability: when the best move changes at depth >= 4,
      // scale the effective soft time by a dynamic factor that grows
      // with accumulated instability (more changes → more time).
      // Base factor: 1.5×, + 0.25× per prior change (capped at 2.5×).
      // Reference: https://www.chessprogramming.org/Time_Management
      if (depth >= 4 && effectiveSoftTime > 0) {
        int factor = 150 + bestMoveChanges * 25;
        if (factor > 250) factor = 250;
        effectiveSoftTime = effectiveSoftTime * factor / 100;
        if (limits.hardTimeMs > 0 && effectiveSoftTime > limits.hardTimeMs)
          effectiveSoftTime = limits.hardTimeMs;
      }
      ++bestMoveChanges;
      stableCount = 1;
    }
    prevIterBest = iterBestMove;

    // --- Easy move detection ---
    // If the best move has been stable for >= 4 iterations, leads the
    // second-best by >= 100cp, and we're deep enough, stop early.
    // Reference: https://www.chessprogramming.org/Time_Management#Easy_Move
    if (effectiveSoftTime > 0 && stableCount >= 4 && depth >= 6 &&
        secondBestScore > -INF_SCORE &&
        iterBestScore - secondBestScore >= 100) {
      break;
    }

    // --- Soft time check ---
    // If the soft time limit has elapsed, don't start the next iteration.
    if (effectiveSoftTime > 0 && state.timeFunc) {
      uint32_t elapsed = state.timeFunc() - state.startTime;
      if (elapsed >= effectiveSoftTime) break;
    }
  }

  result.nodes = state.nodes;

  return result;
}

}  // namespace search
}  // namespace LibreChess
