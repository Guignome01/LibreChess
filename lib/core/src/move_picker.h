#ifndef LIBRECHESS_MOVE_PICKER_H
#define LIBRECHESS_MOVE_PICKER_H

// ---------------------------------------------------------------------------
// MovePicker — staged move generation and heuristic update helpers.
//
// Extracted from search.cpp to separate move ordering concerns from the
// search algorithm itself.  Header-only for inlining critical hot paths
// (MovePicker::next() is called once per move in negamax).
//
// Provides:
//   - MVV-LVA scoring
//   - Staged MovePicker (TT → good captures → killers → countermove →
//     quiets → bad captures)
//   - Heuristic update functions (killers, history gravity, capture
//     history, countermove)
//
// References:
//   https://www.chessprogramming.org/Move_Ordering
//   https://www.chessprogramming.org/Move_Ordering#Staged_Move_Generation
//   https://www.chessprogramming.org/MVV-LVA
//   https://www.chessprogramming.org/Killer_Move
//   https://www.chessprogramming.org/History_Heuristic
//   https://www.chessprogramming.org/Static_Exchange_Evaluation
//   https://www.chessprogramming.org/Countermove_Heuristic
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "attacks.h"
#include "movegen.h"
#include "piece.h"
#include "search.h"
#include "utils.h"

namespace LibreChess {
namespace search {

using namespace piece;

// ===========================================================================
// MVV-LVA scoring
// ===========================================================================

// Simple piece value for MVV-LVA (indexed by PieceType).
// PieceType: NONE=0, PAWN=1, KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5, KING=6.
constexpr int MVV_LVA_VALUE[] = {0, 1, 3, 3, 5, 9, 0};

// Compute MVV-LVA (Most Valuable Victim – Least Valuable Aggressor) score
// for a capture.  Higher scores indicate better trades (valuable victim
// taken by a cheap attacker).  The ×16 scaling leaves room for secondary
// ordering signals (e.g. capture history) to break ties.
//
// Reference: https://www.chessprogramming.org/MVV-LVA
inline int scoreMVVLVA(PieceType victim, PieceType attacker) {
  return MVV_LVA_VALUE[raw(victim)] * 16 - MVV_LVA_VALUE[raw(attacker)];
}

// Sentinel SEE value — move has no precomputed SEE.
constexpr int SEE_NOT_COMPUTED = -32000;

// ===========================================================================
// Move validation + flag reconstruction
// ===========================================================================

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
// pieceIndex of -1 and a PST array-out-of-bounds access.
//
// Fix: after confirming from→to is pseudo-legal, **reconstruct** the
// flags from the current position so make() always sees correct metadata.
// The promotion-type index (bits 4-5) is preserved from the stored move
// since it encodes the engine's deliberate choice.
//
// Reference: https://www.chessprogramming.org/Transposition_Table#Move_from_another_Position
// ---------------------------------------------------------------------------

inline bool isMoveValid(const BitboardSet& bb, const Piece mailbox[],
                        Move& m, const PositionState& state, Color side) {
  // Reject moves where the piece doesn't belong to the current side.
  Piece piece = mailbox[m.from];
  if (piece == Piece::NONE || pieceColor(piece) != side)
    return false;

  // Step 1: validate from→to is legal (pseudo-legal + does not leave
  //         own king in check).
  if (!movegen::isValidMove(bb, mailbox, m.from, m.to, state))
    return false;

  // Step 2: reconstruct flags from the current position.
  PieceType pt = pieceType(piece);

  uint8_t flags = 0;

  // Capture: destination has an opponent piece.
  if (mailbox[m.to] != Piece::NONE)
    flags |= MOVE_CAPTURE;

  // En passant: pawn moving diagonally to the EP target square.
  if (pt == PieceType::PAWN && state.epSquare != SQ_NONE &&
      m.to == state.epSquare)
    flags |= MOVE_EP | MOVE_CAPTURE;

  // Castling: king sliding two files.
  if (pt == PieceType::KING && abs(fileOf(m.from) - fileOf(m.to)) == 2)
    flags |= MOVE_CASTLING;

  // Promotion: pawn reaching the back rank; preserve promo-type choice.
  if (pt == PieceType::PAWN && rankOf(m.to) == promotionRank(side))
    flags |= MOVE_PROMOTION | (m.flags & (0x03 << MOVE_PROMO_SHIFT));

  m.flags = flags;
  return true;
}

// ===========================================================================
// Selection sort helpers
// ===========================================================================

// Partial selection sort: find the best-scored move in [start, end) and
// swap it to position `start`.
inline void pickBestInRange(Move moves[], int16_t scores[], int start,
                            int end) {
  int bestIdx = start;
  for (int i = start + 1; i < end; ++i) {
    if (scores[i] > scores[bestIdx]) bestIdx = i;
  }
  if (bestIdx != start) {
    Move tmpM = moves[start];
    moves[start] = moves[bestIdx];
    moves[bestIdx] = tmpM;
    int16_t tmpS = scores[start];
    scores[start] = scores[bestIdx];
    scores[bestIdx] = tmpS;
  }
}

// Convenience: pick best in [start, moves.count).
template <int N>
inline void pickBest(MoveListBase<N>& moves, int16_t scores[], int start) {
  pickBestInRange(moves.moves, scores, start, moves.count);
}

// ===========================================================================
// Safe access helpers for heuristic tables indexed by pieceIndex.
//
// Centralise the bounds check (isValidPieceIndex + victim range) so that
// every callsite uses the same logic.  Returns 0 / no-op on invalid index.
// ===========================================================================

// Read a captureHistory score with bounds validation.
inline int16_t safeCaptureHistScore(const SearchState& ss,
                                    PieceType attacker,
                                    PieceType victim, int toSq) {
  int ai = raw(attacker) - 1;
  int vi = raw(victim) - 1;
  if (ai >= 0 && ai < 6 && vi >= 0 && vi < 6)
    return ss.captureHistory[ai][vi][toSq];
  return 0;
}

// Read a countermove entry with bounds validation.
inline PackedMove safeCountermove(const SearchState& ss, int pieceIdx,
                                  int toSq) {
  if (isValidPieceIndex(pieceIdx))
    return ss.countermoves[pieceIdx][toSq];
  return 0;
}

// ===========================================================================
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
// ===========================================================================

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
  //   [0, goodCapEnd)             — candidate good captures (SEE computed lazily)
  //   [goodCapEnd, totalCaps)     — confirmed bad captures (SEE < 0, deferred)
  //   [totalCaps, moves.count)    — quiet moves
  //
  // SEE is computed lazily when a capture is about to be yielded in
  // GOOD_CAPTURES stage.  If SEE < 0, the capture is swapped to the bad
  // section and goodCapEnd shrinks.  This avoids computing SEE for
  // captures never examined due to early beta cutoff.
  //
  // Score array uses int16_t (range ±32767).  All stored values fit:
  // MVV-LVA ≤ ~600, history ≤ ±7000, SEE_NOT_COMPUTED = -32000.
  //
  // Reference: https://www.chessprogramming.org/Move_Ordering
  MoveList moves;
  int16_t scores[MAX_MOVES];
  int totalCaps;     // total captures generated
  int goodCapEnd;    // right boundary of candidate good captures
  int capIdx;        // next good capture index
  int badCapIdx;     // next bad capture index
  int quietIdx;      // next quiet move index

  // Legality context built once per position (in INIT_CAPTURES stage).
  // Reused by INIT_QUIETS to avoid recomputing pin/check masks.
  movegen::LegalityContext legalCtx;

  // SEE of the last move returned (SEE_NOT_COMPUTED for non-captures).
  int16_t lastSee;

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
    hasTT  = !tt.isNull();

    killer0 = unpackMove(ssRef.killers[p][0]);
    killer1 = unpackMove(ssRef.killers[p][1]);

    hasCounter = false;
    if (!isEmpty(prevPiece)) {
      int idx = pieceIndex(prevPiece);
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
    goodCapEnd   = 0;
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

        // --- Good captures (SEE >= 0, computed lazily) -----------------
        //
        // Picks best-scored capture, then computes SEE on demand.  If
        // SEE < 0 the move is swapped to the bad-capture section and
        // goodCapEnd shrinks, avoiding wasted SEE for moves never
        // examined due to early beta cutoff.
        //
        // Reference: https://www.chessprogramming.org/Static_Exchange_Evaluation
        case STAGE_GOOD_CAPTURES:
          while (capIdx < goodCapEnd) {
            pickBestInRange(moves.moves, scores, capIdx, goodCapEnd);
            Move m = moves.moves[capIdx];
            if (ttYielded && m == ttMove) { ++capIdx; continue; }
            int see = attacks::see(*bb, mailbox, m);
            if (see < 0) {
              // Reclassify as bad capture: swap to end of good section.
              // Cache the SEE result in scores[] (overwriting the MVV-LVA
              // score that is no longer needed for ordering).  The cached
              // value is read back in BAD_CAPTURES, avoiding a redundant
              // SEE recomputation.
              --goodCapEnd;
              if (capIdx != goodCapEnd) {
                std::swap(moves.moves[capIdx], moves.moves[goodCapEnd]);
                std::swap(scores[capIdx], scores[goodCapEnd]);
              }
              scores[goodCapEnd] = static_cast<int16_t>(see);
              continue;  // re-examine capIdx (now holds a different move)
            }
            ++capIdx;
            lastSee = static_cast<int16_t>(see);
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
            if (km.isNull()) continue;
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
                !cm.isNull() && // not null
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
          badCapIdx = goodCapEnd;
          stage = STAGE_BAD_CAPTURES;
          continue;

        // --- Bad captures (SEE < 0, deferred from GOOD_CAPTURES) ------
        // SEE values were cached in scores[] during GOOD_CAPTURES
        // reclassification — read them back instead of recomputing.
        // Bad captures are now ordered by SEE (least-negative first),
        // which is better ordering for the recapture-extension consumer.
        case STAGE_BAD_CAPTURES:
          while (badCapIdx < totalCaps) {
            pickBestInRange(moves.moves, scores, badCapIdx, totalCaps);
            Move m = moves.moves[badCapIdx];
            lastSee = scores[badCapIdx];
            ++badCapIdx;
            if (ttYielded && m == ttMove) continue;
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
  // Generate captures, score with MVV-LVA + captureHistory.  SEE is
  // deferred to GOOD_CAPTURES stage (lazy evaluation) — saves expensive
  // swap-algorithm calls for captures never examined due to beta cutoff.
  //
  // Reference: https://www.chessprogramming.org/MVV-LVA
  // -----------------------------------------------------------------------

  void initCaptures() {
    // Build the legality context once — reused in initQuiets().
    Square kingSq = 0;
    utils::resolveKingSquare(*bb, side, kingSq);
    legalCtx = movegen::buildLegalityContext(*bb, side, kingSq);

    movegen::generateMoves(*bb, mailbox, side, *posState, legalCtx, moves,
                            movegen::FilterMode::CAPTURES_PROMOS);
    totalCaps = moves.count;

    // Score all captures uniformly with MVV-LVA + captureHistory.
    // SEE is deferred to GOOD_CAPTURES stage (lazy evaluation).
    for (int i = 0; i < totalCaps; ++i) {
      const Move& m = moves.moves[i];
      PieceType victim   = m.isEP() ? PieceType::PAWN
                                    : pieceType(mailbox[m.to]);
      PieceType attacker = pieceType(mailbox[m.from]);
      int mvvlva   = scoreMVVLVA(victim, attacker);
      int capHist  = safeCaptureHistScore(*ss, attacker, victim, m.to);
      scores[i] = static_cast<int16_t>(mvvlva + capHist / 16);
    }

    // All captures are candidate good captures initially; bad ones are
    // reclassified lazily when SEE is computed in GOOD_CAPTURES stage.
    goodCapEnd = totalCaps;
  }

  // -----------------------------------------------------------------------
  // Generate quiet moves using the pre-built LegalityContext from
  // initCaptures.  Appended after the captures section.
  // Score quiets with the history heuristic.
  // -----------------------------------------------------------------------

  void initQuiets() {
    int beforeCount = moves.count;
    movegen::generateMovesAppend(*bb, mailbox, side, *posState, legalCtx,
                                  moves, movegen::FilterMode::QUIETS);

    uint8_t c = raw(side);
    for (int i = beforeCount; i < moves.count; ++i) {
      const Move& m = moves.moves[i];
      scores[i] = ss->history[c][raw(pieceType(mailbox[m.from])) - 1][m.to];
    }
    quietIdx = totalCaps;
  }
};

// ===========================================================================
// Heuristic update functions
// ===========================================================================

// Update killer moves: slot the new killer into position 0, shifting the
// old one to position 1.  Avoids duplicates.
inline void updateKillers(Move m, int ply, SearchState& state) {
  PackedMove pm = packMove(m);
  if (pm != state.killers[ply][0]) {
    state.killers[ply][1] = state.killers[ply][0];
    state.killers[ply][0] = pm;
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
constexpr int HISTORY_MAX = 7000;

inline void updateHistory(int16_t& h, int bonus) {
  h += bonus - h * (bonus < 0 ? -bonus : bonus) / HISTORY_MAX;
}

// ---------------------------------------------------------------------------
// Update capture history on a beta cutoff by a capture move.
//
// Rewards the cutoff capture and penalizes all previously searched captures
// that failed to cause a cutoff.  The board is in pre-move state (unmake
// already called), so mailbox[to] still holds the victim.
//
// Reference: https://www.chessprogramming.org/History_Heuristic
// Reference: https://www.chessprogramming.org/Relative_History_Heuristic
// ---------------------------------------------------------------------------

inline void updateCaptureCutoffHistory(
    const Move& m, const Position& pos, SearchState& state, int bonus,
    const PackedMove* capturesSearched, int captureCount) {
  int ai = raw(pieceType(pos.mailbox()[m.from])) - 1;
  PieceType victim = m.isEP() ? PieceType::PAWN
                               : pieceType(pos.mailbox()[m.to]);
  int vi = raw(victim) - 1;
  if (ai >= 0 && ai < 6 && vi >= 0 && vi < 6)
    updateHistory(state.captureHistory[ai][vi][m.to], bonus);
  for (int ci = 0; ci < captureCount - 1; ++ci) {
    const Move cm = unpackMove(capturesSearched[ci]);
    int pai = raw(pieceType(pos.mailbox()[cm.from])) - 1;
    PieceType prevVictim = cm.isEP() ? PieceType::PAWN
                                     : pieceType(pos.mailbox()[cm.to]);
    int pvi = raw(prevVictim) - 1;
    if (pai >= 0 && pai < 6 && pvi >= 0 && pvi < 6)
      updateHistory(state.captureHistory[pai][pvi][cm.to], -bonus);
  }
}

// ---------------------------------------------------------------------------
// Update quiet-move ordering heuristics on a beta cutoff.
//
// Three mechanisms work together to improve move ordering:
//   1. Killer moves — two-slot cache of recent cutoff moves per ply.
//   2. History gravity — additive bonus for the cutoff move, symmetric
//      penalties for non-cutoff moves already searched.
//   3. Countermove heuristic — paired with the opponent's previous move.
//
// Reference: https://www.chessprogramming.org/Killer_Move
// Reference: https://www.chessprogramming.org/History_Heuristic
// Reference: https://www.chessprogramming.org/Countermove_Heuristic
// ---------------------------------------------------------------------------

inline void updateQuietCutoffHeuristics(
    const Move& m, const Position& pos, SearchState& state, int ply,
    int bonus, const PackedMove* quietsSearched, int quietCount,
    Piece prevPiece, int prevTo) {
  updateKillers(m, ply, state);
  updateHistory(state.history[raw(pos.sideToMove())][raw(pieceType(pos.mailbox()[m.from])) - 1][m.to], bonus);

  // History gravity: penalize previously searched quiet moves that
  // failed to cause a cutoff.  The cutoff move itself (last entry)
  // is excluded — it received the bonus above.
  Color side = pos.sideToMove();
  for (int q = 0; q < quietCount - 1; ++q) {
    Move qm = unpackMove(quietsSearched[q]);
    updateHistory(state.history[raw(side)][raw(pieceType(pos.mailbox()[qm.from])) - 1][qm.to], -bonus);
  }

  // Store as countermove for the opponent's previous (piece, toSq).
  if (!isEmpty(prevPiece)) {
    int idx = pieceIndex(prevPiece);
    if (isValidPieceIndex(idx))
      state.countermoves[idx][prevTo] = packMove(m);
  }
}

}  // namespace search
}  // namespace LibreChess

#endif  // LIBRECHESS_MOVE_PICKER_H
