#include "uci.h"

#include <cstring>
#include <sstream>
#include <thread>

#include "fen.h"
#include "notation.h"
#include "time_management.h"

// ---------------------------------------------------------------------------
// UCI protocol handler — lean command dispatcher.
//
// Implements the Universal Chess Interface protocol for communication with
// external GUIs and testing tools (cutechess-cli, fastchess).
//
// Commands: uci, isready, setoption, ucinewgame, position, go, stop, quit.
// Reference: https://www.chessprogramming.org/UCI
// ---------------------------------------------------------------------------

namespace LibreChess {
namespace uci {

// ===========================================================================
// Construction / destruction
// ===========================================================================

UCIState::UCIState(search::TimeFunc timeFunc, int ttSize)
    : searchState(timeFunc, &tt, &pawnHash, &evalHash) {
  tt.resize(ttSize);
  pawnHash.resize(eval::DEFAULT_PAWN_HASH_SIZE);
  evalHash.resize(eval::DEFAULT_EVAL_HASH_SIZE);
  pos.loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

UCIState::~UCIState() {
  tt.free();
  pawnHash.free();
  evalHash.free();
}

// ===========================================================================
// Token parsing helpers
// ===========================================================================

// Advance `ptr` past whitespace, then return the next token (non-whitespace).
// Returns empty string at end-of-line.
static std::string nextToken(const char*& ptr) {
  while (*ptr == ' ' || *ptr == '\t') ++ptr;
  if (*ptr == '\0' || *ptr == '\n' || *ptr == '\r') return {};
  const char* start = ptr;
  while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && *ptr != '\r')
    ++ptr;
  return std::string(start, ptr);
}

// Parse an integer token.  Returns defaultVal if token is empty or invalid.
static int parseInt(const char*& ptr, int defaultVal = 0) {
  std::string tok = nextToken(ptr);
  if (tok.empty()) return defaultVal;
  try {
    return std::stoi(tok);
  } catch (...) {
    return defaultVal;
  }
}

// Return the rest of the line from the current pointer position (trimmed).
static std::string restOfLine(const char*& ptr) {
  while (*ptr == ' ' || *ptr == '\t') ++ptr;
  const char* start = ptr;
  const char* end = ptr + strlen(ptr);
  while (end > start && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
    --end;
  ptr = start + strlen(start);
  return std::string(start, end);
}

// ===========================================================================
// Command handlers
// ===========================================================================

// --- uci ---
static void cmdUci(std::string& output) {
  output += "id name LibreChess\n";
  output += "id author LibreChess Contributors\n";
  output += "option name Hash type spin default 4 min 1 max 256\n";
  output += "uciok\n";
}

// --- isready ---
static void cmdIsReady(std::string& output) {
  output += "readyok\n";
}

// --- setoption name <name> value <value> ---
static void cmdSetOption(UCIState& state, const char* ptr) {
  std::string tok = nextToken(ptr);  // "name"
  if (tok != "name") return;

  // Collect option name (may be multi-word, ends at "value" keyword)
  std::string name;
  while (true) {
    tok = nextToken(ptr);
    if (tok.empty() || tok == "value") break;
    if (!name.empty()) name += ' ';
    name += tok;
  }

  // Collect value
  std::string value = restOfLine(ptr);

  if (name == "Hash") {
    int mb = 4;
    try { mb = std::stoi(value); } catch (...) {}
    if (mb < 1) mb = 1;
    if (mb > 256) mb = 256;
    int entries = (mb * 1024 * 1024) / static_cast<int>(sizeof(search::TTEntry));
    state.tt.free();
    state.tt.resize(entries);
  }
}

// --- ucinewgame ---
static void cmdNewGame(UCIState& state) {
  state.tt.clear();
  state.pawnHash.clear();
  state.evalHash.clear();
  state.searchState.clearHeuristics();
  state.pos.loadFEN(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

// --- position [startpos | fen <fen>] [moves <move1> <move2> ...] ---
static void cmdPosition(UCIState& state, const char* ptr) {
  std::string tok = nextToken(ptr);

  if (tok == "startpos") {
    state.pos.loadFEN(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    tok = nextToken(ptr);  // Should be "moves" or empty
  } else if (tok == "fen") {
    // Collect FEN string (6 space-separated fields)
    std::string fen;
    for (int i = 0; i < 6; ++i) {
      std::string field = nextToken(ptr);
      if (field.empty()) break;
      if (i > 0) fen += ' ';
      fen += field;
    }
    if (!state.pos.loadFEN(fen)) return;
    tok = nextToken(ptr);  // Should be "moves" or empty
  } else {
    return;
  }

  // Apply moves
  if (tok == "moves") {
    while (true) {
      tok = nextToken(ptr);
      if (tok.empty()) break;
      Square from, to;
      char promo = ' ';
      if (notation::parseCoordinate(tok, from, to, promo)) {
        state.pos.makeMove(from, to, promo);
      }
    }
  }
}

// Info callback for iterative deepening — formats UCI info lines.
// The output FILE* is stored in a thread-local for the callback.
static thread_local FILE* g_infoOut = nullptr;

static void infoCallback(const search::SearchResult& result) {
  if (!g_infoOut) return;

  // Elapsed time — approximate from node count (no timeFunc in callback)
  const char* scoreType = (result.score >= search::MATE_SCORE - search::MAX_PLY)
                              ? "mate"
                              : "cp";
  int scoreVal = result.score;
  if (result.score >= search::MATE_SCORE - search::MAX_PLY) {
    scoreVal = (search::MATE_SCORE - result.score + 1) / 2;
  } else if (result.score <= -search::MATE_SCORE + search::MAX_PLY) {
    scoreVal = -(search::MATE_SCORE + result.score + 1) / 2;
  }

  fprintf(g_infoOut, "info depth %d score %s %d nodes %u",
          result.depth, scoreType, scoreVal, result.nodes);

  // PV line
  if (result.pvLength > 0) {
    fprintf(g_infoOut, " pv");
    for (int i = 0; i < result.pvLength; ++i) {
      char promo = ' ';
      if (result.pv[i].isPromotion()) {
        static const char promoChars[] = {'n', 'b', 'r', 'q'};
        promo = promoChars[result.pv[i].promoIndex()];
      }
      std::string moveStr = notation::toCoordinate(
          result.pv[i].from, result.pv[i].to, promo);
      fprintf(g_infoOut, " %s", moveStr.c_str());
    }
  }
  fprintf(g_infoOut, "\n");
  fflush(g_infoOut);
}

// Info callback for processLine — appends to a std::string instead of FILE*.
static thread_local std::string* g_infoStr = nullptr;

static void infoCallbackStr(const search::SearchResult& result) {
  if (!g_infoStr) return;

  const char* scoreType = (result.score >= search::MATE_SCORE - search::MAX_PLY)
                              ? "mate"
                              : "cp";
  int scoreVal = result.score;
  if (result.score >= search::MATE_SCORE - search::MAX_PLY) {
    scoreVal = (search::MATE_SCORE - result.score + 1) / 2;
  } else if (result.score <= -search::MATE_SCORE + search::MAX_PLY) {
    scoreVal = -(search::MATE_SCORE + result.score + 1) / 2;
  }

  char buf[512];
  int len = snprintf(buf, sizeof(buf), "info depth %d score %s %d nodes %u",
                     result.depth, scoreType, scoreVal, result.nodes);
  g_infoStr->append(buf, len);

  if (result.pvLength > 0) {
    *g_infoStr += " pv";
    for (int i = 0; i < result.pvLength; ++i) {
      char promo = ' ';
      if (result.pv[i].isPromotion()) {
        static const char promoChars[] = {'n', 'b', 'r', 'q'};
        promo = promoChars[result.pv[i].promoIndex()];
      }
      std::string moveStr = notation::toCoordinate(
          result.pv[i].from, result.pv[i].to, promo);
      *g_infoStr += ' ';
      *g_infoStr += moveStr;
    }
  }
  *g_infoStr += '\n';
}

// --- go [depth <d>] [movetime <t>] [wtime <w> btime <b> ...] [infinite] ---
static void cmdGo(UCIState& state, const char* ptr, std::string& output,
                  search::InfoCallback infoCb) {
  search::SearchLimits limits;
  uint32_t wtime = 0, btime = 0, winc = 0, binc = 0;
  int movestogo = 0;
  bool hasTime = false;

  while (true) {
    std::string tok = nextToken(ptr);
    if (tok.empty()) break;

    if (tok == "depth") {
      limits.maxDepth = parseInt(ptr, search::MAX_PLY);
    } else if (tok == "movetime") {
      uint32_t mt = static_cast<uint32_t>(parseInt(ptr, 0));
      limits.softTimeMs = mt;
      limits.hardTimeMs = mt;
    } else if (tok == "wtime") {
      wtime = static_cast<uint32_t>(parseInt(ptr, 0));
      hasTime = true;
    } else if (tok == "btime") {
      btime = static_cast<uint32_t>(parseInt(ptr, 0));
      hasTime = true;
    } else if (tok == "winc") {
      winc = static_cast<uint32_t>(parseInt(ptr, 0));
    } else if (tok == "binc") {
      binc = static_cast<uint32_t>(parseInt(ptr, 0));
    } else if (tok == "movestogo") {
      movestogo = parseInt(ptr, 0);
    } else if (tok == "infinite") {
      limits.maxDepth = search::MAX_PLY;
      // No time limit — runs until "stop"
    }
  }

  // Apply time management if clock parameters were given
  if (hasTime) {
    search::SearchLimits timeLimits =
        time_management::computeTimeLimits(wtime, btime, winc, binc,
                                           movestogo, state.pos.sideToMove());
    limits.softTimeMs = timeLimits.softTimeMs;
    limits.hardTimeMs = timeLimits.hardTimeMs;
  }

  // Wire stop flag
  state.stop.store(false, std::memory_order_relaxed);
  limits.stop = &state.stop;

  // Run search
  search::SearchResult result =
      search::findBestMove(state.pos, limits, state.searchState, infoCb);

  // Emit bestmove
  if (result.bestMove.from == 0 && result.bestMove.to == 0) {
    output += "bestmove 0000\n";
  } else {
    char promo = ' ';
    if (result.bestMove.isPromotion()) {
      static const char promoChars[] = {'n', 'b', 'r', 'q'};
      promo = promoChars[result.bestMove.promoIndex()];
    }
    std::string moveStr = notation::toCoordinate(
        result.bestMove.from, result.bestMove.to, promo);
    output += "bestmove " + moveStr + "\n";
  }
}

// ===========================================================================
// Public API
// ===========================================================================

bool processLine(UCIState& state, const char* line, std::string& output) {
  const char* ptr = line;
  std::string cmd = nextToken(ptr);

  if (cmd == "uci") {
    cmdUci(output);
  } else if (cmd == "isready") {
    cmdIsReady(output);
  } else if (cmd == "setoption") {
    cmdSetOption(state, ptr);
  } else if (cmd == "ucinewgame") {
    cmdNewGame(state);
  } else if (cmd == "position") {
    cmdPosition(state, ptr);
  } else if (cmd == "go") {
    g_infoStr = &output;
    cmdGo(state, ptr, output, infoCallbackStr);
    g_infoStr = nullptr;
  } else if (cmd == "stop") {
    state.stop.store(true, std::memory_order_relaxed);
  } else if (cmd == "quit") {
    return false;
  }
  // Unknown commands are silently ignored per UCI spec.

  return true;
}

void loop(UCIState& state, FILE* in, FILE* out) {
  char lineBuffer[8192];

  while (fgets(lineBuffer, sizeof(lineBuffer), in)) {
    // Strip trailing newline
    size_t len = strlen(lineBuffer);
    while (len > 0 && (lineBuffer[len - 1] == '\n' ||
                       lineBuffer[len - 1] == '\r'))
      lineBuffer[--len] = '\0';

    const char* ptr = lineBuffer;
    std::string cmd = nextToken(ptr);

    if (cmd == "uci") {
      std::string output;
      cmdUci(output);
      fputs(output.c_str(), out);
      fflush(out);
    } else if (cmd == "isready") {
      fputs("readyok\n", out);
      fflush(out);
    } else if (cmd == "setoption") {
      cmdSetOption(state, ptr);
    } else if (cmd == "ucinewgame") {
      cmdNewGame(state);
    } else if (cmd == "position") {
      cmdPosition(state, ptr);
    } else if (cmd == "go") {
      g_infoOut = out;
      std::string bestmoveOutput;
      cmdGo(state, ptr, bestmoveOutput, infoCallback);
      g_infoOut = nullptr;
      fputs(bestmoveOutput.c_str(), out);
      fflush(out);
    } else if (cmd == "stop") {
      state.stop.store(true, std::memory_order_relaxed);
    } else if (cmd == "quit") {
      break;
    }
    // Unknown commands silently ignored per UCI spec.
  }
}

}  // namespace uci
}  // namespace LibreChess
