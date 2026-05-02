---
description: "DEPRECATED — split into per-library and per-file instruction files"
---

# Chess Libraries — Split Notice

This file has been split into two library-level instruction files (general conventions) plus per-file instruction files (detailed API/design):

**Library-level** (shared conventions, completion checklists):
- **`core.instructions.md`** — `lib/core/**` (includes search, engine facade, and all chess logic)
- **`game-library.instructions.md`** — `lib/game/**`

**Per-file** (auto-loaded when editing the corresponding source):
- Core: `position`, `evaluation`, `movegen`, `attacks`, `notation`, `fen`, `zobrist`, `epd`, `trace`, `core-headers`, `search`, `engine-facade`
- Game: `game`, `history`, `game-headers`

See the individual files for up-to-date content.
