#!/usr/bin/env python3
"""Generate a self-play EPD corpus for Texel tuning.

Runs LibreChess vs itself via fastchess, then extracts quiet positions
labeled with game results.  The output EPD uses the same c9 format as
the zurichess corpus (compatible with tune.cpp).

Requires: python-chess (pip install python-chess), fastchess binary.

Usage:
    python gen_corpus.py                                    # defaults
    python gen_corpus.py --games 10000 --tc 1+0.01          # more games
    python gen_corpus.py --pgn existing.pgn --skip-play     # extract only
"""

import argparse
import os
import random
import subprocess
import sys

try:
    import chess
    import chess.pgn
except ImportError:
    print('ERROR: python-chess required.  Install: python -m pip install python-chess',
          file=sys.stderr)
    sys.exit(1)

# ---------------------------------------------------------------------------
# Paths (relative to this script)
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ENGINE_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, '..', 'engine'))
SPRT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, '..', 'sprt'))

ENGINE_EXE = os.path.join(ENGINE_DIR, 'librechess.exe' if os.name == 'nt' else 'librechess')
FASTCHESS_EXE = os.path.join(SPRT_DIR, 'fastchess.exe' if os.name == 'nt' else 'fastchess')
BOOK_PATH = os.path.join(SPRT_DIR, '8moves_v3.pgn')

MAKE = 'mingw32-make' if os.name == 'nt' else 'make'

# ---------------------------------------------------------------------------
# Self-play via fastchess
# ---------------------------------------------------------------------------

def build_engine():
    """Build the UCI engine from current source."""
    print('Building engine...')
    subprocess.run([MAKE, '-C', ENGINE_DIR, 'clean'],
                   check=True, capture_output=True)
    subprocess.run([MAKE, '-C', ENGINE_DIR],
                   check=True, capture_output=True)
    print(f'  Built: {ENGINE_EXE}')


def run_selfplay(num_games, tc, hash_mb, concurrency, pgn_out):
    """Run engine vs itself using fastchess."""
    if not os.path.isfile(FASTCHESS_EXE):
        print(f'ERROR: fastchess not found: {FASTCHESS_EXE}', file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(ENGINE_EXE):
        print(f'ERROR: engine not found: {ENGINE_EXE}', file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(BOOK_PATH):
        print(f'ERROR: opening book not found: {BOOK_PATH}', file=sys.stderr)
        sys.exit(1)

    cmd = [
        FASTCHESS_EXE,
        '-engine', f'cmd={ENGINE_EXE}', 'name=white',
        '-engine', f'cmd={ENGINE_EXE}', 'name=black',
        '-each', f'tc={tc}', f'option.Hash={hash_mb}',
        '-openings', f'file={BOOK_PATH}', 'format=pgn', 'order=random',
        '-rounds', str(num_games),
        '-repeat',
        '-draw', 'movenumber=34', 'movecount=8', 'score=20',
        '-resign', 'movecount=3', 'score=600',
        '-concurrency', str(concurrency),
        '-recover',
        '-pgnout', f'file={pgn_out}', 'notation=san',
    ]

    print(f'Running self-play: {num_games} games, tc={tc}, '
          f'hash={hash_mb}MB, concurrency={concurrency}')
    print(f'  PGN output: {pgn_out}')
    print(f'  Command: {" ".join(cmd)}')
    print()

    # Run from SPRT dir so fastchess drops config.json there (gitignored).
    subprocess.run(cmd, check=True, cwd=SPRT_DIR)
    print(f'\nSelf-play complete.  PGN: {pgn_out}')


# ---------------------------------------------------------------------------
# Position extraction
# ---------------------------------------------------------------------------

RESULT_MAP = {
    '1-0': '1-0',
    '0-1': '0-1',
    '1/2-1/2': '1/2-1/2',
}


def extract_positions(pgn_path, skip_plies=16, skip_last_plies=10):
    """Extract quiet positions from a PGN file.

    Filters:
      - Skip first `skip_plies` (opening book moves)
      - Skip last `skip_last_plies` (adjudication noise)
      - Skip positions where side to move is in check
      - Skip positions reached via a capture or promotion
      - Skip decisive positions (game already over: checkmate/stalemate)

    Returns list of (fen, c9_result) tuples.
    """
    positions = []
    games_read = 0
    games_skipped = 0

    with open(pgn_path) as f:
        while True:
            game = chess.pgn.read_game(f)
            if game is None:
                break

            result_str = game.headers.get('Result', '*')
            c9 = RESULT_MAP.get(result_str)
            if c9 is None:
                games_skipped += 1
                continue

            games_read += 1
            board = game.board()
            moves = list(game.mainline_moves())
            total_plies = len(moves)

            for ply, move in enumerate(moves):
                is_capture = board.is_capture(move)
                is_promo = move.promotion is not None
                board.push(move)

                # Skip opening book region.
                if ply < skip_plies:
                    continue

                # Skip near-end positions (adjudication region).
                if ply >= total_plies - skip_last_plies:
                    continue

                # Skip captures and promotions (not quiet).
                if is_capture or is_promo:
                    continue

                # Skip positions where side to move is in check.
                if board.is_check():
                    continue

                fen = board.fen()
                positions.append((fen, c9))

            if games_read % 1000 == 0:
                print(f'  Processed {games_read} games, '
                      f'{len(positions)} positions extracted...', end='\r')

    print(f'  Processed {games_read} games ({games_skipped} skipped), '
          f'{len(positions)} positions extracted.        ')
    return positions


def deduplicate(positions):
    """Remove duplicate FENs (keeping first occurrence)."""
    seen = set()
    unique = []
    for fen, c9 in positions:
        # Compare only piece placement + side + castling + ep (not counters).
        key = ' '.join(fen.split()[:4])
        if key not in seen:
            seen.add(key)
            unique.append((fen, c9))
    return unique


def write_epd(positions, output_path):
    """Write positions as EPD with c9 result opcodes."""
    with open(output_path, 'w') as f:
        for fen, c9 in positions:
            # EPD = first 4 FEN fields + opcodes
            parts = fen.split()
            epd = ' '.join(parts[:4])
            f.write(f'{epd} c9 "{c9}";\n')
    print(f'  Written {len(positions)} positions to {output_path}')


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Generate self-play EPD corpus for Texel tuning.')
    parser.add_argument('--games', '-g', type=int, default=5000,
                        help='Number of self-play games (default: 5000)')
    parser.add_argument('--tc', default='1+0.01',
                        help='Time control for self-play (default: 1+0.01)')
    parser.add_argument('--hash', type=int, default=64,
                        help='Hash table size in MB (default: 64)')
    parser.add_argument('--concurrency', '-j', type=int, default=0,
                        help='Concurrent games (default: CPU cores / 2)')
    parser.add_argument('--pgn', default='',
                        help='Path to existing PGN (skip self-play)')
    parser.add_argument('--output', '-o', default='selfplay.epd',
                        help='Output EPD file (default: selfplay.epd)')
    parser.add_argument('--skip-play', action='store_true',
                        help='Skip self-play, extract from --pgn only')
    parser.add_argument('--skip-plies', type=int, default=16,
                        help='Skip first N plies (default: 16 = 8 moves)')
    parser.add_argument('--skip-last', type=int, default=10,
                        help='Skip last N plies (default: 10)')
    parser.add_argument('--no-dedup', action='store_true',
                        help='Keep duplicate positions')
    parser.add_argument('--build', action='store_true',
                        help='Rebuild engine before self-play')
    args = parser.parse_args()

    # Resolve output path relative to script directory.
    output = args.output
    if not os.path.isabs(output):
        output = os.path.join(SCRIPT_DIR, output)

    pgn_path = args.pgn
    if not pgn_path:
        pgn_path = os.path.join(SCRIPT_DIR, 'selfplay.pgn')

    if args.build:
        build_engine()

    # --- Step 1: Self-play ---
    if not args.skip_play:
        concurrency = args.concurrency
        if concurrency <= 0:
            concurrency = max(1, os.cpu_count() // 2)

        run_selfplay(args.games, args.tc, args.hash, concurrency, pgn_path)
        print()

    if not os.path.isfile(pgn_path):
        print(f'ERROR: PGN file not found: {pgn_path}', file=sys.stderr)
        return 1

    # --- Step 2: Extract positions ---
    print('Extracting quiet positions...')
    positions = extract_positions(pgn_path, args.skip_plies, args.skip_last)

    if not args.no_dedup:
        before = len(positions)
        positions = deduplicate(positions)
        dupes = before - len(positions)
        if dupes > 0:
            print(f'  Removed {dupes} duplicate positions '
                  f'({len(positions)} unique remaining).')

    # Shuffle for good train/test split in tuner.
    random.seed(42)
    random.shuffle(positions)

    # --- Step 3: Write EPD ---
    write_epd(positions, output)

    # Summary
    print(f'\n=== Corpus ready: {output} ===')
    print(f'  Games: {args.games if not args.skip_play else "N/A (from PGN)"}')
    print(f'  Positions: {len(positions)}')
    print(f'\nNext: python autotune.py {output} -n 1 -e 100')
    return 0


if __name__ == '__main__':
    sys.exit(main() or 0)
