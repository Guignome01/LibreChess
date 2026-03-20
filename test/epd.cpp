#include "epd.h"

#include <cctype>

// ---------------------------------------------------------------------------
// EPDRecord convenience accessors
// ---------------------------------------------------------------------------

const EPDOperation* EPDRecord::findOperation(const std::string& opcode) const {
  for (int i = 0; i < operationCount; ++i)
    if (operations[i].opcode == opcode) return &operations[i];
  return nullptr;
}

std::string EPDRecord::id() const {
  const EPDOperation* op = findOperation("id");
  if (!op || op->operandCount == 0) return "";
  return op->operands[0];
}

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

namespace {

/// Advance `pos` past any leading spaces.
void skipSpaces(const std::string& line, size_t& pos) {
  while (pos < line.size() && line[pos] == ' ') ++pos;
}

/// Extract the next space-delimited token starting at `pos`.
/// Advances `pos` past the token and any trailing spaces.
/// Returns empty string if at end of line or semicolon.
std::string nextToken(const std::string& line, size_t& pos) {
  skipSpaces(line, pos);
  if (pos >= line.size() || line[pos] == ';') return "";
  size_t start = pos;
  while (pos < line.size() && line[pos] != ' ' && line[pos] != ';') ++pos;
  return line.substr(start, pos - start);
}

/// Extract a quoted string starting at `pos` (which must point to '"').
/// Advances `pos` past the closing quote.
/// Returns the content between quotes (unquoted).
std::string readQuoted(const std::string& line, size_t& pos) {
  if (pos >= line.size() || line[pos] != '"') return "";
  ++pos;  // skip opening quote
  size_t start = pos;
  while (pos < line.size() && line[pos] != '"') ++pos;
  std::string result = line.substr(start, pos - start);
  if (pos < line.size()) ++pos;  // skip closing quote
  return result;
}

/// Strip trailing commas from a string (ERET uses `bm dxe5, Nf3`).
void stripTrailingComma(std::string& s) {
  while (!s.empty() && s.back() == ',') s.pop_back();
}

/// Parse a single operation (opcode + operands) starting at `pos`.
/// Stops at semicolon or end-of-line. Advances `pos` past the semicolon.
EPDOperation parseOperation(const std::string& line, size_t& pos) {
  EPDOperation op;
  op.opcode = nextToken(line, pos);
  if (op.opcode.empty()) return op;

  skipSpaces(line, pos);

  // Read operands until semicolon or end-of-line.
  while (pos < line.size() && line[pos] != ';') {
    skipSpaces(line, pos);
    if (pos >= line.size() || line[pos] == ';') break;

    if (op.operandCount >= EPD_MAX_OPERANDS) break;

    if (line[pos] == '"') {
      // Quoted string operand (id, c0, etc.)
      op.operands[op.operandCount++] = readQuoted(line, pos);
    } else {
      // Unquoted operand (SAN move, integer, etc.)
      std::string tok = nextToken(line, pos);
      stripTrailingComma(tok);
      if (!tok.empty()) op.operands[op.operandCount++] = tok;
    }
  }

  // Skip past semicolon if present.
  if (pos < line.size() && line[pos] == ';') ++pos;

  return op;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace epd {

EPDRecord parseEPDLine(const std::string& line) {
  EPDRecord rec;
  if (line.empty()) return rec;

  size_t pos = 0;

  // Extract the four FEN fields: piece placement, side, castling, EP.
  std::string fields[4];
  for (int i = 0; i < 4; ++i) {
    fields[i] = nextToken(line, pos);
    if (fields[i].empty()) return rec;  // not enough FEN fields
  }

  rec.fen = fields[0] + " " + fields[1] + " " + fields[2] + " " + fields[3];

  // Parse operations until end-of-line.
  skipSpaces(line, pos);
  while (pos < line.size() && rec.operationCount < EPD_MAX_OPERATIONS) {
    EPDOperation op = parseOperation(line, pos);
    if (op.opcode.empty()) break;
    rec.operations[rec.operationCount++] = op;
    skipSpaces(line, pos);
  }

  return rec;
}

bool validateEPDLine(const std::string& line) {
  if (line.empty()) return false;

  size_t pos = 0;

  // Field 1: piece placement — must contain exactly 7 slashes.
  std::string placement = nextToken(line, pos);
  if (placement.empty()) return false;

  int slashes = 0;
  for (char c : placement) {
    if (c == '/')
      ++slashes;
    else if (!std::isdigit(c) && std::string("PNBRQKpnbrqk").find(c) == std::string::npos)
      return false;
  }
  if (slashes != 7) return false;

  // Field 2: side to move — must be 'w' or 'b'.
  std::string side = nextToken(line, pos);
  if (side != "w" && side != "b") return false;

  // Field 3: castling — must be '-' or combination of KQkq.
  std::string castling = nextToken(line, pos);
  if (castling.empty()) return false;
  if (castling != "-") {
    for (char c : castling)
      if (std::string("KQkq").find(c) == std::string::npos) return false;
  }

  // Field 4: en passant target — must be '-' or a valid square.
  std::string ep = nextToken(line, pos);
  if (ep.empty()) return false;
  if (ep != "-") {
    if (ep.size() != 2) return false;
    if (ep[0] < 'a' || ep[0] > 'h') return false;
    if (ep[1] != '3' && ep[1] != '6') return false;
  }

  return true;
}

}  // namespace epd
