#include "attacks.h"
#include "evaluation.h"

namespace LibreChess {
namespace attacks {

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Table storage
// ---------------------------------------------------------------------------

Bitboard KNIGHT[64] = {};
Bitboard KING[64] = {};
Bitboard PAWN[2][64] = {};

// Diagonal masks (a1-h8 direction), one per diagonal.
// Index: rank - file + 7 (range 0–14).
static Bitboard DIAG[15];

// Anti-diagonal masks (h1-a8 direction), one per anti-diagonal.
// Index: rank + file (range 0–14).
static Bitboard ANTI_DIAG[15];

// First-rank attack table: for each file (0-7) and each 6-bit inner
// occupancy pattern (bits 1-6 of the rank), stores the 8-bit attack mask
// on that rank. Used for O(1) rank attack lookup.
static uint8_t FIRST_RANK_ATTACKS[8][64];

// ---------------------------------------------------------------------------
// Table initialization
// ---------------------------------------------------------------------------

// Knight offsets as (file_delta, rank_delta) pairs.
// A knight moves in an L-shape: 2 squares in one axis, 1 in the other.
static void initKnightAttacks() {
  for (Square sq = 0; sq < 64; ++sq) {
    Bitboard bb = squareBB(sq);
    Bitboard attacks = 0;

    // Two north, one east/west
    attacks |= (bb & ~FILE_H) << 17;            // NNE
    attacks |= (bb & ~FILE_A) << 15;            // NNW
    // Two south, one east/west
    attacks |= (bb & ~FILE_H) >> 15;            // SSE
    attacks |= (bb & ~FILE_A) >> 17;            // SSW
    // Two east, one north/south
    attacks |= (bb & ~FILE_G & ~FILE_H) << 10;  // ENE
    attacks |= (bb & ~FILE_G & ~FILE_H) >> 6;   // ESE
    // Two west, one north/south
    attacks |= (bb & ~FILE_A & ~FILE_B) << 6;   // WNW
    attacks |= (bb & ~FILE_A & ~FILE_B) >> 10;  // WSW

    KNIGHT[sq] = attacks;
  }
}

// King can step one square in any of the 8 compass directions.
static void initKingAttacks() {
  for (Square sq = 0; sq < 64; ++sq) {
    Bitboard bb = squareBB(sq);
    Bitboard attacks = shiftNorth(bb) | shiftSouth(bb)
                     | shiftEast(bb)  | shiftWest(bb)
                     | shiftNE(bb)    | shiftNW(bb)
                     | shiftSE(bb)    | shiftSW(bb);
    KING[sq] = attacks;
  }
}

// Pawn attacks are the diagonal capture squares (not pushes).
// White pawns attack NE and NW; black pawns attack SE and SW.
static void initPawnAttacks() {
  for (Square sq = 0; sq < 64; ++sq) {
    Bitboard bb = squareBB(sq);
    PAWN[0][sq] = shiftNE(bb) | shiftNW(bb);  // WHITE
    PAWN[1][sq] = shiftSE(bb) | shiftSW(bb);  // BLACK
  }
}

// ---------------------------------------------------------------------------
// Byte swap for Hyperbola Quintessence
// ---------------------------------------------------------------------------
// Reverses byte order of a 64-bit value, which mirrors rank order on the
// board. Used to compute negative-direction slider attacks by applying the
// positive-direction subtraction trick on the reversed occupancy.

static inline uint64_t byteSwap64(uint64_t v) {
  return __builtin_bswap64(v);
}

// ---------------------------------------------------------------------------
// Hyperbola Quintessence — O(1) line attacks (file/diagonal/anti-diagonal)
// ---------------------------------------------------------------------------

static Bitboard lineHQ(Bitboard occ, Square sq, Bitboard mask) {
  Bitboard piece = squareBB(sq);
  Bitboard maskEx = mask ^ piece;
  Bitboard forward = occ & maskEx;
  Bitboard reverse = byteSwap64(forward);
  forward -= 2 * piece;
  reverse -= 2 * byteSwap64(piece);
  forward ^= byteSwap64(reverse);
  return forward & maskEx;
}

// ---------------------------------------------------------------------------
// Diagonal/anti-diagonal mask initialization
// ---------------------------------------------------------------------------

static void initDiagMasks() {
  for (int d = 0; d < 15; ++d) {
    Bitboard diag = 0, adiag = 0;
    for (int r = 0; r < 8; ++r) {
      int df = r - d + 7;   // file for diagonal d at rank r
      int af = d - r;       // file for anti-diagonal d at rank r
      if (df >= 0 && df < 8) diag  |= squareBB(r * 8 + df);
      if (af >= 0 && af < 8) adiag |= squareBB(r * 8 + af);
    }
    DIAG[d] = diag;
    ANTI_DIAG[d] = adiag;
  }
}

// ---------------------------------------------------------------------------
// First-rank attack table initialization
// ---------------------------------------------------------------------------

static void initFirstRankAttacks() {
  for (int file = 0; file < 8; ++file) {
    for (int occ6 = 0; occ6 < 64; ++occ6) {
      uint8_t occ = occ6 << 1;
      uint8_t result = 0;
      for (int f = file + 1; f < 8; ++f) {
        result |= static_cast<uint8_t>(1 << f);
        if (occ & (1 << f)) break;
      }
      for (int f = file - 1; f >= 0; --f) {
        result |= static_cast<uint8_t>(1 << f);
        if (occ & (1 << f)) break;
      }
      FIRST_RANK_ATTACKS[file][occ6] = result;
    }
  }
}

// ---------------------------------------------------------------------------
// Master initialization
// ---------------------------------------------------------------------------

void init() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  initKnightAttacks();
  initKingAttacks();
  initPawnAttacks();
  initDiagMasks();
  initFirstRankAttacks();
}

// ---------------------------------------------------------------------------
// Slider attack functions
// ---------------------------------------------------------------------------

Bitboard rook(Square sq, Bitboard occupied) {
  int rank = rankOf(sq);
  int file = fileOf(sq);

  uint8_t occ6 = static_cast<uint8_t>((occupied >> (rank * 8 + 1)) & 0x3F);
  Bitboard attacks = static_cast<Bitboard>(FIRST_RANK_ATTACKS[file][occ6])
                     << (rank * 8);
  attacks |= lineHQ(occupied, sq, FILE_A << file);
  return attacks;
}

Bitboard bishop(Square sq, Bitboard occupied) {
  int rank = rankOf(sq), file = fileOf(sq);
  return lineHQ(occupied, sq, DIAG[rank - file + 7])
       | lineHQ(occupied, sq, ANTI_DIAG[rank + file]);
}

// ---------------------------------------------------------------------------
// X-ray attack functions
// ---------------------------------------------------------------------------

Bitboard xrayRook(Bitboard occupied, Bitboard friendly, Square sq) {
  Bitboard atk = rook(sq, occupied);
  Bitboard blockers = atk & friendly;
  return rook(sq, occupied ^ blockers);
}

Bitboard xrayBishop(Bitboard occupied, Bitboard friendly, Square sq) {
  Bitboard atk = bishop(sq, occupied);
  Bitboard blockers = atk & friendly;
  return bishop(sq, occupied ^ blockers);
}

// ---------------------------------------------------------------------------
// Ray geometry functions
// ---------------------------------------------------------------------------

Bitboard between(Square s1, Square s2) {
  int r1 = rankOf(s1), f1 = fileOf(s1);
  int r2 = rankOf(s2), f2 = fileOf(s2);
  int dr = r2 - r1, df = f2 - f1;

  if (dr != 0 && df != 0 && (dr < 0 ? -dr : dr) != (df < 0 ? -df : df))
    return 0;

  Bitboard b1 = squareBB(s1), b2 = squareBB(s2);
  if (dr == 0 || df == 0)
    return rook(s1, b2) & rook(s2, b1);
  return bishop(s1, b2) & bishop(s2, b1);
}

// ---------------------------------------------------------------------------
// Attacked-by bitboard computation
// ---------------------------------------------------------------------------

AttackInfo computeAll(const BitboardSet& bb) {
  using namespace LibreChess::piece;

  AttackInfo info = {};

  for (int c = 0; c < 2; ++c) {
    Color color = static_cast<Color>(c);

    // --- Pawns (bulk shift, no per-square loop) ---
    Bitboard pawns = bb.byPiece[pieceIndex(makePiece(color, PieceType::PAWN))];
    if (c == 0) {
      info.byPiece[c][raw(PieceType::PAWN)] = shiftNE(pawns) | shiftNW(pawns);
    } else {
      info.byPiece[c][raw(PieceType::PAWN)] = shiftSE(pawns) | shiftSW(pawns);
    }

    // --- Knights (table lookup per square) ---
    Bitboard knights = bb.byPiece[pieceIndex(makePiece(color, PieceType::KNIGHT))];
    Bitboard knightAtk = 0;
    Bitboard tmp = knights;
    while (tmp) { knightAtk |= KNIGHT[popLsb(tmp)]; }
    info.byPiece[c][raw(PieceType::KNIGHT)] = knightAtk;

    // --- Bishops ---
    Bitboard bishops = bb.byPiece[pieceIndex(makePiece(color, PieceType::BISHOP))];
    Bitboard bishopAtk = 0;
    tmp = bishops;
    while (tmp) { bishopAtk |= bishop(popLsb(tmp), bb.occupied); }
    info.byPiece[c][raw(PieceType::BISHOP)] = bishopAtk;

    // --- Rooks ---
    Bitboard rooks = bb.byPiece[pieceIndex(makePiece(color, PieceType::ROOK))];
    Bitboard rookAtk = 0;
    tmp = rooks;
    while (tmp) { rookAtk |= rook(popLsb(tmp), bb.occupied); }
    info.byPiece[c][raw(PieceType::ROOK)] = rookAtk;

    // --- Queens ---
    Bitboard queens = bb.byPiece[pieceIndex(makePiece(color, PieceType::QUEEN))];
    Bitboard queenAtk = 0;
    tmp = queens;
    while (tmp) { queenAtk |= queen(popLsb(tmp), bb.occupied); }
    info.byPiece[c][raw(PieceType::QUEEN)] = queenAtk;

    // --- King ---
    Bitboard king = bb.byPiece[pieceIndex(makePiece(color, PieceType::KING))];
    info.byPiece[c][raw(PieceType::KING)] = king ? KING[lsb(king)] : 0;

    // --- Color union ---
    info.byColor[c] = info.byPiece[c][raw(PieceType::PAWN)]
                     | info.byPiece[c][raw(PieceType::KNIGHT)]
                     | info.byPiece[c][raw(PieceType::BISHOP)]
                     | info.byPiece[c][raw(PieceType::ROOK)]
                     | info.byPiece[c][raw(PieceType::QUEEN)]
                     | info.byPiece[c][raw(PieceType::KING)];
  }

  info.allAttacks = info.byColor[0] | info.byColor[1];
  return info;
}

// ---------------------------------------------------------------------------
// Square attack detection
// ---------------------------------------------------------------------------

Bitboard attackersOfSquare(const BitboardSet& bb, Square sq,
                          Color attackingColor) {
  Color defending = ~attackingColor;
  Bitboard attackers = 0;

  // Zobrist indices are contiguous per color: white 0-5, black 6-11.
  // base + offset replaces 6× makePiece + pieceIndex calls.
  int base = piece::raw(attackingColor) * 6;

  attackers |= PAWN[piece::raw(defending)][sq] & bb.byPiece[base + 0];
  attackers |= KNIGHT[sq] & bb.byPiece[base + 1];
  attackers |= KING[sq] & bb.byPiece[base + 5];

  Bitboard rookQueens   = bb.byPiece[base + 3] | bb.byPiece[base + 4];
  Bitboard bishopQueens = bb.byPiece[base + 2] | bb.byPiece[base + 4];
  attackers |= rook(sq, bb.occupied)   & rookQueens;
  attackers |= bishop(sq, bb.occupied) & bishopQueens;

  return attackers;
}

bool isSquareUnderAttack(const BitboardSet& bb, Square sq, Color defendingColor) {
  return attackersOfSquare(bb, sq, ~defendingColor) != 0;
}

// ---------------------------------------------------------------------------
// Static Exchange Evaluation (SEE) — swap algorithm.
//
// Simulates a sequence of captures on the target square of the given move.
// Each side recaptures with its least valuable attacker until one side has
// no attackers left or chooses to stop (negamax walk-back).
//
// Material values in centipawns, indexed by PieceType (NONE=0 .. KING=6).
// King value is set high (20000) so "capturing a king" always wins — this
// handles positions where the king itself is an attacker (legal only on
// the last capture).
//
// Reference: https://www.chessprogramming.org/Static_Exchange_Evaluation
// ---------------------------------------------------------------------------

// SEE piece values: delegates to eval::materialValue() for consistency with
// the evaluation function.  King uses a large sentinel so "capturing a king"
// always wins — this handles positions where the king itself is an attacker
// (legal only on the last capture).
static constexpr int SEE_KING_VALUE = 20000;

static int seeValue(PieceType pt) {
  if (pt == PieceType::KING) return SEE_KING_VALUE;
  return eval::materialValue(pt);
}

// Find the least valuable attacker of `sq` for `color`.  Returns the
// PieceType and sets `attackerBB` to the single-bit bitboard of the
// chosen attacker (for removal from occupancy).
// Returns PieceType::NONE if no attacker exists.
static PieceType leastValuableAttacker(const BitboardSet& bb,
                                       Bitboard occupied,
                                       Square sq, Color color,
                                       Bitboard& attackerBB) {
  // Pawns
  int idx = piece::pieceIndex(piece::makePiece(color, PieceType::PAWN));
  // PAWN[~color] gives squares from which a pawn of `color` attacks `sq`.
  Bitboard attackers = PAWN[piece::raw(~color)][sq] & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::PAWN; }

  // Knights
  idx = piece::pieceIndex(piece::makePiece(color, PieceType::KNIGHT));
  attackers = KNIGHT[sq] & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::KNIGHT; }

  // Bishops
  idx = piece::pieceIndex(piece::makePiece(color, PieceType::BISHOP));
  attackers = bishop(sq, occupied) & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::BISHOP; }

  // Rooks
  idx = piece::pieceIndex(piece::makePiece(color, PieceType::ROOK));
  attackers = rook(sq, occupied) & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::ROOK; }

  // Queens
  idx = piece::pieceIndex(piece::makePiece(color, PieceType::QUEEN));
  attackers = queen(sq, occupied) & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::QUEEN; }

  // King
  idx = piece::pieceIndex(piece::makePiece(color, PieceType::KING));
  attackers = KING[sq] & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::KING; }

  return PieceType::NONE;
}

// ---------------------------------------------------------------------------
// SEE gain walkback — propagates the negamax minimax through the gain list.
//
// At each depth level, the capturing side chooses the better of recapturing
// (gain[d]) vs standing pat (-gain[d-1]).  Walking back from the deepest
// exchange resolves the final value at gain[0].
//
// This is the "swap algorithm" described in CPW's SEE article.
//
// Reference: https://www.chessprogramming.org/SEE_-_The_Swap_Algorithm
// ---------------------------------------------------------------------------
static void seeWalkBack(int gain[], int depth) {
  while (depth > 0) {
    int standPat = -gain[depth - 1];
    gain[depth - 1] = -(standPat > gain[depth] ? standPat : gain[depth]);
    --depth;
  }
}

int see(const BitboardSet& bb, const Piece mailbox[], Move m) {
  Square target = m.to;

  // Determine the initial captured piece value.
  PieceType captured;
  if (m.isEP()) {
    captured = PieceType::PAWN;
  } else {
    captured = piece::pieceType(mailbox[target]);
  }

  // The attacker is the piece on the 'from' square.
  PieceType attacker = piece::pieceType(mailbox[m.from]);
  Color side = piece::pieceColor(mailbox[m.from]);

  // Gain list: gain[0] = value of the initial capture.
  // Each subsequent entry is the value of the recapture.
  int gain[32];
  int d = 0;
  gain[d] = seeValue(captured);

  // Remove the initial attacker from occupancy.
  Bitboard occupied = bb.occupied;
  occupied ^= squareBB(m.from);
  // For EP, also remove the captured pawn from occupancy.
  if (m.isEP()) {
    // EP captured pawn is on the same file as target, one rank behind.
    int epPawnSq = (side == Color::WHITE)
                       ? target - 8   // white captures: pawn is one rank below
                       : target + 8;  // black captures: pawn is one rank above
    occupied ^= squareBB(epPawnSq);
  }

  // The piece now "on" the target square is the initial attacker.
  PieceType onTarget = attacker;

  // Alternate sides, each recapturing with least valuable attacker.
  side = ~side;
  Bitboard attackerBB;

  while (true) {
    PieceType nextAttacker = leastValuableAttacker(bb, occupied, target,
                                                   side, attackerBB);
    if (nextAttacker == PieceType::NONE) break;  // no more attackers

    ++d;
    // The gain is the value of the piece currently on the target square,
    // minus what was lost previously (negamax convention).
    gain[d] = seeValue(onTarget) - gain[d - 1];

    // If the gain is already so negative that even capturing can't help,
    // we can prune: the side to move would choose not to recapture.
    // But we must be careful: this is an optimization, not strictly needed.
    // For correctness we continue the loop.

    // Remove the recapturing piece from occupancy.
    occupied ^= attackerBB;
    onTarget = nextAttacker;
    side = ~side;
  }

  // Walk back the gain list using negamax minimax.
  seeWalkBack(gain, d);

  return gain[0];
}

}  // namespace attacks
}  // namespace LibreChess
