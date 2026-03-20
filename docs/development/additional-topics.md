# Additional Topics

## AI-Assisted Development

This project provides context files for AI coding assistants:

| File | Purpose |
|------|---------|
| `.github/copilot-instructions.md` | GitHub Copilot instructions (always loaded) |
| `.github/instructions/*.instructions.md` | Scoped instructions (auto-loaded by `applyTo` pattern) |
| `.github/skills/*/SKILL.md` | Workflow skills (loaded on keyword match) |

`copilot-instructions.md` contains the project architecture summary, conventions, and engineering principles. Scoped instruction files provide detailed context for specific areas (core library, game mode, engine, board driver, frontend, testing, API, WiFi manager) and load automatically when editing matching files.

### Workflow: Consult Documentation First

Before analyzing or modifying any component, read the relevant `docs/` file for the area you're about to touch. This ensures you understand existing design decisions, naming conventions, and constraints before making changes.

| Task area | Read first |
|-----------|------------|
| Class hierarchy, data flows, concurrency | `docs/development/architecture.md` |
| Coding conventions, naming, frontend patterns | `docs/development/project-standards.md` |
| API endpoints | `docs/development/api.md` |
| File layout | `docs/development/project-structure.md` |
| Build setup, dependencies | `docs/development/installation.md` |
| Board interactions, menus, calibration | `docs/guides/` |
| Hardware, wiring, components | `docs/hardware/` |

### Documentation Sync Rule

When a code change affects architecture, public APIs, endpoints, configuration, build pipeline, file structure, or engineering conventions, update the relevant documentation **and tests** in the same change — never defer to a follow-up:

- New or changed API endpoints → update [api.md](api.md)
- New or removed source files → update [project-structure.md](project-structure.md)
- Build or dependency changes → update [installation.md](installation.md)
- Architecture or internal design changes → update [architecture.md](architecture.md)
- New LED behaviors, menu changes, or physical interaction changes → update the relevant file in `docs/guides/`
- Chess logic changes in `lib/core/` → update or add unit tests in `test/`, update `core-library.instructions.md` and `testing.instructions.md` as needed (see Completion Checklist in `core-library.instructions.md`)
- Any behavior change documented in a `.github/instructions/` file → update the instruction file in the same change

## CLI Quick Reference

PlatformIO CLI is not on `PATH` by default. Use the full path:

| Platform | Path |
|----------|------|
| Windows | `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` |
| Linux | `~/.platformio/penv/bin/pio` |

Common commands:

| Action | Command |
|--------|---------|
| Build | `pio run` |
| Upload firmware | `pio run -t upload` |
| Serial monitor | `pio device monitor` |
| Clean build | `pio run -t clean` |
| Run all tests | `pio test -e native` |
| Run one test suite | `pio test -e native -f test_core` |

Serial monitor runs at **115200 baud** (configured in `platformio.ini`).

## Memory Budget

ESP32-WROOM-32: 520 KiB SRAM (~320 KiB usable DRAM), 4 MiB flash, 240 MHz dual-core.

### Flash Partitions

| Partition | Size |
|-----------|------|
| nvs | 20 KiB |
| otadata | 8 KiB |
| app0 (OTA slot A) | 1.75 MiB |
| app1 (OTA slot B) | 1.75 MiB |
| spiffs (LittleFS data) | 448 KiB |

### Heap Allocations

| Component | Size | Notes |
|-----------|------|-------|
| WiFi + networking | ~60–70 KiB | Persistent after `WiFi.begin()` |
| Async web server | ~10–20 KiB | ESPAsyncWebServer buffers |
| LittleFS | ~5–10 KiB | Filesystem metadata |
| NeoPixelBus (LED) | ~2–3 KiB | DMA buffer for 64 LEDs |
| Transposition table | up to 128 KiB | Dynamic: `(freeHeap - 48KB) / 4`, capped, 16B/entry |
| Pawn hash table | 8 KiB | 1024 entries × 8B, owned by Engine |
| Eval hash table | 8 KiB | 1024 entries × 8B, owned by Engine |
| SearchState | ~19 KiB | `std::unique_ptr` in `findBestMove()` |
| **Free after persistent allocs** | **~80–100 KiB** | Available for TT + hash tables + SearchState |

### FreeRTOS Task Stacks

| Task | Stack Size | Purpose |
|------|-----------|---------|
| lcTask (LibreChess engine) | 16 KiB | Search: Engine + per-ply recursion |
| AnimWorker | 4 KiB | LED animation queue processing |
| WiFi connect | default | ESP-IDF WiFi connection |
| OTA reboot | 2 KiB | Short-lived reboot task |

### Engine Stack Budget (lcTask, 16 KiB)

| Component | Size | Location |
|-----------|------|----------|
| Engine (Position w/ HashHistory 256) | ~2,350 B | stack |
| MoveList rootMoves | 876 B | stack |
| Per-ply recursion (MoveList + scores + UndoInfo) | ~1,844 B × depth | stack |
| At depth 6 | ~11,064 B | stack |
| SearchState (history + killers + countermoves) | ~19 KiB | **heap** (`std::unique_ptr`) |
| Transposition table | varies | **heap** (`new[]`) |
| **Estimated total at depth 6** | **~14,290 B** | fits in 16 KiB |

### Per-Ply Recursion Breakdown

| Component | Size |
|-----------|------|
| MoveList (Move[218] + count) | 876 B |
| scores[218] (int array) | 872 B |
| UndoInfo (PositionState + hash + piece + sq + int) | ~36 B |
| Local variables | ~60 B |
| **Total per ply** | **~1,844 B** |

### Position Size (on stack inside Engine)

| Field | Size |
|-------|------|
| BitboardSet (12 piece + 2 color + occupancy) | 120 B |
| Piece mailbox[64] | 64 B |
| Color currentTurn | 1 B |
| PositionState | ~20 B |
| Square kingSquare[2] | 2 B |
| uint64_t hash | 8 B |
| HashHistory (256 × 8B + int) | ~2,052 B |
| Cache fields (FEN string + eval + dirty flags) | ~36 B |
| **Total** | **~2,303 B** |

## Remaining Features & Enhancements

Features from the [Chess Programming Wiki](https://www.chessprogramming.org/Main_Page) that are not yet implemented or are only partially implemented. Organized by category with links to the relevant CPW pages.

### Search

| Feature | Status | Description | Reference |
|---------|--------|-------------|-----------|
| Singular Extensions | Not implemented | Extend search for moves that are significantly better than all alternatives at a node. Requires a verification re-search per node, making it the most compute-intensive search enhancement. | [CPW — Singular Extensions](https://www.chessprogramming.org/Singular_Extensions) |
| Reverse Futility Pruning | Implemented | Static pruning: if staticEval - RFP_MARGIN*depth/(1+improving) >= beta at depth ≤ 6, prune the node. Margin halved when position is improving. | [CPW — Reverse Futility Pruning](https://www.chessprogramming.org/Reverse_Futility_Pruning) |
| ProbCut | Not implemented | Uses a shallow search with reduced bounds to predict whether a deeper search would cause a beta cutoff. | [CPW — ProbCut](https://www.chessprogramming.org/ProbCut) |
| Staged Move Generation | Not implemented | Generate moves lazily (TT move first, then captures, then quiets) instead of generating all moves upfront. Saves move generation work when early moves cause cutoffs. | [CPW — Move Generation](https://www.chessprogramming.org/Move_Generation#Staged_Move_Generation) |
| History-Informed LMR | Implemented | Logarithmic LMR table (`LMR_TABLE[depth][moveIndex]` via `0.75 + ln(d)*ln(m)/2.5`) with history adjustments (hist < -500 → +1, hist > 1500 → -1) and improving flag (+1 when not improving). | [CPW — Late Move Reductions](https://www.chessprogramming.org/Late_Move_Reductions) |
| Continuation History | Not implemented | Two-ply history heuristic (previous move + current move) for better quiet move ordering. | [CPW — Continuation History](https://www.chessprogramming.org/History_Heuristic#Continuation_History) |

### Evaluation

| Feature | Status | Description | Reference |
|---------|--------|-------------|-----------|
| Endgame PSTs for All Pieces | Implemented | All six piece types have separate MG and EG piece-square tables. Knight EG favors support squares, bishop EG favors long diagonals, rook EG has heavy 7th rank bonus, queen EG favors central activity. | [CPW — Piece-Square Tables](https://www.chessprogramming.org/Piece-Square_Tables) |
| Threats | Implemented | MG/EG bonus for attacking higher-value enemy pieces with lower-value friendly pieces — pawn→minor, pawn→rook, pawn→queen, minor→rook, minor→queen, rook→queen. Uses `AttackInfo.byPiece` intersection with enemy piece bitboards. | [CPW — Threats](https://www.chessprogramming.org/Evaluation#Threats) |

### Testing

| Feature | Status | Description | Reference |
|---------|--------|-------------|-----------|
| Tactical Test Suites | Implemented | WAC, BK, ERET benchmark suites with EPD parser. Time-only mode (500ms/position). | [CPW — Test Positions](https://www.chessprogramming.org/Test-Positions) |
| NPS Benchmarks | Implemented | Nodes-per-second measurement across 6 standard positions (opening, middlegame, endgame, tactical). 1 second per position. Informational — no hard thresholds. `test/test_benchmarks/test_nps.cpp`. | [CPW — Nodes per Second](https://www.chessprogramming.org/Nodes_per_Second) |
| Evaluation Regression Tests | Implemented | 12 fixed-position score assertions covering symmetry, material advantage, pawn structure, bishop pair, king safety, threats, phase tapering. `test/test_core/test_eval_regression.cpp`. | — |

## References

- [Chess Programming Wiki](https://www.chessprogramming.org/Main_Page) — comprehensive resource for chess engine programming: board representations (bitboards, mailbox), move generation, search algorithms, evaluation, and endgame tablebases.
