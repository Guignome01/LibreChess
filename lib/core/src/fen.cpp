#include "fen.h"

#include <cctype>
#include <cstring>
#include <string>

#include "utils.h"

namespace LibreChess {
namespace fen {

// Extract the next space-delimited token from `remaining`, advance past it.
static std::string nextToken(std::string& remaining) {
  if (remaining.empty()) return "";
  size_t sp = remaining.find(' ');
  std::string token = (sp != std::string::npos) ? remaining.substr(0, sp) : remaining;
  remaining = (sp != std::string::npos) ? remaining.substr(sp + 1) : "";
  return token;
}

std::string boardToFEN(const Piece mailbox[], Color currentTurn, const PositionState* state) {
  using namespace LibreChess;
  std::string fen;

  // Board position — rank 8 first, rank 1 last.
  int emptyCount = 0;
  for (int rank = 7; rank >= 0; --rank) {
    if (rank < 7) {
      if (emptyCount > 0) {
        fen += std::to_string(emptyCount);
        emptyCount = 0;
      }
      fen += '/';
    }
    for (int file = 0; file < 8; ++file) {
      Piece piece = mailbox[makeSquare(rank, file)];
      if (piece == Piece::NONE) {
        emptyCount++;
      } else {
        if (emptyCount > 0) {
          fen += std::to_string(emptyCount);
          emptyCount = 0;
        }
        fen += piece::pieceToChar(piece);
      }
    }
  }
  if (emptyCount > 0)
    fen += std::to_string(emptyCount);

  // Active color
  fen += ' ';
  fen += piece::colorToChar(currentTurn);

  // Castling availability
  if (state != nullptr)
    fen += " " + utils::castlingRightsToString(state->castlingRights);
  else
    fen += " KQkq";

  // En passant target square
  if (state != nullptr && state->epSquare != SQ_NONE) {
    fen += ' ';
    fen += utils::fileChar(fileOf(state->epSquare));
    fen += utils::rankCharFromRank(rankOf(state->epSquare));
  } else {
    fen += " -";
  }

  // Halfmove clock
  if (state != nullptr)
    fen += " " + std::to_string(state->halfmoveClock);
  else
    fen += " 0";

  // Fullmove number
  if (state != nullptr)
    fen += " " + std::to_string(state->fullmoveClock);
  else
    fen += " 1";

  return fen;
}

void fenToBoard(const std::string& fen, BitboardSet& bb, Piece mailbox[],
                Color& currentTurn, PositionState* state) {
  // Lenient parser — assumes the caller has already accepted the FEN via
  // fen::validateFEN().  Does not re-check field counts, EP-square rank,
  // halfmove/fullmove syntax, or piece-placement well-formedness.  Invalid
  // input is accepted as best-effort partial state, which is safe only when
  // upstream validation has occurred.  Public callers (UCI, web API, file
  // load) MUST gate this with validateFEN() first.
  using namespace LibreChess;
  std::string remaining = fen;
  std::string boardPart = nextToken(remaining);

  // Clear board
  bb.clear();
  memset(mailbox, 0, 64 * sizeof(Piece));

  // Parse ranks (rank 8 first, rank 1 last)
  int rank = 7;
  int file = 0;
  for (size_t i = 0; i < boardPart.length() && rank >= 0; i++) {
    char c = boardPart[i];
    if (c == '/') {
      rank--;
      file = 0;
    } else if (c >= '1' && c <= '8') {
      file += c - '0';
    } else {
      Piece p = piece::charToPiece(c);
      if (p != Piece::NONE && rank >= 0 && file >= 0 && file < 8) {
        Square sq = makeSquare(rank, file);
        bb.setPiece(sq, p);
        mailbox[sq] = p;
        file++;
      }
    }
  }
  if (state != nullptr)
    *state = PositionState{};

  // Active color
  std::string activeColor = nextToken(remaining);
  if (!activeColor.empty())
    currentTurn = (activeColor == "b" || activeColor == "B") ? Color::BLACK : Color::WHITE;

  // Castling rights
  std::string castlingStr = nextToken(remaining);
  if (!castlingStr.empty() && state != nullptr)
    state->castlingRights = utils::castlingRightsFromString(castlingStr);

  // En passant target square
  std::string enPassantSquare = nextToken(remaining);
  if (!enPassantSquare.empty() && enPassantSquare != "-" && enPassantSquare.length() >= 2
      && state != nullptr) {
    Square epSq;
    if (utils::parseSquareAlgebraic(enPassantSquare[0], enPassantSquare[1], epSq))
      state->epSquare = epSq;
  }

  // Halfmove clock
  std::string halfmoveStr = nextToken(remaining);
  if (!halfmoveStr.empty() && halfmoveStr.size() <= 9 && state != nullptr)
    state->halfmoveClock = std::stoi(halfmoveStr);

  // Fullmove number
  std::string fullmoveStr = nextToken(remaining);
  if (!fullmoveStr.empty() && fullmoveStr.size() <= 9 && state != nullptr) {
    int fullmove = std::stoi(fullmoveStr);
    state->fullmoveClock = fullmove > 0 ? fullmove : 1;
  }
}

// ---------------------------------------------------------------------------
// FEN validation sub-routines
//
// Each validator checks one field of a Forsyth–Edwards Notation string.
// Together they form the complete syntax check for validateFEN().
//
// Reference: https://www.chessprogramming.org/Forsyth-Edwards_Notation
// ---------------------------------------------------------------------------

// Validate the board (piece placement) field.
// Must contain exactly 7 '/' separators.  Each rank must sum to 8 squares
// and contain only valid piece characters (rnbqkpRNBQKP) and digits 1-8.
// Must also contain exactly one white king ('K') and one black king ('k'):
// positions without two kings are illegal per FIDE rules, and Position::
// kingSq() / attack detection assume every side has exactly one king.
static bool validateBoardField(const std::string& boardPart) {
  int slashCount = 0;
  for (char c : boardPart) {
    if (c == '/') slashCount++;
  }
  if (slashCount != 7) return false;

  int col = 0;
  int whiteKings = 0;
  int blackKings = 0;
  for (char c : boardPart) {
    if (c == '/') {
      if (col != 8) return false;
      col = 0;
    } else if (c >= '1' && c <= '8') {
      col += c - '0';
    } else if (std::strchr("rnbqkpRNBQKP", c)) {
      if (c == 'K') ++whiteKings;
      else if (c == 'k') ++blackKings;
      col++;
    } else {
      return false;
    }
  }
  if (col != 8) return false;
  // Exactly one king per side is required by FIDE rules and assumed
  // throughout the move generator and attack detector.
  return whiteKings == 1 && blackKings == 1;
}

// Validate the active color field.  Must be exactly "w" or "b".
static bool validateTurnField(const std::string& field) {
  return field == "w" || field == "b";
}

// Validate the castling availability field.
// Must be "-" or a combination of K, Q, k, q with no duplicates.
static bool validateCastlingField(const std::string& field) {
  if (field == "-") return true;
  uint8_t seen = 0;
  for (char c : field) {
    uint8_t bit = utils::castlingCharToBit(c);
    if (!bit) return false;
    if (seen & bit) return false;
    seen |= bit;
  }
  return true;
}

// Validate the en passant target square field.
// Must be "-" or a two-character algebraic square on rank 3 or 6.
static bool validateEPField(const std::string& field) {
  if (field == "-") return true;
  if (field.length() != 2) return false;
  if (field[0] < 'a' || field[0] > 'h') return false;
  return field[1] == '3' || field[1] == '6';
}

// Validate a clock field (halfmove clock or fullmove number).
// Must be a non-empty string of digits that fits in an int.
static bool validateClockField(const std::string& field) {
  if (field.empty()) return false;
  for (char c : field) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  // Reject absurdly long numeric strings that would overflow std::stoi.
  if (field.size() > 9) return false;
  return true;
}

bool validateFEN(const std::string& fen) {
  // Strict syntax checker.  Always call this before fenToBoard() \u2014 the
  // latter is a lenient parser that assumes well-formed input.  The
  // two-step pattern keeps the hot path cheap (parse) while still
  // protecting boundaries (validate) when needed.
  if (fen.empty()) return false;

  // Extract board part (before first space)
  size_t spacePos = fen.find(' ');
  std::string boardPart = (spacePos != std::string::npos) ? fen.substr(0, spacePos) : fen;

  if (!validateBoardField(boardPart)) return false;

  // Remaining fields are optional — if only the board part is given, accept it
  if (spacePos == std::string::npos || spacePos + 1 >= fen.size()) return true;

  std::string remaining = fen.substr(spacePos + 1);

  // Turn field
  std::string turnField = nextToken(remaining);
  if (!validateTurnField(turnField)) return false;

  // Castling field (optional from here)
  if (remaining.empty()) return true;
  if (!validateCastlingField(nextToken(remaining))) return false;

  // En passant field
  if (remaining.empty()) return true;
  if (!validateEPField(nextToken(remaining))) return false;

  // Halfmove clock
  if (remaining.empty()) return true;
  if (!validateClockField(nextToken(remaining))) return false;

  // Fullmove number
  if (remaining.empty()) return true;
  std::string fullmoveField = nextToken(remaining);
  if (!validateClockField(fullmoveField)) return false;
  if (std::stoi(fullmoveField) < 1) return false;

  return true;
}

}  // namespace fen
}  // namespace LibreChess
