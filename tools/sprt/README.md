# SPRT Testing

Validate engine changes with a [Sequential Probability Ratio Test](https://www.chessprogramming.org/Match_Statistics#SPRT) (SPRT)
before committing. Uses [fastchess](https://github.com/Disservin/fastchess) to
run many games between a baseline engine and a dev engine, accepting or
rejecting the change with statistical confidence.

## Prerequisites

1. **fastchess** — download from [releases](https://github.com/Disservin/fastchess/releases)
   and place the executable in this directory (`tools/sprt/`), or add it to PATH.
2. **Toolchain** — `g++` and `make` (or `mingw32-make`) on PATH.
3. **Git** — needed to check out the baseline version via worktree. On Windows, Git provides `sh` which the Makefile recipes require.

## Quick Start

```sh
cd tools/sprt

# Default: compare working tree against HEAD, tc=8+0.08, elo0=0 elo1=10
make

# Compare against previous commit
make BASELINE=HEAD~1

# Tighter bounds (detect +5 Elo changes) with longer TC
make TC=10+0.1 ELO1=5

# Use a pre-built baseline binary (skip git worktree)
make BASE_EXE=./old-engine.exe
```

On Windows with PlatformIO's MinGW, use `mingw32-make` instead of `make`.

## Parameters

| Variable       | Default   | Description |
|----------------|-----------|-------------|
| `BASELINE`     | `HEAD`    | Git ref for the baseline engine |
| `TC`           | `8+0.08`  | Time control (seconds+increment) |
| `ELO0`         | `0`       | H0 bound — null hypothesis (no gain) |
| `ELO1`         | `10`      | H1 bound — alternative hypothesis (this much gain) |
| `ALPHA`        | `0.05`    | False positive rate (Type I error) |
| `BETA`         | `0.05`    | False negative rate (Type II error) |
| `ROUNDS`       | `5000`    | Maximum rounds (2 games per round) |
| `CONCURRENCY`  | auto      | Parallel games (default: CPU count) |
| `BOOK`         | `8moves_v3.pgn` | Opening book path (PGN or EPD, auto-detected) |
| `BASE_EXE`     | *(none)*  | Pre-built baseline binary (skips worktree build) |
| `HASH`         | `64`      | Hash table size in MB per engine |

## How It Works

1. **Builds dev engine** from the current working tree using `tools/engine/Makefile`.
2. **Builds baseline engine** by creating a temporary git worktree at the specified ref, building there, then cleaning up the worktree.
3. **Runs fastchess** with SPRT bounds. The test terminates early once the statistical test reaches a conclusion (accept H1 or accept H0), or when the maximum number of rounds is reached.
4. **Results**: fastchess prints live Elo estimates and SPRT status. Games are saved to `games.pgn`.

## Opening Book

**8moves_v3.pgn** — ~34,700 openings, 8 moves deep, generated from Stockfish self-play.
Sourced from the [Chess Programming Wiki](https://www.chessprogramming.org/). Designed
for testing weaker engines with balanced, diverse positions.

Format is auto-detected from the file extension (`.pgn` or `.epd`). Supply a custom book
with `BOOK=path/to/book.epd`.

## Interpreting Results

- **H1 accepted** — the change is likely a gain of at least `ELO1` Elo. Ship it.
- **H0 accepted** — the change shows no significant improvement. Rethink the approach.
- **Inconclusive** — maximum rounds reached without a decision. Increase `ROUNDS` or widen bounds.

## Typical Workflows

### Testing a search optimization
```sh
# Make your changes to lib/core/src/search.cpp, then:
make ELO1=10
```

### Testing an evaluation tweak
```sh
# After tuning eval parameters:
make ELO1=5 ROUNDS=10000
```

### Comparing two branches
```sh
# Build the other branch's engine separately, then:
make BASE_EXE=./other-branch.exe
```

## Files

| File | Description |
|------|-------------|
| `Makefile` | SPRT runner: builds engines, invokes fastchess with SPRT bounds |
| `8moves_v3.pgn` | Opening book (~34,700 positions, 8 moves deep) |
| `.gitignore` | Ignores build artifacts, PGN output, fastchess binary |
