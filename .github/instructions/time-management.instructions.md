---
applyTo: "lib/core/src/time_management.*"
description: "Time management: computeTimeLimits() pure function converting UCI clock parameters to SearchLimits."
---

# Time Management (`lib/core/src/time_management.h`)

Header-only, single pure function. Converts UCI clock parameters into `SearchLimits` for the search.

## Public API

```cpp
namespace time_management {
  SearchLimits computeTimeLimits(uint32_t wtime, uint32_t btime,
                                 uint32_t winc, uint32_t binc,
                                 int movestogo, Color sideToMove);
}
```

**Parameters** (all in milliseconds, from UCI `go` command):
- `wtime/btime` — remaining time for white/black
- `winc/binc` — increment per move
- `movestogo` — moves until next time control (0 = sudden death)
- `sideToMove` — which side's clock to use

**Returns**: `SearchLimits` with `softTimeMs` and `hardTimeMs` set. `maxDepth` and `stop` keep their default values so callers can combine time controls with depth/stop policy.

## Formula

```
remaining     = (sideToMove == WHITE) ? wtime : btime
increment     = (sideToMove == WHITE) ? winc  : binc
safeRemaining = max(1, remaining - MOVE_OVERHEAD)
softTime      = movestogo > 0
              ? safeRemaining / movestogo + increment
              : safeRemaining / 30 + increment / 2
hardTime      = min(max(1, safeRemaining / 4), softTime * HARD_TIME_MULTIPLIER)
softTime      = min(softTime, hardTime, safeRemaining)
hardTime      = min(hardTime, safeRemaining)
```

Constants: `MOVE_OVERHEAD = 50ms`, `HARD_TIME_MULTIPLIER = 4`.

## Design Decisions

- **Header-only** — single function, no state, no `.cpp` file needed.
- **Pure function** — no side effects, easy to unit test and reuse.
- **Saturating/bounded arithmetic** — all public clock inputs are `uint32_t`, internal math uses `uint64_t`, and zero-clock/pathological inputs still produce at least `softTimeMs=1`, `hardTimeMs=1`.
- **Called from two places**: `uci::cmdGo()` (CLI) and potentially `LibreChessEngine` (firmware, if time-based play is added).
- **`movestogo = 0` means sudden death** — uses `remaining / 30` as a reasonable default.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `uci.instructions.md` | `cmdGo()` calls `computeTimeLimits()` when wtime/btime are present |
| `search.instructions.md` | `SearchLimits` struct is the output target |
| `core.instructions.md` | Parent library conventions |
