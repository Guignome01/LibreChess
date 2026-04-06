#include <unity.h>

#include <position.h>
#include <history.h>
#include <move.h>
#include <piece.h>
#include <types.h>

#include <cstdio>

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Perft — exhaustive move-tree node count for move generator correctness.
// Reference: Chess Programming Wiki "Perft Results" page.
//
// Standalone test suite: run with `pio test -e native -f test_perft`
// ---------------------------------------------------------------------------

// Detailed perft result counters matching Chess Programming Wiki columns.
struct PerftResult {
  uint64_t nodes = 0;
  uint64_t captures = 0;
  uint64_t ep = 0;
  uint64_t castles = 0;
  uint64_t promotions = 0;
  uint64_t checks = 0;
  uint64_t checkmates = 0;

  PerftResult& operator+=(const PerftResult& o) {
    nodes += o.nodes;
    captures += o.captures;
    ep += o.ep;
    castles += o.castles;
    promotions += o.promotions;
    checks += o.checks;
    checkmates += o.checkmates;
    return *this;
  }
};

static Position perftBoard;

void setUp(void) {}
void tearDown(void) {}

// Recursive perft with detailed move-type counters.
// Counters are accumulated at leaf depth only (depth == 1),
// matching the Chess Programming Wiki convention.
static PerftResult perft(Position& pos, int depth) {
  PerftResult result;
  if (depth == 0) {
    result.nodes = 1;
    return result;
  }

  Color turn = pos.currentTurn();
  PositionState prevState = pos.positionState();

  for (Square sq = 0; sq < 64; sq++) {
    Piece piece = pos.getSquare(sq);
    if (piece == Piece::NONE) continue;
    if (piece::pieceColor(piece) != turn) continue;

    MoveList moves;
    pos.getPossibleMoves(sq, moves);

    for (int i = 0; i < moves.count; i++) {
      Move m = moves.moves[i];
      Square toSq = static_cast<Square>(m.to);

      // Map promotion index to character for makeMove
      static constexpr char PROMO_CHARS[] = {'n', 'b', 'r', 'q'};
      char promoPiece = m.isPromotion() ? PROMO_CHARS[m.promoIndex()] : ' ';
      Piece targetPiece = pos.getSquare(toSq);

      MoveResult mr = pos.makeMove(sq, toSq, promoPiece);
      if (!mr.valid()) continue;

      MoveEntry entry = MoveEntry::build(sq, toSq,
                                         piece, targetPiece,
                                         mr, prevState);

      if (depth == 1) {
        // Leaf move — count node and move-type properties
        result.nodes++;
        if (mr.isCapture()) result.captures++;
        if (mr.isEnPassant()) result.ep++;
        if (mr.isCastling()) result.castles++;
        if (mr.isPromotion()) result.promotions++;
        if (mr.isCheck()) result.checks++;
        if (mr.gameResult == GameResult::CHECKMATE) result.checkmates++;
      } else {
        result += perft(pos, depth - 1);
      }

      pos.reverseMove(entry);
    }
  }
  return result;
}

// Assert all detailed perft counters for a given depth.
static void expectPerft(Position& pos, int depth,
                        uint64_t nodes, uint64_t captures, uint64_t ep,
                        uint64_t castles, uint64_t promotions,
                        uint64_t checks, uint64_t checkmates) {
  PerftResult r = perft(pos, depth);
  char msg[64];
  snprintf(msg, sizeof(msg), "depth %d nodes", depth);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(nodes, r.nodes, msg);
  snprintf(msg, sizeof(msg), "depth %d captures", depth);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(captures, r.captures, msg);
  snprintf(msg, sizeof(msg), "depth %d ep", depth);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(ep, r.ep, msg);
  snprintf(msg, sizeof(msg), "depth %d castles", depth);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(castles, r.castles, msg);
  snprintf(msg, sizeof(msg), "depth %d promotions", depth);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(promotions, r.promotions, msg);
  snprintf(msg, sizeof(msg), "depth %d checks", depth);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(checks, r.checks, msg);
  snprintf(msg, sizeof(msg), "depth %d checkmates", depth);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(checkmates, r.checkmates, msg);
}

// ---------------------------------------------------------------------------
// Test positions from Chess Programming Wiki "Perft Results"
// Reference values include: Nodes, Captures, E.p., Castles, Promotions,
//                           Checks, Checkmates
// ---------------------------------------------------------------------------

// Position 1: Initial position
static void test_perft_initial_position(void) {
  perftBoard.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  //                                nodes  capt  ep  castl promo check  mate
  expectPerft(perftBoard, 1,           20,    0,  0,    0,    0,     0,    0);
  expectPerft(perftBoard, 2,          400,    0,  0,    0,    0,     0,    0);
  expectPerft(perftBoard, 3,         8902,   34,  0,    0,    0,    12,    0);
  expectPerft(perftBoard, 4,       197281, 1576,  0,    0,    0,   469,    8);
  expectPerft(perftBoard, 5,      4865609,82719,258,    0,    0, 27351,  347);
  // expectPerft(perftBoard, 6, 119060324ULL, 2812008ULL, 5248ULL, 0ULL, 0ULL, 809099ULL, 10828ULL);
  // expectPerft(perftBoard, 7, 3195901860ULL, 108329926ULL, 319617ULL, 883453ULL, 0ULL, 33103848ULL, 435767ULL);
  // expectPerft(perftBoard, 8, 84998978956ULL, 3523740106ULL, 7187977ULL, 23605205ULL, 0ULL, 968981593ULL, 9852036ULL);
  // expectPerft(perftBoard, 9, 2439530234167ULL, 125208536153ULL, 319496827ULL, 1784356000ULL, 17334376ULL, 36095901903ULL, 400191963ULL);
}

// Position 2: Kiwipete — rich in castling, en passant, and promotion edge cases
static void test_perft_kiwipete(void) {
  perftBoard.loadFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  //                                nodes   capt    ep  castl  promo  check mate
  expectPerft(perftBoard, 1,           48,      8,   0,     2,     0,     0,   0);
  expectPerft(perftBoard, 2,         2039,    351,   1,    91,     0,     3,   0);
  expectPerft(perftBoard, 3,        97862,  17102,  45,  3162,     0,   993,   1);
  expectPerft(perftBoard, 4,      4085603, 757163,1929,128013, 15172, 25523,  43);
  // expectPerft(perftBoard, 5, 193690690ULL, 35043416ULL, 73365ULL, 4993637ULL, 8392ULL, 3309887ULL, 30171ULL);
  // expectPerft(perftBoard, 6, 8031647685ULL, 1558445089ULL, 3577504ULL, 184513607ULL, 56627920ULL, 92238050ULL, 360003ULL);
}

// Position 3 — exposed kings, pawn captures, rook endgame
static void test_perft_position3(void) {
  perftBoard.loadFEN("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
  //                                 nodes   capt    ep castl promo  check  mate
  expectPerft(perftBoard, 1,            14,      1,   0,   0,    0,     2,    0);
  expectPerft(perftBoard, 2,           191,     14,   0,   0,    0,    10,    0);
  expectPerft(perftBoard, 3,          2812,    209,   2,   0,    0,   267,    0);
  expectPerft(perftBoard, 4,         43238,   3348, 123,   0,    0,  1680,   17);
  expectPerft(perftBoard, 5,        674624,  52051,1165,   0,    0, 52950,    0);
  expectPerft(perftBoard, 6,      11030083, 940350,33325,  0, 7552,452473, 2733);
  // expectPerft(perftBoard, 7, 178633661ULL, 14519036ULL, 294874ULL, 0ULL, 140024ULL, 12797406ULL, 87ULL);
  // expectPerft(perftBoard, 8, 3009794393ULL, 267586558ULL, 8009239ULL, 0ULL, 6578076ULL, 135626805ULL, 450410ULL);
}

// Position 4 — promotions, checks, en passant
static void test_perft_position4(void) {
  perftBoard.loadFEN("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
  //                                 nodes    capt   ep castl  promo  check   mate
  expectPerft(perftBoard, 1,             6,      0,   0,    0,     0,     0,     0);
  expectPerft(perftBoard, 2,           264,     87,   0,    6,    48,    10,     0);
  expectPerft(perftBoard, 3,          9467,   1021,   4,    0,   120,    38,    22);
  expectPerft(perftBoard, 4,        422333, 131393,   0, 7795, 60032, 15492,     5);
  expectPerft(perftBoard, 5,      15833292,2046173,6512,    0,329464,200568, 50562);
  // expectPerft(perftBoard, 6, 706045033ULL, 210369132ULL, 212ULL, 10882006ULL, 81102984ULL, 26973664ULL, 81076ULL);
}

// Position 5 — promotion on d8, mixed castling rights (nodes only, no wiki counters)
static void test_perft_position5(void) {
  perftBoard.loadFEN("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
  TEST_ASSERT_EQUAL_UINT64(44, perft(perftBoard, 1).nodes);
  TEST_ASSERT_EQUAL_UINT64(1486, perft(perftBoard, 2).nodes);
  TEST_ASSERT_EQUAL_UINT64(62379, perft(perftBoard, 3).nodes);
  TEST_ASSERT_EQUAL_UINT64(2103487, perft(perftBoard, 4).nodes);
  // TEST_ASSERT_EQUAL_UINT64(89941194ULL, perft(perftBoard, 5).nodes);
}

// Position 6 — symmetrical, double fianchetto (nodes only, no wiki counters)
static void test_perft_position6(void) {
  perftBoard.loadFEN("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
  TEST_ASSERT_EQUAL_UINT64(46, perft(perftBoard, 1).nodes);
  TEST_ASSERT_EQUAL_UINT64(2079, perft(perftBoard, 2).nodes);
  TEST_ASSERT_EQUAL_UINT64(89890, perft(perftBoard, 3).nodes);
  TEST_ASSERT_EQUAL_UINT64(3894594, perft(perftBoard, 4).nodes);
  // TEST_ASSERT_EQUAL_UINT64(164075551ULL, perft(perftBoard, 5).nodes);
  // TEST_ASSERT_EQUAL_UINT64(6923051137ULL, perft(perftBoard, 6).nodes);
  // TEST_ASSERT_EQUAL_UINT64(287188994746ULL, perft(perftBoard, 7).nodes);
  // TEST_ASSERT_EQUAL_UINT64(11923589843526ULL, perft(perftBoard, 8).nodes);
  // TEST_ASSERT_EQUAL_UINT64(490154852788714ULL, perft(perftBoard, 9).nodes);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_perft_initial_position);
  RUN_TEST(test_perft_kiwipete);
  RUN_TEST(test_perft_position3);
  RUN_TEST(test_perft_position4);
  RUN_TEST(test_perft_position5);
  RUN_TEST(test_perft_position6);
  return UNITY_END();
}
