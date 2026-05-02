#include <unity.h>

#include "epd.h"

using namespace LibreChess;

// ---------------------------------------------------------------------------
// parseEPDLine — basic parsing
// ---------------------------------------------------------------------------

// Minimal EPD line with a single bm operation.
static void test_epd_parse_single_bm(void) {
  EPDRecord rec = epd::parseEPDLine(
      "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - bm Qxf7+; id \"Scholar.1\";");
  TEST_ASSERT_FALSE(rec.fen.empty());
  TEST_ASSERT_EQUAL_STRING(
      "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq -",
      rec.fen.c_str());
  TEST_ASSERT_EQUAL_INT(2, rec.operationCount);
  TEST_ASSERT_EQUAL_STRING("bm", rec.operations[0].opcode.c_str());
  TEST_ASSERT_EQUAL_INT(1, rec.operations[0].operandCount);
  TEST_ASSERT_EQUAL_STRING("Qxf7+", rec.operations[0].operands[0].c_str());
}

// Multiple bm moves separated by spaces.
static void test_epd_parse_multiple_bm(void) {
  EPDRecord rec = epd::parseEPDLine(
      "2rr3k/pp3pp1/1nnrb1p1/3pN3/2pP4/2P3N1/PP1QBPPP/R4RK1 w - - bm Nf5 Nxd5; id \"WAC.099\";");
  const EPDOperation* bm = rec.findOperation("bm");
  TEST_ASSERT_NOT_NULL(bm);
  TEST_ASSERT_EQUAL_INT(2, bm->operandCount);
  TEST_ASSERT_EQUAL_STRING("Nf5", bm->operands[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Nxd5", bm->operands[1].c_str());
}

// ---------------------------------------------------------------------------
// parseEPDLine — am (avoid move) operation
// ---------------------------------------------------------------------------

// ERET-style line with am instead of bm.
static void test_epd_parse_am_operation(void) {
  EPDRecord rec = epd::parseEPDLine(
      "r2qk2r/ppp1b1pp/2n1p3/3pP1n1/3P2b1/2PB1N2/PP4PP/RNBQK2R w KQkq - am Nxg5; id \"ERET 024\";");
  const EPDOperation* am = rec.findOperation("am");
  TEST_ASSERT_NOT_NULL(am);
  TEST_ASSERT_EQUAL_INT(1, am->operandCount);
  TEST_ASSERT_EQUAL_STRING("Nxg5", am->operands[0].c_str());
}

// ---------------------------------------------------------------------------
// parseEPDLine — quoted string operands (id, c0)
// ---------------------------------------------------------------------------

// id and c0 with quoted strings.
static void test_epd_parse_quoted_operands(void) {
  EPDRecord rec = epd::parseEPDLine(
      "8/8/8/8/8/8/8/8 w - - bm e4; id \"Test.001\"; c0 \"A comment\";");
  TEST_ASSERT_EQUAL_INT(3, rec.operationCount);

  const EPDOperation* id = rec.findOperation("id");
  TEST_ASSERT_NOT_NULL(id);
  TEST_ASSERT_EQUAL_STRING("Test.001", id->operands[0].c_str());

  const EPDOperation* c0 = rec.findOperation("c0");
  TEST_ASSERT_NOT_NULL(c0);
  TEST_ASSERT_EQUAL_STRING("A comment", c0->operands[0].c_str());
}

// ---------------------------------------------------------------------------
// parseEPDLine — ERET comma-separated operands
// ---------------------------------------------------------------------------

// ERET style: `bm dxe5, Nf3` — commas stripped from operands.
static void test_epd_parse_comma_separated_operands(void) {
  EPDRecord rec = epd::parseEPDLine(
      "8/8/8/8/8/8/8/8 w - - bm dxe5, Nf3; id \"ERET 011\";");
  const EPDOperation* bm = rec.findOperation("bm");
  TEST_ASSERT_NOT_NULL(bm);
  TEST_ASSERT_EQUAL_INT(2, bm->operandCount);
  TEST_ASSERT_EQUAL_STRING("dxe5", bm->operands[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Nf3", bm->operands[1].c_str());
}

// ---------------------------------------------------------------------------
// parseEPDLine — no space before semicolon
// ---------------------------------------------------------------------------

// Some EPD files have `bm Rxh7;c0 "comment"` with no space before ';'.
static void test_epd_parse_no_space_before_semicolon(void) {
  EPDRecord rec = epd::parseEPDLine(
      "8/8/8/8/8/8/8/8 w - - bm Rxh7;c0 \"Mate in 7\";");
  const EPDOperation* bm = rec.findOperation("bm");
  TEST_ASSERT_NOT_NULL(bm);
  TEST_ASSERT_EQUAL_STRING("Rxh7", bm->operands[0].c_str());

  const EPDOperation* c0 = rec.findOperation("c0");
  TEST_ASSERT_NOT_NULL(c0);
  TEST_ASSERT_EQUAL_STRING("Mate in 7", c0->operands[0].c_str());
}

// ---------------------------------------------------------------------------
// parseEPDLine — c9 game result (tuning corpus format)
// ---------------------------------------------------------------------------

// Tuning corpus line: FEN + c9 "1.0" (white win).
static void test_epd_parse_c9_white_win(void) {
  EPDRecord rec = epd::parseEPDLine(
      "r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - c9 \"1.0\";");
  const EPDOperation* c9 = rec.findOperation("c9");
  TEST_ASSERT_NOT_NULL(c9);
  TEST_ASSERT_EQUAL_INT(1, c9->operandCount);
  TEST_ASSERT_EQUAL_STRING("1.0", c9->operands[0].c_str());
}

// Tuning corpus: c9 "0.5" (draw).
static void test_epd_parse_c9_draw(void) {
  EPDRecord rec = epd::parseEPDLine(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - c9 \"0.5\";");
  const EPDOperation* c9 = rec.findOperation("c9");
  TEST_ASSERT_NOT_NULL(c9);
  TEST_ASSERT_EQUAL_STRING("0.5", c9->operands[0].c_str());
}

// Tuning corpus: c9 "0.0" (black win).
static void test_epd_parse_c9_black_win(void) {
  EPDRecord rec = epd::parseEPDLine(
      "8/8/8/8/8/8/8/8 w - - c9 \"0.0\";");
  const EPDOperation* c9 = rec.findOperation("c9");
  TEST_ASSERT_NOT_NULL(c9);
  TEST_ASSERT_EQUAL_STRING("0.0", c9->operands[0].c_str());
}

// ---------------------------------------------------------------------------
// parseEPDLine — no operations (FEN only)
// ---------------------------------------------------------------------------

// EPD with no operations is valid — just a 4-field FEN.
static void test_epd_parse_no_operations(void) {
  EPDRecord rec = epd::parseEPDLine(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -");
  TEST_ASSERT_EQUAL_STRING(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
      rec.fen.c_str());
  TEST_ASSERT_EQUAL_INT(0, rec.operationCount);
}

// ---------------------------------------------------------------------------
// parseEPDLine — empty / malformed input
// ---------------------------------------------------------------------------

// Empty string returns empty record.
static void test_epd_parse_empty_line(void) {
  EPDRecord rec = epd::parseEPDLine("");
  TEST_ASSERT_TRUE(rec.fen.empty());
  TEST_ASSERT_EQUAL_INT(0, rec.operationCount);
}

// Incomplete FEN (only 2 fields) returns empty record.
static void test_epd_parse_incomplete_fen(void) {
  EPDRecord rec = epd::parseEPDLine("8/8/8/8/8/8/8/8 w");
  TEST_ASSERT_TRUE(rec.fen.empty());
}

// ---------------------------------------------------------------------------
// validateEPDLine
// ---------------------------------------------------------------------------

// Valid EPD line with operations.
static void test_epd_validate_valid_line(void) {
  TEST_ASSERT_TRUE(epd::validateEPDLine(
      "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - bm Qxf7+;"));
}

// Valid EPD line with no operations (FEN only).
static void test_epd_validate_fen_only(void) {
  TEST_ASSERT_TRUE(epd::validateEPDLine(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"));
}

// Empty line is invalid.
static void test_epd_validate_empty(void) {
  TEST_ASSERT_FALSE(epd::validateEPDLine(""));
}

// Missing FEN fields.
static void test_epd_validate_missing_fields(void) {
  TEST_ASSERT_FALSE(epd::validateEPDLine("8/8/8/8/8/8/8/8 w KQkq"));
}

// Invalid side to move.
static void test_epd_validate_bad_side(void) {
  TEST_ASSERT_FALSE(epd::validateEPDLine("8/8/8/8/8/8/8/8 x KQkq -"));
}

// Invalid piece placement character.
static void test_epd_validate_bad_placement(void) {
  TEST_ASSERT_FALSE(epd::validateEPDLine("8/8/8/8/8/8/8/Z w KQkq -"));
}

// Wrong number of ranks (missing slash).
static void test_epd_validate_wrong_rank_count(void) {
  TEST_ASSERT_FALSE(epd::validateEPDLine("8/8/8/8/8/8/8 w KQkq -"));
}

// Rank widths must be validated, not just slash count and piece characters.
static void test_epd_validate_bad_rank_sum(void) {
  TEST_ASSERT_FALSE(epd::validateEPDLine("4k4/8/8/8/8/8/8/4K3 w - -"));
}

// Invalid en passant square.
static void test_epd_validate_bad_ep(void) {
  TEST_ASSERT_FALSE(epd::validateEPDLine("8/8/8/8/8/8/8/8 w KQkq e5"));
}

// Valid en passant square.
static void test_epd_validate_valid_ep(void) {
  TEST_ASSERT_TRUE(epd::validateEPDLine(
      "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3"));
}

static void test_epd_validate_too_many_operands(void) {
  TEST_ASSERT_FALSE(epd::validateEPDLine(
      "4k3/8/8/8/8/8/8/4K3 w - - bm a1 a2 a3 a4 a5 a6 a7 a8 b1 b2 b3 b4 b5 b6 b7 b8 c1;"));
}

static void test_epd_validate_too_many_operations(void) {
  TEST_ASSERT_FALSE(epd::validateEPDLine(
      "4k3/8/8/8/8/8/8/4K3 w - - c0 \"0\"; c1 \"1\"; c2 \"2\"; c3 \"3\"; c4 \"4\"; c5 \"5\"; c6 \"6\"; c7 \"7\"; c8 \"8\";"));
}

static void test_epd_parse_too_many_operands_returns_empty(void) {
  EPDRecord rec = epd::parseEPDLine(
      "4k3/8/8/8/8/8/8/4K3 w - - bm a1 a2 a3 a4 a5 a6 a7 a8 b1 b2 b3 b4 b5 b6 b7 b8 c1;");
  TEST_ASSERT_TRUE(rec.fen.empty());
}

// ---------------------------------------------------------------------------
// Convenience accessors
// ---------------------------------------------------------------------------

// findOperation returns correct operation.
static void test_epd_find_operation(void) {
  EPDRecord rec = epd::parseEPDLine(
      "8/8/8/8/8/8/8/8 w - - bm e4; id \"Test\"; c0 \"Comment\";");
  const EPDOperation* op = rec.findOperation("c0");
  TEST_ASSERT_NOT_NULL(op);
  TEST_ASSERT_EQUAL_STRING("Comment", op->operands[0].c_str());
}

// findOperation returns nullptr for missing opcode.
static void test_epd_find_operation_miss(void) {
  EPDRecord rec = epd::parseEPDLine("8/8/8/8/8/8/8/8 w - - bm e4;");
  TEST_ASSERT_NULL(rec.findOperation("am"));
}

// id() shortcut returns unquoted id string.
static void test_epd_id_shortcut(void) {
  EPDRecord rec = epd::parseEPDLine(
      "8/8/8/8/8/8/8/8 w - - bm e4; id \"WAC.042\";");
  TEST_ASSERT_EQUAL_STRING("WAC.042", rec.id().c_str());
}

// id() returns empty string when no id operation.
static void test_epd_id_shortcut_missing(void) {
  EPDRecord rec = epd::parseEPDLine("8/8/8/8/8/8/8/8 w - - bm e4;");
  TEST_ASSERT_EQUAL_STRING("", rec.id().c_str());
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_epd_tests() {
  // parseEPDLine
  RUN_TEST(test_epd_parse_single_bm);
  RUN_TEST(test_epd_parse_multiple_bm);
  RUN_TEST(test_epd_parse_am_operation);
  RUN_TEST(test_epd_parse_quoted_operands);
  RUN_TEST(test_epd_parse_comma_separated_operands);
  RUN_TEST(test_epd_parse_no_space_before_semicolon);
  RUN_TEST(test_epd_parse_c9_white_win);
  RUN_TEST(test_epd_parse_c9_draw);
  RUN_TEST(test_epd_parse_c9_black_win);
  RUN_TEST(test_epd_parse_no_operations);
  RUN_TEST(test_epd_parse_empty_line);
  RUN_TEST(test_epd_parse_incomplete_fen);
  RUN_TEST(test_epd_parse_too_many_operands_returns_empty);
  // validateEPDLine
  RUN_TEST(test_epd_validate_valid_line);
  RUN_TEST(test_epd_validate_fen_only);
  RUN_TEST(test_epd_validate_empty);
  RUN_TEST(test_epd_validate_missing_fields);
  RUN_TEST(test_epd_validate_bad_side);
  RUN_TEST(test_epd_validate_bad_placement);
  RUN_TEST(test_epd_validate_wrong_rank_count);
  RUN_TEST(test_epd_validate_bad_rank_sum);
  RUN_TEST(test_epd_validate_bad_ep);
  RUN_TEST(test_epd_validate_valid_ep);
  RUN_TEST(test_epd_validate_too_many_operands);
  RUN_TEST(test_epd_validate_too_many_operations);
  // Convenience accessors
  RUN_TEST(test_epd_find_operation);
  RUN_TEST(test_epd_find_operation_miss);
  RUN_TEST(test_epd_id_shortcut);
  RUN_TEST(test_epd_id_shortcut_missing);
}
