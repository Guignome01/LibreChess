---
applyTo: "lib/core/src/uci.*"
description: "UCI protocol handler: UCIState resource bundle, loop/processLine free functions, command dispatch."
---

# UCI Protocol Handler (`lib/core/src/uci.h/cpp`)

Lean UCI command dispatcher for the LibreChess engine. Two entry points, one state struct, no classes.

## Public API

```cpp
struct UCIState {
  Position pos;
  Engine engine;
  std::atomic<bool> stop{false};
  explicit UCIState(TimeFunc timeFunc = nullptr, int ttSize = DEFAULT_TT_SIZE);
};

void loop(UCIState& state, FILE* in, FILE* out);
bool processLine(UCIState& state, const char* line, std::string& output);
```

- `UCIState` — resource bundle owning Position and Engine (search resources). Non-copyable, non-movable. The `Engine` member owns TT, pawn hash, eval hash, and SearchState. The external stop flag (`stop`) is wired to the engine at construction.
- `loop()` — blocking stdin/stdout loop for the native CLI executable. Exits on `quit` or EOF.
- `processLine()` — pure string-in/string-out for unit tests. Returns false on `quit`.

## Supported Commands

| Command | Handler | Notes |
|---------|---------|-------|
| `uci` | `cmdUci` | Prints `id name`, `id author`, `option Hash`, `uciok` |
| `isready` | inline | Prints `readyok` |
| `setoption name Hash value N` | `cmdSetOption` | Resizes TT (1–256 MB) |
| `ucinewgame` | `cmdNewGame` | Clears TT, hash tables, heuristics, loads starting position |
| `position [startpos|fen ...] [moves ...]` | `cmdPosition` | Loads position + applies moves |
| `go [depth N|movetime N|wtime/btime/winc/binc/movestogo|infinite]` | `cmdGo` | Runs search, outputs `info` lines + `bestmove` |
| `quit` | inline | Returns false to exit |

## Design Decisions

- **Engine composition** — `UCIState` composes `Engine` as a value member, delegating TT/hash/SearchState ownership. `cmdGo` → `engine.calculateMove()`, `cmdNewGame` → `engine.clearState()`, `cmdSetOption Hash` → `engine.resizeTT()`. UCIState destructor is defaulted (Engine cleans up its own resources).
- **Token parsing** — `nextToken()`, `parseInt()`, `restOfLine()` helpers work on `const char*` with pointer advancement. No `std::istringstream`.
- **Info callback** — `thread_local std::string*` + `thread_local FILE*` bridge info output to either `FILE*` (CLI) or `std::string` (tests).
- **Author** — "LibreChess Contributors" (not a personal name).

## Three Paths to the Search

| Context | Path |
|---------|------|
| Native CLI (SPRT) | `stdin → uci::loop() → findBestMove() → stdout` |
| ESP32 firmware | `LibreChessProvider → Game::calculateMove() → findBestMove()` |
| UCI tests | `processLine(line, outputBuf) → verify string` |

## Testing

Mirror test file: `test/test_core/test_uci.cpp` (suite: `test_core`). 11 tests via `processLine()`. See `testing.instructions.md`.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `engine.instructions.md` | `Engine` facade composed by UCIState |
| `search.instructions.md` | `findBestMove()` is the actual search entry point |
| `time-management.instructions.md` | `computeTimeLimits()` converts `go wtime/btime` to `SearchLimits` |
| `core.instructions.md` | Parent library conventions |
| `testing.instructions.md` | Test architecture for `test_uci.cpp` |
