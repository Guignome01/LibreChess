// Depth-based position test suite — WAC at fixed depth.
//
// Runs all WAC positions at a fixed search depth (no time limit) to produce
// deterministic, machine-independent results.  The solve count is asserted
// against a calibrated baseline — any regression in search quality will fail
// the test.
//
// Loads EPD files from ../suites/.  Move comparison uses coordinate
// notation (the proven pattern from test_search.cpp).
//
// Reference: https://www.chessprogramming.org/Test-Positions
//            https://www.chessprogramming.org/Win_at_Chess

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

/// Fixed search depth — deterministic, no time dependency.
static constexpr int DEPTH = 10;

/// Minimum number of WAC positions the engine must solve at DEPTH.
/// Calibrated from baseline run — update when search improves.
static constexpr int WAC_DEPTH_BASELINE = 238;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Suite runner — depth-based, no time limit
// ---------------------------------------------------------------------------

/// Run all positions at fixed depth.  Returns number solved.
static int runDepthSuite(const std::vector<EPDRecord>& records) {
  int solved = 0;

  for (int i = 0; i < static_cast<int>(records.size()); ++i) {
    const EPDRecord& rec = records[i];

    Position pos;
    pos.loadFEN(rec.fen.c_str());

    // Depth-only search — no time limit
    search::SearchLimits limits;
    limits.maxDepth = DEPTH;
    search::SearchState state(chronoMillis, &tables.tt, &tables.pawn, &tables.eval);
    search::SearchResult result = search::findBestMove(
        pos, limits, state);
    std::string engineMove = moveToStr(result.bestMove);

    // Check bm/am
    const EPDOperation* bmOp = rec.findOperation("bm");
    const EPDOperation* amOp = rec.findOperation("am");

    std::string id = rec.id();
    if (id.empty()) {
      char buf[16];
      snprintf(buf, sizeof(buf), "#%d", i + 1);
      id = buf;
    }

    bool pass = false;

    if (bmOp && bmOp->operandCount > 0) {
      for (int j = 0; j < bmOp->operandCount; ++j) {
        std::string expected = sanToCoordinate(pos, bmOp->operands[j]);
        if (!expected.empty() && engineMove == expected) {
          pass = true;
          break;
        }
      }
    }

    if (amOp && amOp->operandCount > 0) {
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
// Test
// ===========================================================================

static void test_wac_depth(void) {
  tables.clear();
  std::vector<EPDRecord> records = loadEPDFile(EPD_DIR + "wac.epd");
  int count = static_cast<int>(records.size());
  TEST_ASSERT_MESSAGE(count > 0, "Failed to load wac.epd");

  printf("\n--- WAC depth %d (%d positions) ---\n", DEPTH, count);
  int solved = runDepthSuite(records);
  printf("WAC result: %d / %d (%.1f%%)\n", solved, count,
         100.0 * solved / count);

  // Hard gate — search quality must not regress
  TEST_ASSERT_GREATER_OR_EQUAL(WAC_DEPTH_BASELINE, solved);
}

// ===========================================================================
// Entry point
// ===========================================================================

int main(int argc, char** argv) {
  tables.init();
  UNITY_BEGIN();
  RUN_TEST(test_wac_depth);
  int result = UNITY_END();
  tables.free();
  return result;
}
