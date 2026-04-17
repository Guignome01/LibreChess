#ifndef LIBRECHESS_UCI_H
#define LIBRECHESS_UCI_H

// ---------------------------------------------------------------------------
// UCI protocol handler — lean command dispatcher for the LibreChess engine.
//
// Two entry points:
//   loop()        — blocking stdin/stdout loop for the native CLI executable
//   processLine() — pure string-in/string-out for unit tests
//
// UCIState is a resource bundle owning Position, TT, hash tables, and
// SearchState — the same resources the old Engine facade held, without
// the non-standard API.
//
// No classes, no inheritance, no abstract streams.  Just free functions
// in a namespace, operating on a state struct.
//
// Reference: https://www.chessprogramming.org/UCI
// ---------------------------------------------------------------------------

#include <atomic>
#include <cstdio>
#include <string>

#include "engine.h"
#include "position.h"

namespace LibreChess {
namespace uci {

// ---------------------------------------------------------------------------
// UCIState — engine resource bundle.
//
// Owns Position and Engine (search resources).  The stop flag lives here
// because UCI stop commands arrive asynchronously from the search thread.
// Constructed once for the lifetime of the process (CLI) or test fixture.
// ---------------------------------------------------------------------------
struct UCIState {
  Position pos;
  Engine engine;
  std::atomic<bool> stop{false};

  // Construct with optional time function and TT size.
  explicit UCIState(search::TimeFunc timeFunc = nullptr,
                    int ttSize = search::DEFAULT_TT_SIZE);
  ~UCIState() = default;

  // Non-copyable, non-movable (owns heap resources).
  UCIState(const UCIState&) = delete;
  UCIState& operator=(const UCIState&) = delete;
};

// ---------------------------------------------------------------------------
// Blocking main loop — reads lines from `in`, dispatches commands, writes
// responses to `out`.  Returns on "quit" or EOF.
//
// For the native CLI: loop(state, stdin, stdout).
// For testing: not typically used (use processLine instead).
// ---------------------------------------------------------------------------
void loop(UCIState& state, FILE* in, FILE* out);

// ---------------------------------------------------------------------------
// Process a single UCI command line.  Appends response text to `output`.
// Returns false on "quit" (caller should stop).
//
// Pure string-in/string-out — no I/O, no global state.  Testable.
// ---------------------------------------------------------------------------
bool processLine(UCIState& state, const char* line, std::string& output);

}  // namespace uci
}  // namespace LibreChess

#endif  // LIBRECHESS_UCI_H
