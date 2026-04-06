#include "notation.h"

#include <cctype>

#include "move.h"
#include "utils.h"
#include "movegen.h"

using namespace LibreChess;

namespace LibreChess {
namespace notation {

// Strip trailing '+' and '#' check/checkmate suffixes in place.
static void stripCheckSuffix(std::string& s) {
  while (!s.empty() && (s.back() == '+' || s.back() == '#'))
    s.pop_back();
}

// Castling notation: kingside "O-O", queenside "O-O-O".
static std::string castlingNotation(const MoveEntry& move) {
  return (fileOf(move.to) > fileOf(move.from)) ? "O-O" : "O-O-O";
}

// ---------------------------------------------------------------------------
// Output — Coordinate notation
// ---------------------------------------------------------------------------

std::string toCoordinate(Square from, Square to, char promotion) {
  std::string move = utils::squareName(from);
  move += utils::squareName(to);

  if (promotion != ' ' && promotion != '\0') {
    move += static_cast<char>(tolower(promotion));
  }

  return move;
}

// ---------------------------------------------------------------------------
// Output — Long Algebraic Notation
// ---------------------------------------------------------------------------

std::string toLAN(const MoveEntry& move) {
  // Castling
  if (move.isCastling()) {
    return castlingNotation(move);
  }

  std::string result;
  PieceType type = piece::pieceType(move.piece);

  // Piece prefix (omit for pawns)
  if (type != PieceType::PAWN) {
    result += piece::pieceTypeToChar(type);
  }

  // Origin square
  result += utils::squareName(move.from);

  // Separator: 'x' for captures, '-' otherwise
  result += move.isCapture() ? 'x' : '-';

  // Destination square
  result += utils::squareName(move.to);

  // Promotion suffix
  if (move.isPromotion() && move.promotion != Piece::NONE) {
    result += '=';
    result += piece::pieceTypeToChar(piece::pieceType(move.promotion));
  }

  return result;
}

// ---------------------------------------------------------------------------
// SAN helpers — shared by toSAN (output) and parseSAN (input)
// ---------------------------------------------------------------------------

// Check if a character is a piece letter (NBRQK — not P, pawns are implicit)
static bool isPieceLetter(char c) {
  return c == 'N' || c == 'B' || c == 'R' || c == 'Q' || c == 'K';
}

// ---------------------------------------------------------------------------
// SAN promotion extraction — strips "=X" promotion suffix from the SAN
// string and returns the lowercase promotion character (or ' ' if none).
//
// SAN encodes promotion as "=Q", "=R", "=B", or "=N" appended after the
// destination square (e.g. "e8=Q", "exd1=N").
//
// Reference: https://www.chessprogramming.org/Algebraic_Chess_Notation#SAN
// ---------------------------------------------------------------------------
static char extractSANPromotion(std::string& s) {
  if (s.size() >= 2 && s[s.size() - 2] == '=') {
    char promo = s.back();
    if (utils::isValidPromotionChar(promo)) {
      char result = static_cast<char>(tolower(promo));
      s.erase(s.size() - 2, 2);
      return result;
    }
  }
  return ' ';
}

// ---------------------------------------------------------------------------
// SAN disambiguation — extracts file and/or rank hints from the characters
// preceding the destination square.
//
// In SAN, when two or more pieces of the same type can move to the same
// square, the origin is disambiguated with:
//   • file letter if pieces are on different files (e.g. Rae1)
//   • rank digit  if pieces share the same file    (e.g. R1e1)
//   • both        if neither alone is sufficient    (e.g. Qa1e1)
//
// Reference: https://www.chessprogramming.org/Algebraic_Chess_Notation#Disambiguation
// ---------------------------------------------------------------------------
static void parseSANDisambiguation(const std::string& hints,
                                   int& hintFile, int& hintRank) {
  hintFile = -1;
  hintRank = -1;
  for (char c : hints) {
    if (c >= 'a' && c <= 'h')
      hintFile = utils::fileIndex(c);
    else if (c >= '1' && c <= '8')
      hintRank = utils::rankIndexFromChar(c);
  }
}

// ---------------------------------------------------------------------------
// SAN piece resolution — searches the board for the unique piece that
// matches the SAN description: type, color, optional file/rank hints,
// and a legal move to the destination square.
//
// Returns true if exactly one match is found (sets fromRow/fromCol).
// Returns false on zero or multiple matches (ambiguous or illegal SAN).
//
// Reference: https://www.chessprogramming.org/Algebraic_Chess_Notation#SAN
// ---------------------------------------------------------------------------
static bool findMatchingPiece(const BitboardSet& bb, const Piece mailbox[],
                              const PositionState& state, Color currentTurn,
                              PieceType targetType, int hintFile, int hintRank,
                              Square toSq, Square& fromSq) {
  Square matchSq = SQ_NONE;
  int matchCount = 0;

  utils::forEachPiece(bb, mailbox, [&](Square sq, Piece p) {
    if (piece::pieceType(p) != targetType) return;
    if (piece::pieceColor(p) != currentTurn) return;
    if (hintFile >= 0 && fileOf(sq) != hintFile) return;
    if (hintRank >= 0 && rankOf(sq) != hintRank) return;
    if (!movegen::isValidMove(bb, mailbox, sq, toSq, state)) return;
    matchSq = sq;
    ++matchCount;
  });

  if (matchCount != 1) return false;
  fromSq = matchSq;
  return true;
}

// ---------------------------------------------------------------------------
// SAN disambiguation (output) — determines whether the origin file, rank,
// or both must be included when formatting a piece move in SAN.
//
// Scans the board for other pieces of the same type and color that could
// also legally move to the same destination square.
//
// Reference: https://www.chessprogramming.org/Algebraic_Chess_Notation#Disambiguation
// ---------------------------------------------------------------------------
static void computeDisambiguation(const BitboardSet& bb, const Piece mailbox[],
                                  const PositionState& state,
                                  const MoveEntry& move,
                                  bool& needFile, bool& needRank) {
  PieceType type = piece::pieceType(move.piece);
  Color color = piece::pieceColor(move.piece);
  needFile = false;
  needRank = false;

  utils::forEachPiece(bb, mailbox, [&](Square sq, Piece other) {
    if (sq == move.from) return;
    if (piece::pieceType(other) != type) return;
    if (piece::pieceColor(other) != color) return;
    if (!movegen::isValidMove(bb, mailbox, sq, move.to, state))
      return;
    if (fileOf(sq) != fileOf(move.from))
      needFile = true;
    else
      needRank = true;
  });
}

// ---------------------------------------------------------------------------
// Output — Standard Algebraic Notation
// ---------------------------------------------------------------------------

// Piece letter for SAN (uppercase, omit for pawns).
static char sanPieceLetter(Piece piece) {
  PieceType type = piece::pieceType(piece);
  return (type == PieceType::PAWN) ? '\0' : piece::pieceTypeToChar(type);
}

std::string toSAN(const BitboardSet& bb, const Piece mailbox[],
                  const PositionState& state, const MoveEntry& move) {
  // Castling
  if (move.isCastling()) {
    return castlingNotation(move);
  }

  std::string result;
  PieceType type = piece::pieceType(move.piece);
  Color color = piece::pieceColor(move.piece);

  if (type == PieceType::PAWN) {
    // Pawn moves
    if (move.isCapture()) {
      // Capture: file of origin + 'x' + destination
      result += utils::fileChar(fileOf(move.from));
      result += 'x';
    }
    result += utils::squareName(move.to);

    if (move.isPromotion() && move.promotion != Piece::NONE) {
      result += '=';
      result += piece::pieceTypeToChar(piece::pieceType(move.promotion));
    }
  } else {
    // Piece moves — may require disambiguation
    result += piece::pieceTypeToChar(type);

    bool needFile = false;
    bool needRank = false;
    computeDisambiguation(bb, mailbox, state, move, needFile, needRank);

    if (needFile) result += utils::fileChar(fileOf(move.from));
    if (needRank) result += utils::rankCharFromRank(rankOf(move.from));

    if (move.isCapture()) result += 'x';

    result += utils::squareName(move.to);
  }

  return result;
}

// ---------------------------------------------------------------------------
// Input — Coordinate notation
// ---------------------------------------------------------------------------

bool parseCoordinate(const std::string& move,
                     Square& from, Square& to,
                     char& promotion) {
  size_t len = move.length();
  if (len < 4 || len > 5) return false;

  char fromFile = move[0];
  char fromRank = move[1];
  char toFile = move[2];
  char toRank = move[3];

  int ff = utils::fileIndex(fromFile);
  int fr = utils::rankIndexFromChar(fromRank);
  int tf = utils::fileIndex(toFile);
  int tr = utils::rankIndexFromChar(toRank);

  if ((unsigned)ff >= 8 || (unsigned)fr >= 8 ||
      (unsigned)tf >= 8 || (unsigned)tr >= 8)
    return false;

  from = makeSquare(fr, ff);
  to   = makeSquare(tr, tf);

  // From and to squares must differ
  if (from == to) return false;

  // Promotion (optional 5th char) must be q, r, b, or n
  promotion = ' ';
  if (len == 5) {
    char promo = tolower(move[4]);
    if (!utils::isValidPromotionChar(promo)) return false;
    promotion = move[4];
  }

  return true;
}

// ---------------------------------------------------------------------------
// Input — Long Algebraic Notation
// ---------------------------------------------------------------------------

bool parseLAN(const std::string& move,
              Square& from, Square& to,
              char& promotion) {
  if (move.empty()) return false;

  // Handle castling: LAN lacks board context needed to resolve the king's
  // position, so we return false here.  The auto-detect entry point
  // parseMove() tries LAN first; when it returns false for castling
  // notation, it falls through to parseSAN which has board context.
  // Coordinate-style castling (e.g. e1g1) is handled by parseCoordinate.
  std::string upper;
  for (char c : move) upper += static_cast<char>(toupper(c));
  if (upper == "O-O" || upper == "0-0" ||
      upper == "O-O-O" || upper == "0-0-0") {
    return false;
  }

  // Strip check/checkmate suffixes
  std::string cleaned = move;
  stripCheckSuffix(cleaned);

  // Strip promotion marker (=Q, =q, etc.)
  promotion = ' ';
  if (cleaned.size() >= 2 && cleaned[cleaned.size() - 2] == '=') {
    char promo = tolower(cleaned.back());
    if (utils::isValidPromotionChar(promo)) {
      promotion = cleaned.back();
      cleaned.erase(cleaned.size() - 2, 2);
    }
  }

  // Strip piece prefix (uppercase letters A-Z at start that aren't file letters
  // in context — the first char is a piece if followed by a file letter)
  if (!cleaned.empty() && isupper(cleaned[0])) {
    char next = cleaned.size() > 1 ? cleaned[1] : 0;
    // Piece prefix if next char is a file letter (a-h)
    if (next >= 'a' && next <= 'h') {
      cleaned.erase(0, 1);
    }
  }

  // Remove separator characters ('-' and 'x')
  std::string coords;
  for (char c : cleaned) {
    if (c != '-' && c != 'x') {
      coords += c;
    }
  }

  // Now we should have pure coordinate notation (e.g., "e2e4")
  // Re-attach promotion if we stripped it
  if (promotion != ' ') {
    coords += promotion;
  }

  return parseCoordinate(coords, from, to, promotion);
}

// ---------------------------------------------------------------------------
// Input — Standard Algebraic Notation
// ---------------------------------------------------------------------------

// Find and validate a castling move for the given side.
static bool findCastlingMove(const BitboardSet& bb, const Piece mailbox[],
                             const PositionState& state,
                             Color currentTurn, bool kingSide,
                             Square& from, Square& to,
                             char& promotion) {
  int rank = piece::homeRank(currentTurn);
  Square kingSq = makeSquare(rank, 4);
  Piece king = mailbox[kingSq];
  if (piece::pieceType(king) != PieceType::KING || piece::pieceColor(king) != currentTurn)
    return false;
  if (!utils::hasCastlingRight(state.castlingRights, currentTurn, kingSide))
    return false;
  from = kingSq;
  to   = makeSquare(rank, kingSide ? 6 : 2);
  promotion = ' ';
  return movegen::isValidMove(bb, mailbox, from, to, state);
}

bool parseSAN(const BitboardSet& bb, const Piece mailbox[],
              const PositionState& state,
              Color currentTurn, const std::string& san,
              Square& from, Square& to,
              char& promotion) {
  if (san.empty()) return false;

  // Handle castling
  std::string upper;
  for (char c : san) upper += static_cast<char>(toupper(c));
  // Strip check suffixes for castling comparison
  std::string castleStr = upper;
  stripCheckSuffix(castleStr);

  if (castleStr == "O-O" || castleStr == "0-0")
    return findCastlingMove(bb, mailbox, state, currentTurn, true, from, to, promotion);

  if (castleStr == "O-O-O" || castleStr == "0-0-0")
    return findCastlingMove(bb, mailbox, state, currentTurn, false, from, to, promotion);

  // Strip check/checkmate suffixes
  std::string s = san;
  stripCheckSuffix(s);
  if (s.empty()) return false;

  // Extract promotion suffix (e.g. "=Q")
  promotion = extractSANPromotion(s);

  // Determine piece type
  char pieceTypeChar;
  if (isPieceLetter(s[0])) {
    pieceTypeChar = s[0];
    s.erase(0, 1);
  } else {
    pieceTypeChar = 'P';
  }
  PieceType targetType = piece::charToPieceType(pieceTypeChar);

  // Remove capture marker
  std::string stripped;
  for (char c : s) {
    if (c != 'x') stripped += c;
  }
  s = stripped;

  if (s.size() < 2) return false;

  // Last two chars are the destination square
  char destFile = s[s.size() - 2];
  char destRank = s[s.size() - 1];
  if (destFile < 'a' || destFile > 'h' || destRank < '1' || destRank > '8')
    return false;

  to = makeSquare(utils::rankIndexFromChar(destRank), utils::fileIndex(destFile));

  // Parse disambiguation hints
  int hintFile = -1, hintRank = -1;
  parseSANDisambiguation(s.substr(0, s.size() - 2), hintFile, hintRank);

  // Find the unique matching piece
  return findMatchingPiece(bb, mailbox, state, currentTurn, targetType,
                           hintFile, hintRank, to, from);
}

// ---------------------------------------------------------------------------
// Input — Auto-detect format
// ---------------------------------------------------------------------------

// Quick check: is this string coordinate notation? (4-5 chars: [a-h][1-8][a-h][1-8][qrbn]?)
static bool looksLikeCoordinate(const std::string& move) {
  size_t len = move.length();
  if (len < 4 || len > 5) return false;
  if (move[0] < 'a' || move[0] > 'h') return false;
  if (move[1] < '1' || move[1] > '8') return false;
  if (move[2] < 'a' || move[2] > 'h') return false;
  if (move[3] < '1' || move[3] > '8') return false;
  if (len == 5) {
    if (!utils::isValidPromotionChar(move[4])) return false;
  }
  return true;
}

// Quick check: does the string contain LAN-specific separators?
static bool looksLikeLAN(const std::string& move) {
  // LAN has '-' or 'x' between squares, often with a piece prefix
  // Must have at least 5 chars (e2-e4) and contain '-' or 'x' with
  // file-rank patterns on both sides
  for (size_t i = 0; i < move.size(); ++i) {
    if (move[i] == '-' && i > 0) return true;
  }
  // Check for piece prefix + squares (e.g., "Ng1xf3")
  if (move.size() >= 5 && isPieceLetter(move[0])) {
    // Check if char at position 3 or 4 is 'x'
    for (size_t i = 2; i < move.size() && i < 5; ++i) {
      if (move[i] == 'x') return true;
    }
  }
  return false;
}

bool parseMove(const BitboardSet& bb, const Piece mailbox[],
               const PositionState& state,
               Color currentTurn, const std::string& move,
               Square& from, Square& to,
               char& promotion) {
  if (move.empty()) return false;

  // 1. Try coordinate notation first (fastest, most common in UCI protocol)
  if (looksLikeCoordinate(move)) {
    if (parseCoordinate(move, from, to, promotion))
      return true;
  }

  // 2. Try LAN (has separators like '-' or piece prefix with 'x')
  if (looksLikeLAN(move)) {
    if (parseLAN(move, from, to, promotion))
      return true;
  }

  // 3. Try SAN (requires board context)
  if (parseSAN(bb, mailbox, state, currentTurn, move, from, to, promotion))
    return true;

  return false;
}

}  // namespace notation
}  // namespace LibreChess
