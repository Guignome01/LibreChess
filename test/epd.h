#ifndef TEST_EPD_H
#define TEST_EPD_H

// ---------------------------------------------------------------------------
// EPD (Extended Position Description) parser — test utility.
//
// Parses EPD records as defined by the Chess Programming Wiki. An EPD line
// consists of the first four FEN fields (piece placement, side to move,
// castling ability, en passant target) followed by zero or more operations.
// Each operation is an opcode with operands, terminated by a semicolon.
//
// Supports the operation types used in standard tactical test suites:
//   bm  — best move(s), SAN                (WAC, BK, ERET)
//   am  — avoid move(s), SAN               (ERET)
//   id  — position identification, quoted   (all suites)
//   c0  — comment, quoted                   (WAC)
//
// Placed in test/ root so PlatformIO auto-links it into every test binary,
// available to both test_core/ (EPD parser unit tests) and test_tactics/
// (tactical suite runner).
// ---------------------------------------------------------------------------

#include <string>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Maximum number of operands per operation (e.g. multiple bm moves).
static constexpr int EPD_MAX_OPERANDS = 16;

/// Maximum number of operations per EPD record.
static constexpr int EPD_MAX_OPERATIONS = 8;

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

/// A single EPD operation: opcode + operand list.
/// Examples: `bm Nf3 Qxa8`, `id "WAC.001"`, `am d5`.
struct EPDOperation {
  std::string opcode;
  std::string operands[EPD_MAX_OPERANDS];
  int operandCount = 0;
};

/// A parsed EPD record: 4-field FEN + operation list + convenience accessors.
struct EPDRecord {
  std::string fen;  // 4-field FEN (no halfmove/fullmove clocks)
  EPDOperation operations[EPD_MAX_OPERATIONS];
  int operationCount = 0;

  /// Find an operation by opcode. Returns nullptr if not found.
  const EPDOperation* findOperation(const std::string& opcode) const;

  /// Shortcut: return the id string (unquoted), or empty if absent.
  std::string id() const;
};

// ---------------------------------------------------------------------------
// Parsing functions
// ---------------------------------------------------------------------------

namespace epd {

/// Parse a single EPD line into an EPDRecord.
/// Returns a record with empty fen on failure (e.g. blank line).
EPDRecord parseEPDLine(const std::string& line);

/// Validate that a string is a structurally valid EPD line.
/// Checks: non-empty, has at least 4 space-separated FEN fields,
/// valid piece placement characters, valid side-to-move.
bool validateEPDLine(const std::string& line);

}  // namespace epd

#endif  // TEST_EPD_H
