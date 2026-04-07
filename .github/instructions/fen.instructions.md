---
applyTo: "lib/core/src/fen.*"
description: "FEN parsing, serialization, and validation. Use when editing fen.h or fen.cpp."
---

# FEN (`lib/core/src/fen.h/cpp`)

FEN parse/serialize/validate. Stateless namespace.

## Public API

- `boardToFEN(mailbox, turn, state) → string` — serialize position to FEN
- `fenToBoard(fen, bb, mailbox, turn, state)` — parse FEN into board (lenient)
- `validateFEN(fen) → bool` — strict format validation

## Design Notes

- **Two-step validation** — `validateFEN()` checks format strictly, `fenToBoard()` parses leniently. `Position::loadFEN()` calls validate first, returns `false` on failure without modifying state. Also rejects missing kings.
- Always `validateFEN()` before `fenToBoard()` when accepting user input.

## Testing

Mirror test file: `test/test_core/test_fen.cpp` (suite: `test_core`). When changing FEN parse/serialize, update tests in the same change. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `position.instructions.md` | `Position::loadFEN()` delegates to `fen::fenToBoard`/`validateFEN` |
| `core-headers.instructions.md` | `BitboardSet`, `Piece[]`, `PositionState` |
| `testing.instructions.md` | Test architecture and `test_fen.cpp` group description |
