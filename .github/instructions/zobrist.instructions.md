---
applyTo: "lib/core/src/zobrist.*"
description: "Zobrist hashing: constexpr key generation, full-board and pawn-only hash computation. Use when editing zobrist.h or zobrist.cpp."
---

# Zobrist (`lib/core/src/zobrist.h/cpp`)

Constexpr Zobrist key generation (xorshift64) and hash computation. Header-only except `zobrist.cpp` for `computeHash`.

## Public API

- `Keys` struct — `pieces[12][64]`, `castling[16]`, `enPassant[8]`, `sideToMove` (all compile-time)
- `static constexpr Keys KEYS` — deterministic compile-time initialization
- `computeHash(bb, mailbox, turn, state, epLegal) → uint64_t` — full position hash (EP legality passed by caller)
- `computePawnHash(bb) → uint64_t` — XORs all pawn piece keys, used as pawn hash table lookup key

## Design Notes

- **Incremental in `make()`** — `Position::make()` XORs deltas. `computeHash()` only for debug verification / cold paths.
- **EP legality parameter** — caller pre-computes via `movegen::hasLegalEnPassantCapture()` and passes `bool epLegal`. Avoids zobrist depending on movegen.

## Testing

Mirror test file: `test/test_core/test_zobrist.cpp` (suite: `test_core`). When changing hash computation, update tests in the same change. See `testing.instructions.md` for test group details.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `core.instructions.md` | Parent library — shared conventions |
| `position.instructions.md` | `Position::make()` does incremental XOR updates; `computeHash()` for debug |
| `movegen.instructions.md` | EP legality pre-computed by caller via `hasLegalEnPassantCapture()` |
| `evaluation.instructions.md` | `computePawnHash()` keys the `PawnHashTable` |
| `core-headers.instructions.md` | `BitboardSet`, `Piece[]`, `Color` |
| `testing.instructions.md` | Test architecture and `test_zobrist.cpp` group description |
