---
applyTo: "lib/core/src/epd.*"
description: "EPD parser: parseEPDLine, validateEPDLine. Use when editing epd.h or epd.cpp."
---

# EPD Parser (`lib/core/src/epd.h/cpp`)

Generic EPD parser in `namespace LibreChess`. Stateless namespace.

## Public API

- `EPDOperation` — `opcode`, `operands[EPD_MAX_OPERANDS]`, `operandCount`
- `EPDRecord` — `fen`, `operations[EPD_MAX_OPERATIONS]`, `operationCount`, `findOperation(opcode)`, `id()`
- `parseEPDLine(line) → EPDRecord`
- `validateEPDLine(line) → bool`

Supports standard opcodes: `bm`, `am`, `id`, `c0`, `c9`. Used by tactical test suites and offline tuner.

## Validation Rules

- EPD stores the first four FEN fields (piece placement, side, castling, EP). `validateEPDLine()` appends `0 1` and delegates to `fen::validateFEN()` so rank widths, piece chars, king counts, side, castling, and EP syntax match normal FEN validation.
- Fixed caps are hard limits: more than `EPD_MAX_OPERATIONS` operations or more than `EPD_MAX_OPERANDS` operands is invalid. `parseEPDLine()` returns an empty `EPDRecord` on over-cap input instead of silently truncating.

## Testing

Mirror test file: `test/test_core/test_epd.cpp` (suite: `test_core`). EPD files also used by positional test suites (`test_positions_*`). See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `testing.instructions.md` | Test architecture, `test_epd.cpp` group description, and EPD-based positional suites |
