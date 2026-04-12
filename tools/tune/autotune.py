#!/usr/bin/env python3
"""Iterative tuning loop for LibreChess.

Cycles: build tuner -> run -> parse copy-paste block -> patch eval_params.h
-> rebuild -> repeat.  Each iteration starts from the previous iteration's
tuned values, allowing the optimizer to converge deeper.

Requires: make, g++ (system toolchain) in PATH.

Usage:
    python autotune.py quiet-labeled.epd                       # run until convergence, auto-K
    python autotune.py quiet-labeled.epd --iterations 10       # cap at 10 iterations
    python autotune.py quiet-labeled.epd -n 5 -e 200 --K 1.13
"""

import argparse
import os
import re
import subprocess
import sys
from datetime import datetime

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EVAL_PARAMS_REL = os.path.join('..', '..', 'lib', 'core', 'src', 'eval_params.h')

# Platform-aware make command.
MAKE = 'mingw32-make' if os.name == 'nt' else 'make'

# Marker line emitted by tune.cpp that begins the copy-paste block.
BLOCK_MARKER = 'Copy-paste block for eval_params.h'


# ---------------------------------------------------------------------------
# Output parsing
# ---------------------------------------------------------------------------

def find_copy_paste_block(stdout):
    """Return lines after the copy-paste block header from tuner stdout."""
    lines = stdout.split('\n')
    start = None
    for i, line in enumerate(lines):
        if BLOCK_MARKER in line:
            start = i + 1
            break
    if start is None:
        return []
    # Skip leading blank/comment lines until first EVAL_CONST
    while start < len(lines) and not lines[start].startswith('EVAL_CONST'):
        start += 1
    return lines[start:]


def parse_declarations(block_lines):
    """Parse copy-paste block into dict: variable_name -> declaration text.

    Handles three forms:
      - scalar:      EVAL_CONST int NAME = value;
      - inline array: EVAL_CONST type NAME[] = {v, v, ...};
      - PST:         EVAL_CONST PST_ELEM NAME[64] = {\\n ... \\n};
    """
    params = {}
    i = 0
    while i < len(block_lines):
        line = block_lines[i]
        stripped = line.strip()
        if not stripped or stripped.startswith('//'):
            i += 1
            continue

        m = re.match(r'EVAL_CONST\s+\w+\s+(\w+)', line)
        if not m:
            i += 1
            continue

        name = m.group(1)

        if '{' in line and '};' not in line:
            # Multi-line declaration (PST) — collect until };
            decl = [line]
            i += 1
            while i < len(block_lines) and '};' not in block_lines[i]:
                decl.append(block_lines[i])
                i += 1
            if i < len(block_lines):
                decl.append(block_lines[i])
                i += 1
            params[name] = '\n'.join(decl)
        else:
            # Single-line (scalar or inline array)
            params[name] = line
            i += 1

    return params


def parse_stderr_metrics(stderr):
    """Extract K, initial/final MSE from tuner stderr."""
    metrics = {}

    m = re.search(r'(?:Optimal|provided) K\s*=\s*([\d.]+)', stderr)
    if m:
        metrics['K'] = float(m.group(1))

    m = re.search(r'Initial:\s*train MSE\s*=\s*([\d.]+)', stderr)
    if m:
        metrics['init_train'] = float(m.group(1))

    m = re.search(r'Final:\s*train MSE\s*=\s*([\d.]+)', stderr)
    if m:
        metrics['final_train'] = float(m.group(1))

    m = re.search(r'Final:\s*test\s+MSE\s*=\s*([\d.]+)', stderr)
    if m:
        metrics['final_test'] = float(m.group(1))

    return metrics


def parse_changed_count(stdout):
    """Extract 'N parameters changed out of M total' from stdout."""
    m = re.search(r'(\d+) parameters changed out of (\d+) total', stdout)
    if m:
        return int(m.group(1)), int(m.group(2))
    return 0, 0


# ---------------------------------------------------------------------------
# File patching
# ---------------------------------------------------------------------------

def patch_file(params, filepath):
    """Replace parameter declarations in eval_params.h in-place.

    For each parameter in *params*, find the matching EVAL_CONST declaration
    in the file (by variable name) and replace the entire declaration with
    the tuner's output.  Comments between parameters are preserved.

    Returns the set of parameter names that were successfully patched.
    """
    with open(filepath, 'r') as f:
        lines = f.readlines()

    output = []
    i = 0
    patched = set()

    while i < len(lines):
        line = lines[i]

        # Try to match an EVAL_CONST declaration for a known parameter.
        matched_name = None
        for name in params:
            pattern = (r'EVAL_CONST\s+\w+\s+'
                       + re.escape(name)
                       + r'(?:\[|\s*=)')
            if re.search(pattern, line):
                matched_name = name
                break

        if matched_name:
            new_decl = params[matched_name]
            patched.add(matched_name)

            # Skip old declaration lines in the file.
            if '{' in line and '};' not in line:
                # Multi-line — skip until }; (inclusive)
                while i < len(lines) and '};' not in lines[i]:
                    i += 1
                if i < len(lines):
                    i += 1  # skip the }; line itself
            else:
                i += 1  # single line

            output.append(new_decl + '\n')
        else:
            output.append(line)
            i += 1

    with open(filepath, 'w') as f:
        f.writelines(output)

    return patched


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Iterative tuning loop for LibreChess eval parameters.')
    parser.add_argument('corpus', help='EPD corpus file (e.g. quiet-labeled.epd)')
    parser.add_argument('--iterations', '-n', type=int, default=0,
                        help='Max iterations (0 = run until convergence, default: 0)')
    parser.add_argument('--epochs', '-e', type=int, default=500,
                        help='Adam epochs per iteration (default: 500)')
    parser.add_argument('--K', type=float, default=0.0,
                        help='Sigmoid K (0 = auto-discover on first iteration)')
    args = parser.parse_args()

    eval_params = os.path.normpath(os.path.join(SCRIPT_DIR, EVAL_PARAMS_REL))
    tune_exe = os.path.join(SCRIPT_DIR, 'tune')
    if os.name == 'nt':
        tune_exe += '.exe'

    if not os.path.isfile(eval_params):
        print(f'ERROR: eval_params.h not found: {eval_params}', file=sys.stderr)
        return 1

    K = args.K
    max_iter = args.iterations  # 0 = unlimited
    log_entries = []

    limit_str = f'{max_iter} iterations' if max_iter > 0 else 'until convergence'
    print(f'=== Iterative tuning: {limit_str}, '
          f'{args.epochs} epochs ===')
    print(f'Corpus:  {args.corpus}')
    print(f'Target:  {eval_params}')
    if K > 0:
        print(f'K:       {K:.6f} (fixed)')
    else:
        print('K:       auto (discovered on first iteration)')
    print()

    it = 0
    while True:
        it += 1
        if max_iter > 0 and it > max_iter:
            break
        label = f'{it}/{max_iter}' if max_iter > 0 else str(it)
        print(f'--- Iteration {label} ---')

        # 1. Build tuner (picks up patched eval_params.h)
        print('  Building tuner...')
        subprocess.run([MAKE, 'clean'], cwd=SCRIPT_DIR,
                       capture_output=True, text=True)
        r = subprocess.run([MAKE], cwd=SCRIPT_DIR,
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f'  BUILD FAILED:\n{r.stderr}', file=sys.stderr)
            return 1

        # 2. Run tuner
        cmd = [tune_exe, args.corpus, str(args.epochs)]
        if K > 0:
            cmd.append(f'{K:.6f}')

        short_cmd = ' '.join(os.path.basename(c) for c in cmd)
        print(f'  Running: {short_cmd}')
        r = subprocess.run(cmd, cwd=SCRIPT_DIR,
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f'  TUNER FAILED:\n{r.stderr}', file=sys.stderr)
            return 1

        # 3. Parse metrics
        metrics = parse_stderr_metrics(r.stderr)
        changed, total = parse_changed_count(r.stdout)

        # Capture K from first iteration for reuse
        if K <= 0 and 'K' in metrics:
            K = metrics['K']
            print(f'  Discovered K = {K:.6f} (reused hereafter)')

        # 4. Parse copy-paste block & patch eval_params.h
        block = find_copy_paste_block(r.stdout)
        if not block:
            print('  ERROR: no copy-paste block in tuner output',
                  file=sys.stderr)
            return 1

        decls = parse_declarations(block)
        patched = patch_file(decls, eval_params)

        if len(patched) < len(decls):
            missing = set(decls.keys()) - patched
            print(f'  WARNING: {len(missing)} params not found in file: '
                  f'{", ".join(sorted(missing))}', file=sys.stderr)

        # 5. Summary
        init_t = metrics.get('init_train', '?')
        final_t = metrics.get('final_train', '?')
        final_v = metrics.get('final_test', '?')
        summary = (f'Changed: {changed}/{total}, '
                   f'Train: {init_t} -> {final_t}, '
                   f'Test: {final_v}, K={K:.6f}')
        print(f'  {summary}')
        log_entries.append(f'[{it}] {summary}')
        print()

        # Converged — no parameters changed
        if changed == 0:
            print('=== Converged: no parameters changed ===')
            break

    # Append to persistent log file
    log_path = os.path.join(SCRIPT_DIR, 'tune_log.txt')
    with open(log_path, 'a') as f:
        f.write(f'\n=== {datetime.now().isoformat()} '
                f'({it} iters, {args.epochs} epochs) ===\n')
        for entry in log_entries:
            f.write(entry + '\n')

    print(f'=== Done. Log appended to {os.path.basename(log_path)} ===')
    return 0


if __name__ == '__main__':
    sys.exit(main())
