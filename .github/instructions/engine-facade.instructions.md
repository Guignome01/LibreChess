---
applyTo: "lib/core/src/engine.*"
description: "Engine facade: owns Position + TT + stop control. Direct-call API: calculateMove(fen, limits) → SearchResult."
---

# Engine Facade (`lib/core/src/engine.h/cpp`)

Thin facade that composes `Position`, `TranspositionTable`, pawn/eval hash tables, and `search::findBestMove`. Provides the single entry point consumed by firmware.

## Public API

```cpp
class Engine {
public:
    Engine();                         // Allocates TT, pawn hash, eval hash, SearchState
    ~Engine();

    SearchResult calculateMove(const char* fen, const SearchLimits& limits);
    void setExternalStop(std::atomic<bool>* stopFlag);
    void setTimeFunc(search::TimeFunc fn);  // Sets state_.timeFunc directly
};
```

- `calculateMove` — parses FEN into internal Position, calls `findBestMove` (passing pre-allocated `SearchState` with infrastructure fields already wired), returns result
- `setExternalStop` — injects firmware's stop flag for cooperative cancellation (FreeRTOS task integration)
- `setTimeFunc` — sets the time function on `state_.timeFunc` directly (no separate `timeFunc_` member)
- TT persists across calls (same game benefits from TT retention); `newGeneration()` called per search

## Owned Resources

| Resource | Type | Purpose |
|----------|------|---------|
| `position_` | `Position` | Board state, parsed from FEN each call |
| `tt_` | `TranspositionTable*` | Heap-allocated, persists across calls |
| `pawnHash_` | Hash table pointer | Pawn structure cache, shared with eval |
| `evalHash_` | Hash table pointer | Full evaluation cache |
| `state_` | `SearchState` | Direct member (~10 KiB), reused across calls |
| `externalStop_` | `atomic<bool>*` | Cooperative cancellation from firmware |

## Design Notes

- **Position by value**: Each `calculateMove` call rebuilds from FEN. TT provides cross-call continuity.
- **Direct SearchState member**: `state_` is a direct `SearchState` member (not heap-allocated), reused across calls, eliminating per-search allocation. Initialized via member initializer list: `state_(nullptr, &tt_, &pawnHash_, &evalHash_)` — infrastructure pointers are wired at construction. `setTimeFunc` wires `state_.timeFunc` post-construction (firmware deferred setup). `findBestMove()` resets per-search transients (`nodes`, `stopped`) at the start of each search.
- **No game state**: Engine is stateless w.r.t. game lifecycle. Firmware manages game flow.
- **Dependency**: Never imports game library.
- **Platform function**: Uses `millis()` on ESP32, `nativeMillis()` on native (selected at compile time via `TIME_FUNC` / platform detection).

## Testing

Mirror test file: `test/test_core/test_engine.cpp` (suite: `test_core`). When changing the Engine facade, update tests in the same change. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `search.instructions.md` | Wraps `search::findBestMove()` — all search logic lives there |
| `position.instructions.md` | Owns a `Position` instance, parses FEN into it |
| `engine.instructions.md` | Firmware `LibreChessProvider` creates and drives `Engine` via FreeRTOS task |
| `testing.instructions.md` | Test architecture and `test_engine.cpp` group description |
