---
applyTo: "lib/core/src/time_management.*"
description: "Time management: computeTimeLimits() pure function converting UCI clock parameters to SearchLimits."
---

# Time Management (`lib/core/src/time_management.h`)

Header-only, single pure function. Converts UCI clock parameters into `SearchLimits` for the search.

## Public API

```cpp
namespace time_management {
  SearchLimits computeTimeLimits(int wtime, int btime, int winc, int binc,
                                 int movestogo, Color sideToMove);
}
```

**Parameters** (all in milliseconds, from UCI `go` command):
- `wtime/btime` — remaining time for white/black
- `winc/binc` — increment per move
- `movestogo` — moves until next time control (0 = sudden death)
- `sideToMove` — which side's clock to use

**Returns**: `SearchLimits` with `softTime` and `hardTime` set, `timeManaged = true`.

## Formula

```
remaining = (sideToMove == WHITE) ? wtime : btime
increment = (sideToMove == WHITE) ? winc  : binc
base      = movestogo > 0 ? remaining / movestogo : remaining / 30
softTime  = base + increment / 2
hardTime  = min(remaining - MOVE_OVERHEAD, softTime * HARD_TIME_MULTIPLIER)
```

Constants: `MOVE_OVERHEAD = 50ms`, `HARD_TIME_MULTIPLIER = 3`.

## Design Decisions

- **Header-only** — single function, no state, no `.cpp` file needed.
- **Pure function** — no side effects, easy to unit test and reuse.
- **Called from two places**: `uci::cmdGo()` (CLI) and potentially `LibreChessProvider` (firmware, if time-based play is added).
- **`movestogo = 0` means sudden death** — uses `remaining / 30` as a reasonable default.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `uci.instructions.md` | `cmdGo()` calls `computeTimeLimits()` when wtime/btime are present |
| `search.instructions.md` | `SearchLimits` struct is the output target |
| `core.instructions.md` | Parent library conventions |
