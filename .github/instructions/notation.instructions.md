---
applyTo: "lib/core/src/notation.*"
description: "Move notation: coordinate, SAN, LAN conversion. Square-native API. Use when editing notation.h or notation.cpp."
---

# Notation (`lib/core/src/notation.h/cpp`)

Square-native coordinate/SAN/LAN conversion. Stateless namespace — all context passed as decomposed parameters.

## Public API

**Output** (from `MoveEntry`):
- `toCoordinate(from, to, promo) → string` — "e2e4"
- `toLAN(MoveEntry) → string` — "e2-e4", "Ng1xf3"
- `toSAN(bb, mailbox, state, MoveEntry) → string` — "e4", "Nxf3", "O-O"

**Input** (parse to `Square`):
- `parseCoordinate(str, from, to, promo) → bool`
- `parseLAN(str, from, to, promo) → bool`
- `parseSAN(bb, mailbox, state, turn, str, from, to, promo) → bool`
- `parseMove(bb, mailbox, state, turn, str, from, to, promo) → bool` — auto-detect format

## Design Notes

- **Check/checkmate suffixes omitted** — `+`/`#` appended by `Game::getHistory()` via replay on temp board.
- **Decomposed params** — accepts `(BitboardSet&, Piece[], PositionState&)` to prevent circular dependency with `position.h`.
- **`Game` wrappers** bridge row/col for firmware.

## Testing

Mirror test file: `test/test_core/test_notation.cpp` (suite: `test_core`). When changing notation formats, update tests in the same change. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `movegen.instructions.md` | SAN parsing/output uses movegen for disambiguation |
| `core-headers.instructions.md` | `MoveEntry`, `BitboardSet`, `PositionState` |
| `testing.instructions.md` | Test architecture and `test_notation.cpp` group description |
