#include "search.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

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
  uint32_t key32 = static_cast<uint32_t>(hash >> 32);
  TTEntry& e = entries[index];

  // Depth-preferred replacement with generation awareness.
  // Reference: https://www.chessprogramming.org/Replacement_Strategy
  bool replace = (e.key32 == 0 && e.depth == 0)    // empty slot
              || (e.key32 == key32)                 // same position (update)
              || (e.generation != generation)       // stale entry from old search
              || (flag == TTFlag::EXACT)             // exact scores always preferred
              || (depth >= e.depth);                 // deeper search preferred
  if (!replace) return;

  e.key32      = key32;
  e.score      = static_cast<int16_t>(score);
  e.bestMove   = packMove(bestMove);
  e.depth      = static_cast<int8_t>(depth);
  e.flag       = flag;
  e.generation = generation;
}

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
// Node check interval — every 512 nodes, poll time and external stop.
// Lower values give finer time resolution at negligible overhead cost.
// ---------------------------------------------------------------------------

constexpr uint32_t CHECK_INTERVAL = 512;

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
// Late Move Reduction constants and logarithmic reduction table.
//
// LMR_FULL_DEPTH_MOVES: number of moves searched at full depth before
// applying reductions.  The first few moves (TT, captures, killers) are
// searched at full depth; later quiet moves get reduced.
// LMR_DEPTH_THRESHOLD: minimum remaining depth to apply LMR.
//
// LMR_TABLE: precomputed base reduction indexed by [depth][moveIndex].
// Formula: max(1, int(0.75 + ln(depth) * ln(moveIndex) / 2.5)).
// History, improving, and clamp adjustments are applied on top.
//
// Reference:
//   https://www.chessprogramming.org/Late_Move_Reductions
//   https://www.chessprogramming.org/Late_Move_Reductions#Base_Reduction
// ---------------------------------------------------------------------------

static constexpr int LMR_FULL_DEPTH_MOVES = 4;
static constexpr int LMR_DEPTH_THRESHOLD  = 3;
static constexpr int LMR_MAX_MOVES        = 64;

static int LMR_TABLE[MAX_PLY][LMR_MAX_MOVES];
static bool lmrInitialized = false;

static void initLMR() {
  if (lmrInitialized) return;
  for (int d = 0; d < MAX_PLY; ++d) {
    for (int m = 0; m < LMR_MAX_MOVES; ++m) {
      if (d == 0 || m == 0)
        LMR_TABLE[d][m] = 0;
      else
        LMR_TABLE[d][m] = std::max(
            1, static_cast<int>(0.75 + std::log(d) * std::log(m) / 2.0));
    }
  }
  lmrInitialized = true;
}

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
// At shallow depths (depth <= 5) in non-PV, non-check nodes, once enough
// moves have been searched without improving alpha, remaining quiet moves
// are skipped entirely.  More aggressive than LMR (which only reduces
// depth; LMP skips the move outright).
//
// LMP_THRESHOLD[depth] = max quiet moves to search before pruning the rest.
// Captures, promotions, and the first move are never pruned.
//
// Reference: https://www.chessprogramming.org/Late_Move_Pruning
// ---------------------------------------------------------------------------

static constexpr int LMP_THRESHOLD[] = {0, 5, 12, 20, 30, 42};  // indexed by depth

// ---------------------------------------------------------------------------
// History Pruning constants.
//
// At shallow depths, quiet moves with very poor history scores are pruned
// before make_move, avoiding the overhead of making and unmaking a move
// the engine has consistently found to be bad.
//
// HISTORY_PRUNE_DEPTH: maximum depth at which history pruning applies.
// HISTORY_PRUNE_THRESHOLD: the per-depth scaling factor.  A move is pruned
// if history[color][from][to] < -THRESHOLD * depth.
//
// Reference: https://www.chessprogramming.org/History_Leaf_Pruning
// ---------------------------------------------------------------------------

static constexpr int HISTORY_PRUNE_DEPTH     = 4;
static constexpr int HISTORY_PRUNE_THRESHOLD = 1024;

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
// Reverse Futility Pruning (Static Null Move Pruning) margin.
//
// At shallow depths in non-PV, non-check nodes, if the static eval minus
// a depth-dependent margin still exceeds beta, the position is so strong
// that searching moves is unnecessary — prune the entire subtree.
//
// RFP_MARGIN: centipawns per depth.  E.g. at depth 3: need eval >= beta+360.
//
// Reference: https://www.chessprogramming.org/Reverse_Futility_Pruning
// ---------------------------------------------------------------------------

static constexpr int RFP_MARGIN = 120;  // per depth

// ---------------------------------------------------------------------------
// Internal Iterative Deepening (IID) constants.
//
// Internal Iterative Reductions (IIR) constants.
//
// At PV nodes without a TT move, move ordering is essentially blind —
// the first move searched is arbitrary.  IIR reduces depth by one ply
// instead of running a full shallow search (IID), saving the overhead
// of a recursive search while still allowing the TT to be populated
// by the reduced-depth iteration.
//
// IID_DEPTH_THRESHOLD: minimum remaining depth to trigger IIR (shallow
// nodes don't benefit enough to justify the reduction).
//
// Reference: https://www.chessprogramming.org/Internal_Iterative_Reductions
// ---------------------------------------------------------------------------

static constexpr int IID_DEPTH_THRESHOLD = 4;

// ---------------------------------------------------------------------------
// Singular Extensions constants.
//
// At nodes where the TT move appears significantly better than all
// alternatives, extend its search by one ply.  Detects "singular" moves
// via a reduced-depth exclusion search: search all moves except the TT
// move at reduced depth with a narrow window around ttScore - margin.
// If nothing beats that threshold, the TT move is singular and extended.
//
// SE_DEPTH_THRESHOLD: minimum remaining depth to trigger SE (shallow
// nodes don't need the overhead of an extra search).
// SE_MARGIN_SCALE: margin = SE_MARGIN_SCALE * depth (scales with depth).
// SE_REDUCTION: depth reduction for the exclusion search (depth / 2).
//
// Reference: https://www.chessprogramming.org/Singular_Extensions
// ---------------------------------------------------------------------------

static constexpr int SE_DEPTH_THRESHOLD = 6;
static constexpr int SE_MARGIN_SCALE    = 2;  // singularBeta = ttScore - 2*depth

// ---------------------------------------------------------------------------
// Lazy Evaluation margin.
//
// Before computing the expensive full evaluation (mobility, king safety,
// pawn structure, etc.), a cheap material-only score is computed.  If this
// score is outside the [alpha - margin, beta + margin] window, the full
// evaluation is unlikely to change the search outcome, so it is skipped.
//
// LAZY_EVAL_MARGIN: the window extension in centipawns.
// Set conservatively to avoid pruning positions where positional terms
// would flip the score.  Only applied in non-PV, non-check nodes.
//
// Reference: https://www.chessprogramming.org/Lazy_Evaluation
// ---------------------------------------------------------------------------

static constexpr int LAZY_EVAL_MARGIN = 300;

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
// Move ordering — staged move generation via MovePicker.
//
// Instead of generating all legal moves upfront and scoring them, moves are
// produced in priority stages.  If an early stage (TT move, good capture)
// causes a beta cutoff, later stages (killers, quiets, bad captures) are
// skipped entirely — saving the cost of generating and scoring them.
//
// Stage pipeline:
//   TT_MOVE → INIT_CAPTURES → GOOD_CAPTURES → KILLERS → COUNTERMOVE →
//   INIT_QUIETS → QUIETS → BAD_CAPTURES → DONE
//
// Score priority bands (within stages):
//   Good captures:  MVV-LVA (victim*16 - attacker) + captureHistory  [SEE≥0]
//   Quiets:         history[color][from][to]  (0 .. ~7000)
//   Bad captures:   SEE value (negative)                              [SEE<0]
//
// References:
//   https://www.chessprogramming.org/Move_Ordering
//   https://www.chessprogramming.org/Move_Ordering#Staged_Move_Generation
//   https://www.chessprogramming.org/MVV-LVA
//   https://www.chessprogramming.org/Killer_Move
//   https://www.chessprogramming.org/History_Heuristic
//   https://www.chessprogramming.org/Static_Exchange_Evaluation
// ---------------------------------------------------------------------------

// Simple piece value for MVV-LVA (indexed by PieceType).
// PieceType: NONE=0, PAWN=1, KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5, KING=6.
static constexpr int MVV_LVA_VALUE[] = {0, 1, 3, 3, 5, 9, 0};

// Sentinel SEE value — move has no precomputed SEE.
static constexpr int SEE_NOT_COMPUTED = -32000;

// Validate that a Move struct represents a legal move in the given position.
// Used by MovePicker to validate TT, killer, and countermove candidates.
// ---------------------------------------------------------------------------
// Move validation + flag reconstruction for TT / killer / countermoves.
//
// Moves stored in the TT, killer slots, or countermove table carry flags
// (EP, castling, capture, promotion) from the position where they were
// recorded.  On a hash collision or when replayed at a different node in
// the search tree, those flags may be stale (e.g. EP flag set when no EP
// is possible).  Playing a move with wrong flags causes Position::make()
// to take an incorrect code path — specifically, the EP path reads
// mailbox_[epSq] expecting a pawn but finds NONE, producing an invalid
// pieceZobristIndex of -1 and a PST array-out-of-bounds access.
//
// Fix: after confirming from→to is pseudo-legal, **reconstruct** the
// flags from the current position so make() always sees correct metadata.
// The promotion-type index (bits 4-5) is preserved from the stored move
// since it encodes the engine's deliberate choice.
//
// Reference: https://www.chessprogramming.org/Transposition_Table#Move_from_another_Position
// ---------------------------------------------------------------------------

static bool isMoveValid(const BitboardSet& bb, const Piece mailbox[],
                        Move& m, const PositionState& state, Color side) {
  // Reject moves where the piece doesn't belong to the current side.
  Piece piece = mailbox[m.from];
  if (piece == Piece::NONE || piece::pieceColor(piece) != side)
    return false;

  // Step 1: validate from→to is legal (pseudo-legal + does not leave
  //         own king in check).
  if (!movegen::isValidMove(bb, mailbox,
        rowOf(m.from), colOf(m.from), rowOf(m.to), colOf(m.to), state))
    return false;

  // Step 2: reconstruct flags from the current position.
  PieceType pt = piece::pieceType(piece);
  int toRow = rowOf(m.to);
  int toCol = colOf(m.to);

  uint8_t flags = 0;

  // Capture: destination has an opponent piece.
  if (mailbox[m.to] != Piece::NONE)
    flags |= MOVE_CAPTURE;

  // En passant: pawn moving diagonally to the EP target square.
  if (pt == PieceType::PAWN && state.epRow >= 0 &&
      toRow == state.epRow && toCol == state.epCol)
    flags |= MOVE_EP | MOVE_CAPTURE;

  // Castling: king sliding two files.
  if (pt == PieceType::KING && abs(colOf(m.from) - toCol) == 2)
    flags |= MOVE_CASTLING;

  // Promotion: pawn reaching the back rank; preserve promo-type choice.
  if (pt == PieceType::PAWN && toRow == piece::promotionRow(side))
    flags |= MOVE_PROMOTION | (m.flags & (0x03 << MOVE_PROMO_SHIFT));

  m.flags = flags;
  return true;
}

// Partial selection sort: find the best-scored move in [start, end) and
// swap it to position `start`.  If `seeCache` is non-null, its entries are
// swapped in parallel (keeps SEE values aligned with their moves).
inline void pickBestInRange(Move moves[], int scores[], int start, int end,
                            int seeCache[] = nullptr) {
  int bestIdx = start;
  for (int i = start + 1; i < end; ++i) {
    if (scores[i] > scores[bestIdx]) bestIdx = i;
  }
  if (bestIdx != start) {
    Move tmpM = moves[start];
    moves[start] = moves[bestIdx];
    moves[bestIdx] = tmpM;
    int tmpS = scores[start];
    scores[start] = scores[bestIdx];
    scores[bestIdx] = tmpS;
    if (seeCache) {
      int tmpSee = seeCache[start];
      seeCache[start] = seeCache[bestIdx];
      seeCache[bestIdx] = tmpSee;
    }
  }
}

// Convenience: pick best in [start, moves.count).
inline void pickBest(MoveList& moves, int scores[], int start,
                    int seeCache[] = nullptr) {
  pickBestInRange(moves.moves, scores, start, moves.count, seeCache);
}

// ---------------------------------------------------------------------------
// Safe access helpers for heuristic tables indexed by pieceZobristIndex.
//
// centralise the bounds check (isValidZobristIndex + victim range) so that
// every callsite uses the same logic.  Returns 0 / no-op on invalid index.
// ---------------------------------------------------------------------------

// Read a captureHistory score with full bounds validation.
inline int16_t safeCaptureHistScore(const SearchState& ss, int pieceIdx,
                                    PieceType victim, int toSq) {
  int vi = raw(victim) - 1;
  if (isValidZobristIndex(pieceIdx) && vi >= 0 && vi < 6)
    return ss.captureHistory[pieceIdx][vi][toSq];
  return 0;
}

// Read a countermove entry with bounds validation.
inline PackedMove safeCountermove(const SearchState& ss, int pieceIdx,
                                  int toSq) {
  if (isValidZobristIndex(pieceIdx))
    return ss.countermoves[pieceIdx][toSq];
  return 0;
}

// ---------------------------------------------------------------------------
// MovePicker — staged move generation for the negamax search.
//
// Lazily generates moves in priority order so that a beta cutoff from a
// high-priority move (TT, good capture) avoids the cost of generating
// lower-priority moves (quiets).  Captures and quiets share a single
// MoveList to minimise per-frame stack usage:
//
//   moves[0, totalCaps)       — captures (good in front, bad in back)
//   moves[totalCaps, count)   — quiet moves (appended in INIT_QUIETS)
//
// Reference: https://www.chessprogramming.org/Move_Ordering#Staged_Move_Generation
// ---------------------------------------------------------------------------

struct MovePicker {
  // Pipeline stages — processed in declaration order.
  enum Stage : uint8_t {
    STAGE_TT,             // Yield the TT move (validated for legality)
    STAGE_INIT_CAPTURES,  // Generate + score + partition captures
    STAGE_GOOD_CAPTURES,  // Yield good captures (SEE >= 0) via pickBest
    STAGE_KILLERS,        // Yield killer[0], killer[1] if valid quiet
    STAGE_COUNTERMOVE,    // Yield countermove if valid quiet + not duplicate
    STAGE_INIT_QUIETS,    // Generate all moves, filter, score with history
    STAGE_QUIETS,         // Yield quiet moves via pickBest
    STAGE_BAD_CAPTURES,   // Yield losing captures (SEE < 0)
    STAGE_DONE            // All stages exhausted
  };

  Stage stage;

  // Position data (non-owning references, valid for the picker's lifetime).
  const BitboardSet* bb;
  const Piece* mailbox;
  Color side;
  const PositionState* posState;

  // Search heuristics (non-owning).
  const SearchState* ss;
  int ply;

  // Special moves — validated and yielded individually.
  Move ttMove;
  Move killer0, killer1, counterMove;
  bool hasTT, hasCounter;

  // Track which special moves were actually yielded (for duplicate skip).
  bool ttYielded, killer0Yielded, killer1Yielded, counterYielded;
  int killerPhase;  // 0 = try killer0, 1 = try killer1

  // Shared move storage:
  //   [0, goodCapCount)           — good captures (SEE >= 0)
  //   [goodCapCount, totalCaps)   — bad captures  (SEE < 0)
  //   [totalCaps, moves.count)    — quiet moves
  MoveList moves;
  int scores[MAX_MOVES];
  int seeValues[MAX_MOVES];
  int totalCaps;     // total captures generated
  int goodCapCount;  // good captures in [0, goodCapCount)
  int capIdx;        // next good capture index
  int badCapIdx;     // next bad capture index
  int quietIdx;      // next quiet move index

  // SEE of the last move returned (SEE_NOT_COMPUTED for non-captures).
  int lastSee;

  // -----------------------------------------------------------------------
  // Initialise the picker for a position.
  // -----------------------------------------------------------------------

  void init(const BitboardSet& bbRef, const Piece mail[], Color s,
            const PositionState& ps, const SearchState& ssRef,
            int p, Move tt, Piece prevPiece, int prevTo) {
    bb       = &bbRef;
    mailbox  = mail;
    side     = s;
    posState = &ps;
    ss       = &ssRef;
    ply      = p;

    ttMove = tt;
    hasTT  = (tt.from != 0 || tt.to != 0);

    killer0 = ssRef.killers[p][0];
    killer1 = ssRef.killers[p][1];

    hasCounter = false;
    if (!isEmpty(prevPiece)) {
      int idx = pieceZobristIndex(prevPiece);
      PackedMove cpm = safeCountermove(ssRef, idx, prevTo);
      if (cpm != 0) {
        counterMove = unpackMove(cpm);
        hasCounter  = true;
      }
    }

    stage        = STAGE_TT;
    killerPhase  = 0;
    capIdx       = 0;
    badCapIdx    = 0;
    goodCapCount = 0;
    totalCaps    = 0;
    quietIdx     = 0;
    moves.count  = 0;
    lastSee      = SEE_NOT_COMPUTED;
    ttYielded    = false;
    killer0Yielded  = false;
    killer1Yielded  = false;
    counterYielded  = false;
  }

  // -----------------------------------------------------------------------
  // Return the next move in priority order.
  // Returns Move() (from=to=flags=0) when all stages are exhausted.
  // Sets `lastSee` to the SEE value for captures, SEE_NOT_COMPUTED otherwise.
  // -----------------------------------------------------------------------

  Move next() {
    while (stage != STAGE_DONE) {
      switch (stage) {

        // --- TT move: highest priority, validated for legality ----------
        case STAGE_TT:
          stage = STAGE_INIT_CAPTURES;
          if (hasTT && isMoveValid(*bb, mailbox, ttMove, *posState, side)) {
            lastSee = SEE_NOT_COMPUTED;
            ttYielded = true;
            return ttMove;
          }
          continue;

        // --- Generate + score + partition captures ----------------------
        case STAGE_INIT_CAPTURES:
          initCaptures();
          capIdx = 0;
          stage = STAGE_GOOD_CAPTURES;
          continue;

        // --- Good captures (SEE >= 0) via pickBest ----------------------
        case STAGE_GOOD_CAPTURES:
          while (capIdx < goodCapCount) {
            pickBestInRange(moves.moves, scores, capIdx, goodCapCount,
                            seeValues);
            Move m   = moves.moves[capIdx];
            int  see = seeValues[capIdx];
            ++capIdx;
            if (ttYielded && m == ttMove) continue;
            lastSee = see;
            return m;
          }
          killerPhase = 0;
          stage = STAGE_KILLERS;
          continue;

        // --- Killer moves (quiet, not duplicate) ------------------------
        case STAGE_KILLERS:
          while (killerPhase < 2) {
            Move km = (killerPhase == 0) ? killer0 : killer1;
            ++killerPhase;
            // Skip null/empty killers
            if (km.from == 0 && km.to == 0) continue;
            // Skip if same as TT move
            if (ttYielded && km == ttMove) continue;
            // Skip if this is a capture in the current position
            if (mailbox[km.to] != Piece::NONE) continue;
            // Validate legality
            if (!isMoveValid(*bb, mailbox, km, *posState, side)) continue;
            lastSee = SEE_NOT_COMPUTED;
            if (killerPhase == 1)
              killer0Yielded = true;
            else
              killer1Yielded = true;
            return km;
          }
          stage = STAGE_COUNTERMOVE;
          continue;

        // --- Countermove (quiet, not duplicate) -------------------------
        case STAGE_COUNTERMOVE:
          stage = STAGE_INIT_QUIETS;
          if (hasCounter) {
            Move cm = counterMove;
            if (!(ttYielded && cm == ttMove) &&
                !(killer0Yielded && cm == killer0) &&
                !(killer1Yielded && cm == killer1) &&
                cm.from != 0 && // not null
                mailbox[cm.to] == Piece::NONE &&
                isMoveValid(*bb, mailbox, cm, *posState, side)) {
              lastSee = SEE_NOT_COMPUTED;
              counterYielded = true;
              return cm;
            }
          }
          continue;

        // --- Generate quiet moves: all moves minus captures/specials ----
        case STAGE_INIT_QUIETS:
          initQuiets();
          stage = STAGE_QUIETS;
          continue;

        // --- Quiet moves via pickBest -----------------------------------
        case STAGE_QUIETS:
          while (quietIdx < moves.count) {
            pickBestInRange(moves.moves, scores, quietIdx, moves.count);
            Move m = moves.moves[quietIdx];
            ++quietIdx;
            // Skip duplicates of earlier stages
            if (ttYielded && m == ttMove) continue;
            if (killer0Yielded && m == killer0) continue;
            if (killer1Yielded && m == killer1) continue;
            if (counterYielded && m == counterMove) continue;
            lastSee = SEE_NOT_COMPUTED;
            return m;
          }
          badCapIdx = goodCapCount;
          stage = STAGE_BAD_CAPTURES;
          continue;

        // --- Bad captures (SEE < 0) ------------------------------------
        case STAGE_BAD_CAPTURES:
          while (badCapIdx < totalCaps) {
            pickBestInRange(moves.moves, scores, badCapIdx, totalCaps,
                            seeValues);
            Move m   = moves.moves[badCapIdx];
            int  see = seeValues[badCapIdx];
            ++badCapIdx;
            if (ttYielded && m == ttMove) continue;
            lastSee = see;
            return m;
          }
          stage = STAGE_DONE;
          continue;

        case STAGE_DONE:
          break;
      }
    }
    lastSee = SEE_NOT_COMPUTED;
    return Move();
  }

private:
  // -----------------------------------------------------------------------
  // Generate captures, score with MVV-LVA + SEE + captureHistory, and
  // partition into good (front) and bad (back).
  // -----------------------------------------------------------------------

  void initCaptures() {
    movegen::generateCaptures(*bb, mailbox, side, *posState, moves);
    totalCaps = moves.count;

    for (int i = 0; i < totalCaps; ++i) {
      const Move& m = moves.moves[i];
      int seeVal = attacks::see(*bb, mailbox, m);
      seeValues[i] = seeVal;

      if (seeVal >= 0) {
        PieceType victim   = m.isEP() ? PieceType::PAWN
                                      : pieceType(mailbox[m.to]);
        PieceType attacker = pieceType(mailbox[m.from]);
        int mvvlva = MVV_LVA_VALUE[raw(victim)] * 16 -
                     MVV_LVA_VALUE[raw(attacker)];
        int capHistIdx = pieceZobristIndex(mailbox[m.from]);
        int capHist = safeCaptureHistScore(*ss, capHistIdx, victim, m.to);
        scores[i] = mvvlva + capHist / 16;
      } else {
        scores[i] = seeVal;  // negative — sorted below all other moves
      }
    }

    // Partition: good captures to [0, goodCapCount), bad to the rest.
    goodCapCount = 0;
    for (int i = 0; i < totalCaps; ++i) {
      if (seeValues[i] >= 0) {
        if (i != goodCapCount) {
          std::swap(moves.moves[i], moves.moves[goodCapCount]);
          std::swap(scores[i], scores[goodCapCount]);
          std::swap(seeValues[i], seeValues[goodCapCount]);
        }
        ++goodCapCount;
      }
    }
  }

  // -----------------------------------------------------------------------
  // Generate all legal moves, filter out captures (already handled) and
  // append the remaining quiet moves after the captures section.
  // Score quiets with the history heuristic.
  // -----------------------------------------------------------------------

  void initQuiets() {
    MoveList allMoves;
    movegen::generateAllMoves(*bb, mailbox, side, *posState, allMoves);

    uint8_t c = raw(side);
    for (int i = 0; i < allMoves.count; ++i) {
      const Move& m = allMoves.moves[i];
      if (m.isCapture()) continue;  // already in capture stages
      int idx = moves.count;
      moves.moves[idx] = m;
      scores[idx] = ss->history[c][m.from][m.to];
      seeValues[idx] = SEE_NOT_COMPUTED;
      ++moves.count;
    }
    quietIdx = totalCaps;
  }
};

// Update killer moves: slot the new killer into position 0, shifting the
// old one to position 1.  Avoids duplicates.
inline void updateKillers(Move m, int ply, SearchState& state) {
  if (!(m == state.killers[ply][0])) {
    state.killers[ply][1] = state.killers[ply][0];
    state.killers[ply][0] = m;
  }
}

// History heuristic with gravity — deeper cutoffs get more weight,
// but scores decay toward zero as they approach the cap.  The gravity
// formula prevents bias: multiple small penalties don't permanently
// push scores to -MAX.
//
// Unified for both bonus (beta cutoff, positive) and penalty (non-cutoff
// quiet moves, negative).  Formula: h += bonus − h × |bonus| / MAX.
//
// Reference: https://www.chessprogramming.org/History_Heuristic#History_Gravity
static constexpr int HISTORY_MAX = 7000;

inline void updateHistory(int16_t& h, int bonus) {
  h += bonus - h * (bonus < 0 ? -bonus : bonus) / HISTORY_MAX;
}

// ---------------------------------------------------------------------------
// Material-only evaluation from the side-to-move perspective.
// Counts material using eval::materialValue() — the single source of truth
// for piece values.  No positional terms; used by lazy evaluation to decide
// if the full eval is worth computing.
// ---------------------------------------------------------------------------

int lazyEval(const Position& pos) {
  const BitboardSet& bb = pos.bitboards();
  int score = 0;
  // PieceType enum: PAWN=1 .. QUEEN=5, matching byPiece index + 1.
  for (int i = 0; i < 6; ++i) {
    int val = eval::materialValue(static_cast<PieceType>(i + 1));
    score += popcount(bb.byPiece[i]) * val;
    score -= popcount(bb.byPiece[i + 6]) * val;
  }
  return (pos.sideToMove() == Color::WHITE) ? score : -score;
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
                                     state.pawnHash);
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
// When the side to move is **in check**, standing pat is not valid (the
// position might be checkmate), so all evasion moves are generated and
// searched.  Delta and SEE pruning are also skipped under check — we
// must find any legal escape or confirm checkmate.
//
// Reference: https://www.chessprogramming.org/Quiescence_Search
// ---------------------------------------------------------------------------

int quiescence(Position& pos, int alpha, int beta, int ply,
               SearchState& state) {
  state.nodes++;

  // --- Ply overflow guard ---
  // Check evasions generate all moves (including quiets), which can cause
  // unbounded recursion in positions with perpetual checks.  Bound the
  // qsearch depth to the same MAX_PLY limit used by negamax.
  // Reference: https://www.chessprogramming.org/Quiescence_Search
  if (ply >= MAX_PLY - 1) return evaluate(pos, state);

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
  MoveList moves;
  if (inCheck)
    movegen::generateAllMoves(pos.bitboards(), pos.mailbox(),
                              pos.sideToMove(), pos.positionState(), moves);
  else
    movegen::generateCaptures(pos.bitboards(), pos.mailbox(),
                              pos.sideToMove(), pos.positionState(), moves);

  // --- MVV-LVA ordering for captures ---
  // Score captures by Most Valuable Victim − Least Valuable Aggressor so
  // the best trades are tried first, improving beta-cutoff rates.
  // Non-captures (evasions under check) get a neutral score.
  // Reference: https://www.chessprogramming.org/MVV-LVA
  int capScores[MAX_MOVES];
  for (int j = 0; j < moves.count; ++j) {
    const Move& cm = moves.moves[j];
    if (cm.isCapture()) {
      PieceType victim   = cm.isEP() ? PieceType::PAWN
                                     : pieceType(pos.mailbox()[cm.to]);
      PieceType attacker = pieceType(pos.mailbox()[cm.from]);
      capScores[j] = MVV_LVA_VALUE[raw(victim)] * 16 -
                     MVV_LVA_VALUE[raw(attacker)];
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
    int score = -quiescence(pos, -beta, -alpha, ply + 1, state);
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
  if (inCheck) ++depth;

  // --- Horizon: quiescence search ---
  if (depth <= 0) return quiescence(pos, alpha, beta, ply, state);

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
    return quiescence(pos, alpha, beta, ply, state);
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
    const TTEntry* entry = state.tt->probe(pos.hash());
    ttEntry = entry;  // save for SE probe
    // Skip TT cutoffs when inside an exclusion search — we need to
    // search all non-excluded moves regardless of TT score.
    if (entry && entry->depth >= depth && !hasExcluded) {
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
  Move quietsSearched[64];
  int quietCount = 0;

  // Track captures searched for capture history gravity (penalize on cutoff).
  Move capturesSearched[64];
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
      int16_t hist = state.history[mc][m.from][m.to];
      if (hist < -HISTORY_PRUNE_THRESHOLD * depth) continue;
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
      if (seVal >= 0) extension = 1;
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
        // Reduced-depth zero-window scout search.
        // Base reduction from logarithmic table, with history and
        // improving adjustments on top.
        // depth < MAX_PLY is guaranteed by the ply overflow guard
        // (ply >= MAX_PLY - 1 → return) which bounds recursion depth.
        int mi = movesSearched < LMR_MAX_MOVES ? movesSearched
                                               : LMR_MAX_MOVES - 1;
        int reduction = LMR_TABLE[depth][mi];

        // History-based LMR: adjust reduction by history score.
        // With gravity-based history, scores are more centered — use
        // tighter thresholds than traditional linear history.
        // pos is in post-make state; toggle side to get the move maker.
        uint8_t mc = raw(pos.sideToMove()) ^ 1;
        int16_t hist = state.history[mc][m.from][m.to];
        if (hist < -500)
          ++reduction;           // Bad history → reduce more
        else if (hist > 1500)
          --reduction;           // Good history → reduce less
        if (!improving)
          ++reduction;           // Not improving → reduce more
        if (!pvNode)
          ++reduction;           // Non-PV nodes → reduce more
        if (reduction < 1) reduction = 1;
        if (reduction > newDepth - 2) reduction = newDepth - 2;
        if (reduction < 1) reduction = 1;

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
        score = -negamax(pos, newDepth, -alpha - 1, -alpha, ply + 1, state,
                         movingPiece, m.to);
      }

      // PVS re-search: if the zero-window scout failed high within the
      // PV window, we need an exact score — re-search with full window.
      if (score > alpha && score < beta) {
        score = -negamax(pos, newDepth, -beta, -alpha, ply + 1, state,
                         movingPiece, m.to);
      }
    }

    pos.unmake(m, undo);

    if (state.stopped) return 0;

    // Track quiet moves for history gravity.
    if (!m.isCapture() && !m.isPromotion() && quietCount < 64)
      quietsSearched[quietCount++] = m;

    // Track captures for capture history gravity.
    if (m.isCapture() && captureCount < 64)
      capturesSearched[captureCount++] = m;

    ++movesSearched;

    if (score > bestScore) {
      bestScore = score;
      bestMove = m;
      if (score > alpha) {
        alpha = score;

        // Collect principal variation: this move + the child's PV line.
        state.pv[ply][0] = m;
        int childLen = (ply + 1 < MAX_PLY) ? state.pvLength[ply + 1] : 0;
        if (childLen < 0) childLen = 0;
        if (childLen > MAX_PLY - 1 - ply) childLen = MAX_PLY - 1 - ply;
        std::memcpy(&state.pv[ply][1], &state.pv[ply + 1][0],
                    childLen * sizeof(Move));
        state.pvLength[ply] = childLen + 1;

        if (alpha >= beta) {
          int bonus = depth * depth;
          // Beta cutoff — update move ordering heuristics
          if (m.isCapture()) {
            // Capture history: reward the cutoff capture, penalize prior captures.
            // Board is in pre-move state (unmake already called), so
            // mailbox[to] still holds the victim piece for any capture.
            int chIdx = pieceZobristIndex(pos.mailbox()[m.from]);
            PieceType victim = m.isEP() ? PieceType::PAWN
                                        : pieceType(pos.mailbox()[m.to]);
            int vi = raw(victim) - 1;
            if (isValidZobristIndex(chIdx) && vi >= 0 && vi < 6)
              updateHistory(state.captureHistory[chIdx][vi][m.to], bonus);
            for (int ci = 0; ci < captureCount - 1; ++ci) {
              const Move& cm = capturesSearched[ci];
              int prevIdx = pieceZobristIndex(pos.mailbox()[cm.from]);
              PieceType prevVictim = cm.isEP() ? PieceType::PAWN
                                               : pieceType(pos.mailbox()[cm.to]);
              int pvi = raw(prevVictim) - 1;
              if (isValidZobristIndex(prevIdx) && pvi >= 0 && pvi < 6)
                updateHistory(state.captureHistory[prevIdx][pvi][cm.to], -bonus);
            }
          } else {
            // Quiet cutoff — update killers, history, countermoves.
            updateKillers(m, ply, state);
            updateHistory(state.history[raw(pos.sideToMove())][m.from][m.to],
                          bonus);

            // History gravity: penalize previously searched quiet moves
            // that failed to cause a cutoff.  The cutoff move itself (last
            // entry in quietsSearched) is excluded — it received the bonus.
            Color side = pos.sideToMove();
            for (int q = 0; q < quietCount - 1; ++q)
              updateHistory(
                  state.history[raw(side)][quietsSearched[q].from]
                               [quietsSearched[q].to],
                  -bonus);

            // Store as countermove for the opponent's previous (piece, toSq).
            // On future visits, this move will be tried earlier when the same
            // previous move is encountered.
            if (!isEmpty(prevPiece)) {
              int idx = pieceZobristIndex(prevPiece);
              if (isValidZobristIndex(idx))
                state.countermoves[idx][prevTo] = packMove(m);
            }
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
                          TranspositionTable* tt,
                          eval::PawnHashTable* pawnHash,
                          eval::EvalHashTable* evalHash) {
  // One-time initialization of the logarithmic LMR reduction table.
  initLMR();

  // Advance TT generation so stale entries from previous searches are
  // replaced cheaply by the depth-preferred replacement policy.
  if (tt) tt->newGeneration();

  // Heap-allocate SearchState (~39 KiB) to avoid overflowing the 16 KiB
  // FreeRTOS task stack.  Contains history[2][64][64] (16 KiB), killers
  // (384 B), countermoves (1.5 KiB), and triangular PV table (~12 KiB).
  auto statePtr = std::make_unique<SearchState>();
  SearchState& state = *statePtr;
  state.timeFunc     = timeFunc;
  state.startTime    = timeFunc ? timeFunc() : 0;
  state.hardTimeMs   = limits.hardTimeMs;
  state.externalStop = limits.stop;
  state.tt           = tt;
  state.pawnHash     = pawnHash;
  state.evalHash     = evalHash;
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
        int score = searchRootMove(pos, rootMoves.moves[i], depth,
                                   alpha, beta, state);
        if (state.stopped) break;

        if (score > iterBestScore) {
          secondBestScore = iterBestScore;
          iterBestScore = score;
          iterBestMove = rootMoves.moves[i];

          // Build root PV: this root move + child PV from ply 1.
          state.pv[0][0] = rootMoves.moves[i];
          int childLen = state.pvLength[1];
          if (childLen < 0) childLen = 0;
          if (childLen > MAX_PLY - 1) childLen = MAX_PLY - 1;
          std::memcpy(&state.pv[0][1], &state.pv[1][0],
                      childLen * sizeof(Move));
          state.pvLength[0] = childLen + 1;

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

    // Copy principal variation from this iteration's PV table.
    result.pvLength = state.pvLength[0];
    if (result.pvLength < 0) result.pvLength = 0;
    if (result.pvLength > MAX_PLY) result.pvLength = MAX_PLY;
    std::memcpy(result.pv, state.pv[0], result.pvLength * sizeof(Move));

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
