#include "attacks.h"

namespace LibreChess {
namespace attacks {

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Table storage
// ---------------------------------------------------------------------------

Bitboard KNIGHT[64] = {};
Bitboard KING[64] = {};
Bitboard PAWN[2][64] = {};

// Diagonal masks (a1-h8 direction), one per square, inclusive of the square.
static Bitboard DIAG_MASK[64];

// Anti-diagonal masks (h1-a8 direction), one per square, inclusive of square.
static Bitboard ANTI_DIAG_MASK[64];

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
  for (Square sq = 0; sq < 64; ++sq) {
    int rank = sq / 8, file = sq % 8;

    // Diagonal (rank - file = constant)
    Bitboard diag = 0;
    int startR = (rank >= file) ? rank - file : 0;
    int startF = (file >= rank) ? file - rank : 0;
    for (int r = startR, f = startF; r < 8 && f < 8; ++r, ++f)
      diag |= squareBB(r * 8 + f);
    DIAG_MASK[sq] = diag;

    // Anti-diagonal (rank + file = constant)
    Bitboard adiag = 0;
    int sum = rank + file;
    int startR2 = (sum <= 7) ? 0 : sum - 7;
    int startF2 = (sum <= 7) ? sum : 7;
    for (int r = startR2, f = startF2; r < 8 && f >= 0; ++r, --f)
      adiag |= squareBB(r * 8 + f);
    ANTI_DIAG_MASK[sq] = adiag;
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
  int rank = sq / 8;
  int file = sq % 8;

  uint8_t occ6 = static_cast<uint8_t>((occupied >> (rank * 8 + 1)) & 0x3F);
  Bitboard attacks = static_cast<Bitboard>(FIRST_RANK_ATTACKS[file][occ6])
                     << (rank * 8);
  attacks |= lineHQ(occupied, sq, FILE_A << file);
  return attacks;
}

Bitboard bishop(Square sq, Bitboard occupied) {
  return lineHQ(occupied, sq, DIAG_MASK[sq])
       | lineHQ(occupied, sq, ANTI_DIAG_MASK[sq]);
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
  int r1 = static_cast<int>(s1) / 8, c1 = static_cast<int>(s1) % 8;
  int r2 = static_cast<int>(s2) / 8, c2 = static_cast<int>(s2) % 8;
  int dr = r2 - r1, dc = c2 - c1;

  if (dr != 0 && dc != 0 && (dr < 0 ? -dr : dr) != (dc < 0 ? -dc : dc))
    return 0;

  Bitboard b1 = squareBB(s1), b2 = squareBB(s2);
  if (dr == 0 || dc == 0)
    return rook(s1, b2) & rook(s2, b1);
  return bishop(s1, b2) & bishop(s2, b1);
}

Bitboard line(Square s1, Square s2) {
  int r1 = static_cast<int>(s1) / 8, c1 = static_cast<int>(s1) % 8;
  int r2 = static_cast<int>(s2) / 8, c2 = static_cast<int>(s2) % 8;
  int dr = r2 - r1, dc = c2 - c1;

  if (dr != 0 && dc != 0 && (dr < 0 ? -dr : dr) != (dc < 0 ? -dc : dc))
    return 0;

  Bitboard endpoints = squareBB(s1) | squareBB(s2);
  if (dr == 0 || dc == 0)
    return (rook(s1, 0) & rook(s2, 0)) | endpoints;
  return (bishop(s1, 0) & bishop(s2, 0)) | endpoints;
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
    Bitboard pawns = bb.byPiece[pieceZobristIndex(makePiece(color, PieceType::PAWN))];
    if (c == 0) {
      info.byPiece[c][raw(PieceType::PAWN)] = shiftNE(pawns) | shiftNW(pawns);
    } else {
      info.byPiece[c][raw(PieceType::PAWN)] = shiftSE(pawns) | shiftSW(pawns);
    }

    // --- Knights (table lookup per square) ---
    Bitboard knights = bb.byPiece[pieceZobristIndex(makePiece(color, PieceType::KNIGHT))];
    Bitboard knightAtk = 0;
    Bitboard tmp = knights;
    while (tmp) { knightAtk |= KNIGHT[popLsb(tmp)]; }
    info.byPiece[c][raw(PieceType::KNIGHT)] = knightAtk;

    // --- Bishops ---
    Bitboard bishops = bb.byPiece[pieceZobristIndex(makePiece(color, PieceType::BISHOP))];
    Bitboard bishopAtk = 0;
    tmp = bishops;
    while (tmp) { bishopAtk |= bishop(popLsb(tmp), bb.occupied); }
    info.byPiece[c][raw(PieceType::BISHOP)] = bishopAtk;

    // --- Rooks ---
    Bitboard rooks = bb.byPiece[pieceZobristIndex(makePiece(color, PieceType::ROOK))];
    Bitboard rookAtk = 0;
    tmp = rooks;
    while (tmp) { rookAtk |= rook(popLsb(tmp), bb.occupied); }
    info.byPiece[c][raw(PieceType::ROOK)] = rookAtk;

    // --- Queens ---
    Bitboard queens = bb.byPiece[pieceZobristIndex(makePiece(color, PieceType::QUEEN))];
    Bitboard queenAtk = 0;
    tmp = queens;
    while (tmp) { queenAtk |= queen(popLsb(tmp), bb.occupied); }
    info.byPiece[c][raw(PieceType::QUEEN)] = queenAtk;

    // --- King ---
    Bitboard king = bb.byPiece[pieceZobristIndex(makePiece(color, PieceType::KING))];
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

bool isSquareUnderAttack(const BitboardSet& bb, Square sq, Color defendingColor) {
  Color attackingColor = ~defendingColor;

  // Pawn attacks: PAWN[defending][sq] gives squares from which an attacking
  // pawn could reach sq.
  int pawnIdx = piece::pieceZobristIndex(piece::makePiece(attackingColor, PieceType::PAWN));
  if (PAWN[piece::raw(defendingColor)][sq] & bb.byPiece[pawnIdx])
    return true;

  // Knight attacks
  int knightIdx = piece::pieceZobristIndex(piece::makePiece(attackingColor, PieceType::KNIGHT));
  if (KNIGHT[sq] & bb.byPiece[knightIdx])
    return true;

  // King attacks
  int kingIdx = piece::pieceZobristIndex(piece::makePiece(attackingColor, PieceType::KING));
  if (KING[sq] & bb.byPiece[kingIdx])
    return true;

  // Rook/Queen — orthogonal rays
  int rookIdx = piece::pieceZobristIndex(piece::makePiece(attackingColor, PieceType::ROOK));
  int queenIdx = piece::pieceZobristIndex(piece::makePiece(attackingColor, PieceType::QUEEN));
  Bitboard rookQueens = bb.byPiece[rookIdx] | bb.byPiece[queenIdx];
  if (rook(sq, bb.occupied) & rookQueens)
    return true;

  // Bishop/Queen — diagonal rays
  int bishopIdx = piece::pieceZobristIndex(piece::makePiece(attackingColor, PieceType::BISHOP));
  Bitboard bishopQueens = bb.byPiece[bishopIdx] | bb.byPiece[queenIdx];
  if (bishop(sq, bb.occupied) & bishopQueens)
    return true;

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

// SEE piece values in centipawns, indexed by PieceType.
static constexpr int SEE_VALUE[] = {0, 100, 300, 300, 500, 900, 20000};

// Find the least valuable attacker of `sq` for `color`.  Returns the
// PieceType and sets `attackerBB` to the single-bit bitboard of the
// chosen attacker (for removal from occupancy).
// Returns PieceType::NONE if no attacker exists.
static PieceType leastValuableAttacker(const BitboardSet& bb,
                                       Bitboard occupied,
                                       Square sq, Color color,
                                       Bitboard& attackerBB) {
  // Pawns
  int idx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::PAWN));
  // PAWN[~color] gives squares from which a pawn of `color` attacks `sq`.
  Bitboard attackers = PAWN[piece::raw(~color)][sq] & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::PAWN; }

  // Knights
  idx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::KNIGHT));
  attackers = KNIGHT[sq] & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::KNIGHT; }

  // Bishops
  idx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::BISHOP));
  attackers = bishop(sq, occupied) & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::BISHOP; }

  // Rooks
  idx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::ROOK));
  attackers = rook(sq, occupied) & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::ROOK; }

  // Queens
  idx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::QUEEN));
  attackers = queen(sq, occupied) & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::QUEEN; }

  // King
  idx = piece::pieceZobristIndex(piece::makePiece(color, PieceType::KING));
  attackers = KING[sq] & bb.byPiece[idx] & occupied;
  if (attackers) { attackerBB = attackers & -attackers; return PieceType::KING; }

  return PieceType::NONE;
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
  gain[d] = SEE_VALUE[piece::raw(captured)];

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
    gain[d] = SEE_VALUE[piece::raw(onTarget)] - gain[d - 1];

    // If the gain is already so negative that even capturing can't help,
    // we can prune: the side to move would choose not to recapture.
    // But we must be careful: this is an optimization, not strictly needed.
    // For correctness we continue the loop.

    // Remove the recapturing piece from occupancy.
    occupied ^= attackerBB;
    onTarget = nextAttacker;
    side = ~side;
  }

  // Walk back the gain list using negamax: at each level, the capturing
  // side chooses the better of "recapture" (gain[d]) vs "stand pat"
  // (-gain[d-1]).  This is: gain[d-1] = -max(-gain[d-1], gain[d]).
  while (d > 0) {
    int standPat = -gain[d - 1];  // value of NOT recapturing
    gain[d - 1] = -(standPat > gain[d] ? standPat : gain[d]);
    --d;
  }

  return gain[0];
}

}  // namespace attacks
}  // namespace LibreChess
