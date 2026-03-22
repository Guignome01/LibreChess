// Tactical test suites — WAC, BK, ERET.
//
// Loads positions from standard .epd files, runs each through the engine at a
// fixed search depth, and checks whether the engine's best move matches the
// expected move (bm) or avoids a bad move (am).  Results are informational —
// individual mismatches print diagnostics but do NOT fail the test.
//
// Move comparison uses coordinate notation (e.g. "e2e4", "e7e8q") which is
// the proven pattern from test_search.cpp.  EPD best-move operands are SAN,
// so we parse them via notation::parseSAN() and convert to coordinates.

#include <unity.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <fen.h>
#include <movegen.h>
#include <notation.h>
#include <position.h>
#include <search.h>

#include "epd.h"

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

/// Per-position time budget in milliseconds.  Each position gets this much
/// time to find the best move — the standard approach for EPD test suites.
/// No depth cap: the engine searches as deep as it can within the budget.
static constexpr uint32_t TACTICS_TIME_MS = 500;

/// Chrono-based millisecond clock for native builds (firmware uses millis()).
static uint32_t chronoMillis() {
  using namespace std::chrono;
  static const auto epoch = steady_clock::now();
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now() - epoch).count());
}

// ---------------------------------------------------------------------------
// EPD file loading
// ---------------------------------------------------------------------------

/// Return the directory containing this source file (and the .epd files).
/// Works whether __FILE__ expands to an absolute or relative path.
static std::string sourceDir() {
  std::string file = __FILE__;
  auto pos = file.find_last_of("/\\");
  return (pos != std::string::npos) ? file.substr(0, pos + 1) : "";
}

/// Load and parse all positions from an .epd file.
/// Tries the path relative to this source file first, then falls back to the
/// bare filename (works when CWD is the project root).
static std::vector<EPDRecord> loadEPD(const std::string& filename) {
  std::vector<EPDRecord> records;
  std::ifstream in(sourceDir() + filename);
  if (!in.is_open()) in.open(filename);

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    EPDRecord rec = epd::parseEPDLine(line);
    if (!rec.fen.empty()) records.push_back(rec);
  }
  return records;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

/// Convert a Move to UCI coordinate string (handles promotions).
static std::string moveToStr(Move m) {
  char promo = ' ';
  if (m.isPromotion()) {
    // 0=Knight, 1=Bishop, 2=Rook, 3=Queen
    static constexpr char PROMO_CHARS[] = {'n', 'b', 'r', 'q'};
    promo = PROMO_CHARS[m.promoIndex()];
  }
  return notation::toCoordinate(rowOf(m.from), colOf(m.from), rowOf(m.to),
                                colOf(m.to), promo);
}

/// Parse a SAN move string into coordinate notation using the given position.
/// Returns empty string on failure.
static std::string sanToCoordinate(const Position& pos,
                                   const std::string& san) {
  int fromRow, fromCol, toRow, toCol;
  char promotion = ' ';
  bool ok =
      notation::parseSAN(pos.bitboards(), pos.mailbox(), pos.positionState(),
                         pos.sideToMove(), san, fromRow, fromCol, toRow, toCol,
                         promotion);
  if (!ok) return "";
  return notation::toCoordinate(fromRow, fromCol, toRow, toCol, promotion);
}

/// Run a single test suite loaded from an .epd file.
///
/// For each record:
///  1. Load the FEN into a Position.
///  2. Run search at TACTICS_DEPTH.
///  3. Convert engine move and expected moves to coordinate notation.
///  4. Check if engine move matches any expected best-move (bm) or
///     avoids any avoid-move (am).
///
/// Mismatches are printed as informational messages (not failures).
/// Returns the number of positions solved.
static int runSuite(const char* suiteName,
                    const std::vector<EPDRecord>& records) {
  int solved = 0;

  for (int i = 0; i < static_cast<int>(records.size()); ++i) {
    const EPDRecord& rec = records[i];


    // Load position from the 4-field FEN
    Position pos;
    pos.loadFEN(rec.fen.c_str());

    // Search — time-only, no depth cap
    search::SearchLimits limits;
    limits.hardTimeMs = TACTICS_TIME_MS;
    search::SearchResult result = search::findBestMove(pos, limits, chronoMillis);
    std::string engineMove = moveToStr(result.bestMove);

    // Determine expected move list and whether it's bm or am
    const EPDOperation* bmOp = rec.findOperation("bm");
    const EPDOperation* amOp = rec.findOperation("am");

    // Get position id for readable output
    std::string id = rec.id();
    if (id.empty()) {
      char buf[16];
      snprintf(buf, sizeof(buf), "#%d", i + 1);
      id = buf;
    }

    bool pass = false;

    if (bmOp && bmOp->operandCount > 0) {
      // Best-move: engine must match one of them
      for (int j = 0; j < bmOp->operandCount; ++j) {
        std::string expected = sanToCoordinate(pos, bmOp->operands[j]);
        if (!expected.empty() && engineMove == expected) {
          pass = true;
          break;
        }
      }
    }

    if (amOp && amOp->operandCount > 0) {
      // Avoid-move: engine must NOT play any of them.
      // If there's no bm, pass defaults true (any non-avoided move is fine).
      if (!bmOp) pass = true;
      for (int j = 0; j < amOp->operandCount; ++j) {
        std::string avoided = sanToCoordinate(pos, amOp->operands[j]);
        if (!avoided.empty() && engineMove == avoided) {
          pass = false;
          break;
        }
      }
    }

    if (pass) {
      ++solved;
    } else {
      // Build expected string for message
      std::string expectedStr;
      if (bmOp) {
        for (int j = 0; j < bmOp->operandCount; ++j) {
          if (j > 0) expectedStr += "/";
          expectedStr += bmOp->operands[j];
        }
      }
      if (amOp) {
        if (!expectedStr.empty()) expectedStr += " ";
        for (int j = 0; j < amOp->operandCount; ++j) {
          expectedStr += "!";
          expectedStr += amOp->operands[j];
        }
      }
      printf("  %s: engine=%s expected=%s\n", id.c_str(), engineMove.c_str(),
             expectedStr.c_str());
    }
  }

  return solved;
}

// ===========================================================================
// Suite tests — each loads its .epd file and runs the full suite
// ===========================================================================

static void test_wac_suite(void) {
  std::vector<EPDRecord> records = loadEPD("wac.epd");
  int count = static_cast<int>(records.size());
  TEST_ASSERT_MESSAGE(count > 0, "Failed to load wac.epd");

  printf("\n--- WAC (%d positions, %ums/pos) ---\n", count, TACTICS_TIME_MS);
  int solved = runSuite("WAC", records);
  printf("WAC result: %d / %d (%.1f%%)\n", solved, count,
         100.0 * solved / count);
  TEST_ASSERT_GREATER_THAN(0, solved);
}

static void test_bk_suite(void) {
  std::vector<EPDRecord> records = loadEPD("bk.epd");
  int count = static_cast<int>(records.size());
  TEST_ASSERT_MESSAGE(count > 0, "Failed to load bk.epd");

  printf("\n--- BK (%d positions, %ums/pos) ---\n", count, TACTICS_TIME_MS);
  int solved = runSuite("BK", records);
  printf("BK result: %d / %d (%.1f%%)\n", solved, count,
         100.0 * solved / count);
  TEST_ASSERT_GREATER_THAN(0, solved);
}

static void test_eret_suite(void) {
  std::vector<EPDRecord> records = loadEPD("eret.epd");
  int count = static_cast<int>(records.size());
  TEST_ASSERT_MESSAGE(count > 0, "Failed to load eret.epd");

  printf("\n--- ERET (%d positions, %ums/pos) ---\n", count, TACTICS_TIME_MS);
  int solved = runSuite("ERET", records);
  printf("ERET result: %d / %d (%.1f%%)\n", solved, count,
         100.0 * solved / count);
  TEST_ASSERT_GREATER_THAN(0, solved);
}

// ===========================================================================
// Entry point
// ===========================================================================

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_wac_suite);
  RUN_TEST(test_bk_suite);
  RUN_TEST(test_eret_suite);
  return UNITY_END();
}
