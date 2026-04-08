// Time-based position test suites — WAC, BK, ERET.
//
// Loads positions from standard .epd files (in ../suites/), runs each
// through the engine with a per-position time budget, and checks whether the
// engine's best move matches the expected move (bm) or avoids a bad move (am).
// Results are informational — individual mismatches print diagnostics but do
// NOT fail the test.
//
// Move comparison uses coordinate notation (e.g. "e2e4", "e7e8q") which is
// the proven pattern from test_search.cpp.  EPD best-move operands are SAN,
// so we parse them via notation::parseSAN() and convert to coordinates.
//
// Reference: https://www.chessprogramming.org/Test-Positions

#include <unity.h>

#include <cstdio>

#include "../test_helpers.h"

using namespace LibreChess;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

static SharedTables tables;

/// EPD directory resolved relative to this source file.
static const std::string EPD_DIR = testFileDir(__FILE__) + "../suites/";

/// Per-position time budget in milliseconds.  Each position gets this much
/// time to find the best move — the standard approach for EPD test suites.
/// No depth cap: the engine searches as deep as it can within the budget.
static constexpr uint32_t TACTICS_TIME_MS = 500;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

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
    search::SearchState state;
    state.timeFunc = chronoMillis;
    state.tt = &tables.tt;
    state.pawnHash = &tables.pawn;
    state.evalHash = &tables.eval;
    search::SearchResult result = search::findBestMove(
        pos, limits, state);
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
  tables.clear();
  std::vector<EPDRecord> records = loadEPDFile(EPD_DIR + "wac.epd");
  int count = static_cast<int>(records.size());
  TEST_ASSERT_MESSAGE(count > 0, "Failed to load wac.epd");

  printf("\n--- WAC (%d positions, %ums/pos) ---\n", count, TACTICS_TIME_MS);
  int solved = runSuite("WAC", records);
  printf("WAC result: %d / %d (%.1f%%)\n", solved, count,
         100.0 * solved / count);
  TEST_ASSERT_GREATER_THAN(0, solved);
}

static void test_bk_suite(void) {
  tables.clear();
  std::vector<EPDRecord> records = loadEPDFile(EPD_DIR + "bk.epd");
  int count = static_cast<int>(records.size());
  TEST_ASSERT_MESSAGE(count > 0, "Failed to load bk.epd");

  printf("\n--- BK (%d positions, %ums/pos) ---\n", count, TACTICS_TIME_MS);
  int solved = runSuite("BK", records);
  printf("BK result: %d / %d (%.1f%%)\n", solved, count,
         100.0 * solved / count);
  TEST_ASSERT_GREATER_THAN(0, solved);
}

static void test_eret_suite(void) {
  tables.clear();
  std::vector<EPDRecord> records = loadEPDFile(EPD_DIR + "eret.epd");
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
  tables.init();
  UNITY_BEGIN();
  RUN_TEST(test_wac_suite);
  RUN_TEST(test_bk_suite);
  RUN_TEST(test_eret_suite);
  int result = UNITY_END();
  tables.free();
  return result;
}
