---
description: "DEPRECATED — split into per-library and per-file instruction files"
---

# Chess Libraries — Split Notice

This file has been split into three library-level instruction files (general conventions) plus per-file instruction files (detailed API/design):

**Library-level** (shared conventions, completion checklists):
- **`core.instructions.md`** — `lib/core/**`
- **`game-library.instructions.md`** — `lib/game/**`
- **`engine-library.instructions.md`** — `lib/engine/**`

**Per-file** (auto-loaded when editing the corresponding source):
- Core: `position`, `evaluation`, `movegen`, `attacks`, `notation`, `fen`, `zobrist`, `epd`, `trace`, `core-headers`
- Game: `game`, `history`, `game-headers`
- Engine: `search`, `engine-facade`

See the individual files for up-to-date content.
