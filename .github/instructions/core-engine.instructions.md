---
applyTo: "lib/core/src/engine.*"
description: "Engine facade: search resource ownership and invocation. Composed by UCIState and Game."
---

# Engine (`lib/core/src/engine.h/cpp`)

Search resource ownership facade — centralises `TranspositionTable`, `PawnHashTable`, `EvalHashTable`, and `SearchState` lifecycle. Provides a minimal API to run the search engine on an externally-owned `Position`.

## Public API

```cpp
class Engine {
  explicit Engine(int ttSize = search::DEFAULT_TT_SIZE);
  ~Engine();

  search::SearchResult calculateMove(Position& pos,
                                     const search::SearchLimits& limits,
                                     search::InfoCallback info = nullptr);
  void setTimeFunc(search::TimeFunc fn);
  void setExternalStop(std::atomic<bool>* flag);
  void clearState();
  void resizeTT(int entries);

  search::SearchState& searchState();
  const search::SearchState& searchState() const;
};
```

- `Engine(ttSize)` — allocates and sizes TT, pawn hash, eval hash, and constructs `SearchState` with pointers to all three.
- `calculateMove(pos, limits, info)` — wires stop flag (external if set, else internal), delegates to `search::findBestMove()`.
- `clearState()` — clears all hash tables + search heuristics. Called by UCI `ucinewgame` and `Game::newGame()`.
- `resizeTT(entries)` — frees old TT and allocates new. Used by UCI `setoption name Hash`.
- `searchState()` — direct access for consumers needing fine-grained control (book settings, etc.).

## Design Decisions

- **Does not own Position** — different consumers own Position differently (UCIState owns it as a member, Game owns it internally). Engine takes Position by reference to `calculateMove()`.
- **Value-composable and heap-composable** — UCIState holds `Engine engine` by value; Game holds `Engine* engine_` on heap (optional, allocated only for bot mode).
- **Non-copyable, non-movable** — owns hash table resources (raw arrays via `HashTableBase`).
- **Stop flag wiring** — internal `searchStop_` atomic is the default; `setExternalStop()` overrides it. UCI wires `UCIState::stop`; Game/provider wires a FreeRTOS cancel flag.
- **Re-entry guard** — `busy_` atomic flag protects `calculateMove()`, `clearState()`, and `resizeTT()` from concurrent invocation (e.g., firmware UI thread asking to clear state while a search task is running). Acquisition is wrapped in a file-local `BusyGuard` RAII helper (anonymous namespace in `engine.cpp`) that does a single acquire-CAS and releases on destruction. If acquisition fails (another caller holds the flag), the method returns immediately — no blocking, no queuing.
- **No Arduino dependencies** — pure C++, same as all core library code.

## Consumers

| Consumer | Ownership | Stop Flag Source |
|----------|-----------|------------------|
| `UCIState` (uci.h) | Value member (`Engine engine`) | `UCIState::stop` (set by `stop` command) |
| `Game` (game.h) | Heap pointer (`Engine* engine_`) | Provider's cancel flag via `setExternalStop()` |

## Resource Lifecycle

| Resource | Created | Cleared | Freed |
|----------|---------|---------|-------|
| `TranspositionTable` | Constructor | `clearState()` | Destructor |
| `PawnHashTable` | Constructor | `clearState()` | Destructor |
| `EvalHashTable` | Constructor | `clearState()` | Destructor |
| `SearchState` | Constructor | `clearState()` (heuristics only) | Destructor (automatic) |

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `search.instructions.md` | `findBestMove()` — the actual search entry point |
| `uci.instructions.md` | UCIState composes Engine |
| `game.instructions.md` | Game optionally composes Engine |
| `core.instructions.md` | Parent library conventions |
