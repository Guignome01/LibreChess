#include "attacks.h"
#include "evaluation.h"

namespace LibreChess {
namespace attacks {

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Constexpr leaper table builders
// ---------------------------------------------------------------------------

static constexpr Table64 buildKnight() {
  Table64 t{};
  for (Square sq = 0; sq < 64; ++sq) {
    Bitboard bb = squareBB(sq);
    Bitboard a = 0;
    a |= (bb & ~FILE_H) << 17;            // NNE
    a |= (bb & ~FILE_A) << 15;            // NNW
    a |= (bb & ~FILE_H) >> 15;            // SSE
    a |= (bb & ~FILE_A) >> 17;            // SSW
    a |= (bb & ~FILE_G & ~FILE_H) << 10;  // ENE
    a |= (bb & ~FILE_G & ~FILE_H) >> 6;   // ESE
    a |= (bb & ~FILE_A & ~FILE_B) << 6;   // WNW
    a |= (bb & ~FILE_A & ~FILE_B) >> 10;  // WSW
    t.data[sq] = a;
  }
  return t;
}

static constexpr Table64 buildKing() {
  Table64 t{};
  for (Square sq = 0; sq < 64; ++sq) {
    Bitboard bb = squareBB(sq);
    t.data[sq] = shiftNorth(bb) | shiftSouth(bb)
               | shiftEast(bb)  | shiftWest(bb)
               | shiftNE(bb)    | shiftNW(bb)
               | shiftSE(bb)    | shiftSW(bb);
  }
  return t;
}

static constexpr PawnTable buildPawn() {
  PawnTable t{};
  for (Square sq = 0; sq < 64; ++sq) {
    Bitboard bb = squareBB(sq);
    t.data[0][sq] = shiftNE(bb) | shiftNW(bb);  // WHITE
    t.data[1][sq] = shiftSE(bb) | shiftSW(bb);  // BLACK
  }
  return t;
}

// ---------------------------------------------------------------------------
// Leaper table definitions — const, placed in .rodata (Flash on ESP32).
// Initialized at compile time via constexpr builders.
// ---------------------------------------------------------------------------

const Table64   KNIGHT = buildKnight();
const Table64   KING   = buildKing();
const PawnTable PAWN   = buildPawn();

// ---------------------------------------------------------------------------
// Constexpr slider mask/table builders (file-scope, used by rook/bishop)
// ---------------------------------------------------------------------------

struct DiagTables {
  Bitboard diag[15] = {};
  Bitboard antiDiag[15] = {};
  constexpr DiagTables() {
    for (int d = 0; d < 15; ++d) {
      Bitboard dm = 0, am = 0;
      for (int r = 0; r < 8; ++r) {
        int df = r - d + 7;
        int af = d - r;
        if (df >= 0 && df < 8) dm |= squareBB(r * 8 + df);
        if (af >= 0 && af < 8) am |= squareBB(r * 8 + af);
      }
      diag[d] = dm;
      antiDiag[d] = am;
    }
  }
};

struct FirstRankTable {
  uint8_t data[8][64] = {};
  constexpr FirstRankTable() {
    for (int file = 0; file < 8; ++file) {
      for (int occ6 = 0; occ6 < 64; ++occ6) {
        uint8_t occ = static_cast<uint8_t>(occ6 << 1);
        uint8_t result = 0;
        for (int f = file + 1; f < 8; ++f) {
          result |= static_cast<uint8_t>(1 << f);
          if (occ & (1 << f)) break;
        }
        for (int f = file - 1; f >= 0; --f) {
          result |= static_cast<uint8_t>(1 << f);
          if (occ & (1 << f)) break;
        }
        data[file][occ6] = result;
      }
    }
  }
};

static constexpr DiagTables     DIAG_TABLES{};
static constexpr FirstRankTable FIRST_RANK_TABLE{};

// Convenience aliases
static constexpr auto& DIAG           = DIAG_TABLES.diag;
static constexpr auto& ANTI_DIAG      = DIAG_TABLES.antiDiag;
static constexpr auto& FIRST_RANK_ATTACKS = FIRST_RANK_TABLE.data;

// ---------------------------------------------------------------------------
// Byte swap for Hyperbola Quintessence

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
    Bitboard pawns = bb.byPiece[pieceIndex(color, PieceType::PAWN)];
    if (c == 0) {
      info.byPiece[c][raw(PieceType::PAWN)] = shiftNE(pawns) | shiftNW(pawns);
    } else {
      info.byPiece[c][raw(PieceType::PAWN)] = shiftSE(pawns) | shiftSW(pawns);
    }

    // --- Knights (table lookup per square) ---
    Bitboard knightAtk = 0;
    Bitboard tmp = bb.byPiece[pieceIndex(color, PieceType::KNIGHT)];
    while (tmp) { knightAtk |= KNIGHT[popLsb(tmp)]; }
    info.byPiece[c][raw(PieceType::KNIGHT)] = knightAtk;

    // --- Bishops ---
    Bitboard bishopAtk = 0;
    tmp = bb.byPiece[pieceIndex(color, PieceType::BISHOP)];
    while (tmp) { bishopAtk |= bishop(popLsb(tmp), bb.occupied); }
    info.byPiece[c][raw(PieceType::BISHOP)] = bishopAtk;

    // --- Rooks ---
    Bitboard rookAtk = 0;
    tmp = bb.byPiece[pieceIndex(color, PieceType::ROOK)];
    while (tmp) { rookAtk |= rook(popLsb(tmp), bb.occupied); }
    info.byPiece[c][raw(PieceType::ROOK)] = rookAtk;

    // --- Queens ---
    Bitboard queenAtk = 0;
    tmp = bb.byPiece[pieceIndex(color, PieceType::QUEEN)];
    while (tmp) { queenAtk |= queen(popLsb(tmp), bb.occupied); }
    info.byPiece[c][raw(PieceType::QUEEN)] = queenAtk;

    // --- King ---
    Bitboard king = bb.byPiece[pieceIndex(color, PieceType::KING)];
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

  attackers |= PAWN[piece::raw(defending)][sq]
             & bb.byPiece[piece::pieceIndex(attackingColor, PieceType::PAWN)];
  attackers |= KNIGHT[sq]
             & bb.byPiece[piece::pieceIndex(attackingColor, PieceType::KNIGHT)];
  attackers |= KING[sq]
             & bb.byPiece[piece::pieceIndex(attackingColor, PieceType::KING)];

  Bitboard rookQueens   = bb.byPiece[piece::pieceIndex(attackingColor, PieceType::ROOK)]
                        | bb.byPiece[piece::pieceIndex(attackingColor, PieceType::QUEEN)];
  Bitboard bishopQueens = bb.byPiece[piece::pieceIndex(attackingColor, PieceType::BISHOP)]
                        | bb.byPiece[piece::pieceIndex(attackingColor, PieceType::QUEEN)];
  attackers |= rook(sq, bb.occupied)   & rookQueens;
  attackers |= bishop(sq, bb.occupied) & bishopQueens;

  return attackers;
}

bool isSquareUnderAttack(const BitboardSet& bb, Square sq, Color defendingColor) {
  return isSquareUnderAttackOcc(bb, sq, defendingColor, bb.occupied);
}

bool isSquareUnderAttackOcc(const BitboardSet& bb, Square sq,
                            Color defendingColor, Bitboard occupancy) {
  Color attacking = ~defendingColor;

  // Early exits: check leapers first (cheap table lookups), then sliders
  // (expensive ray computation).  Returns as soon as any attacker is found.
  if (PAWN[piece::raw(defendingColor)][sq]
      & bb.byPiece[piece::pieceIndex(attacking, PieceType::PAWN)]) return true;
  if (KNIGHT[sq] & bb.byPiece[piece::pieceIndex(attacking, PieceType::KNIGHT)]) return true;
  if (KING[sq]   & bb.byPiece[piece::pieceIndex(attacking, PieceType::KING)])   return true;

  Bitboard rookQueens = bb.byPiece[piece::pieceIndex(attacking, PieceType::ROOK)]
                      | bb.byPiece[piece::pieceIndex(attacking, PieceType::QUEEN)];
  if (rookQueens   && (rook(sq, occupancy)   & rookQueens))   return true;
  Bitboard bishopQueens = bb.byPiece[piece::pieceIndex(attacking, PieceType::BISHOP)]
                        | bb.byPiece[piece::pieceIndex(attacking, PieceType::QUEEN)];
  if (bishopQueens && (bishop(sq, occupancy) & bishopQueens)) return true;

  return false;
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
  // PAWN[~color] gives squares from which a pawn of `color` attacks `sq`.
  Bitboard attackers = PAWN[piece::raw(~color)][sq]
                     & bb.byPiece[piece::pieceIndex(color, PieceType::PAWN)] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::PAWN; }

  attackers = KNIGHT[sq] & bb.byPiece[piece::pieceIndex(color, PieceType::KNIGHT)] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::KNIGHT; }

  attackers = bishop(sq, occupied) & bb.byPiece[piece::pieceIndex(color, PieceType::BISHOP)] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::BISHOP; }

  attackers = rook(sq, occupied) & bb.byPiece[piece::pieceIndex(color, PieceType::ROOK)] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::ROOK; }

  attackers = queen(sq, occupied) & bb.byPiece[piece::pieceIndex(color, PieceType::QUEEN)] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::QUEEN; }

  attackers = KING[sq] & bb.byPiece[piece::pieceIndex(color, PieceType::KING)] & occupied;
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
