---
name: optimization
description: "**WORKFLOW SKILL** — Implementation-level code optimization and simplification. USE FOR: performance tuning (memory, speed, flash); reducing implementation complexity (nesting, branching, duplication); eliminating dead code; ESP32 resource optimization (stack, heap, IRAM); replacing branching cascades with lookup tables; streamlining logic without changing the public API surface. DO NOT USE FOR: adding new features; redesigning a module's public API or abstraction layers (use redesign skill); cross-module structural changes (use refactoring skill); single-line tweaks; cosmetic formatting. INVOKES: file system tools, terminal (build/test), subagents for codebase exploration."
argument-hint: "Describe the optimization goal (e.g., 'reduce heap usage in Position', 'eliminate redundant sensor reads', 'simplify evaluation hot path')"
---

# Code Optimization & Simplification Workflow

A structured approach to making code faster, leaner, and simpler at the implementation level — without changing behavior or the public API surface. Every optimization must be measured, not assumed. Every simplification must preserve correctness.

**Scope boundary**: This skill works *within* the current design. It changes *how* code implements its contract, not *what* the contract is. If the problem is too many public functions, near-duplicate overloads, or the wrong abstraction layer — that's the redesign skill. If the problem is code in the wrong module or a broken class hierarchy — that's the refactoring skill.

## Step 1 — Profile & Measure

Never optimize without understanding the current state first.

- **Identify the target**: What's slow, complex, or wasteful? Get specifics from the user or from observation.
- **Read the code**: Read every file in the affected area — implementations, not just headers. Understand the full call chain.
- **Establish baselines**: Measure what you can before changing anything:
  - Build output: flash usage, RAM usage (from PlatformIO build summary)
  - Speed: for hot-path changes, write a quick timing test — run the function N times in a loop with `micros()` or `std::chrono` and record the average. Keep the test in the suite if the path is performance-critical.
  - Stack sizes: FreeRTOS task allocations, recursive call depth
  - Complexity: count branches, nesting depth, function length, parameter counts
  - Duplication: identify copy-pasted logic, near-identical functions
  - Memory footprint inventory: for struct/class optimization, build a byte-precise table of every field (type, size, offset) and every per-instance allocation. For recursive code (search), compute per-ply stack frame size (largest locals × recursion depth) and compare against FreeRTOS task stack. This inventory is the foundation for every memory optimization — skip it and you're guessing.
- **Read related docs and instructions**: Check architecture docs and instruction files for constraints and design rationale. Understand *why* the code is the way it is before deciding it's wrong.

**Output**: Present findings with concrete numbers — "this function is 120 lines with 8 nesting levels", "this struct uses 2.4KB stack", "these two functions share 90% of their logic". No vague claims like "this seems slow."

## Step 2 — Identify Opportunities

Categorize findings by type and impact.

### Performance opportunities
- **Memory**: heap allocations that could be stack/static, oversized buffers, redundant copies, `String` vs `std::string` conversions
- **Flash**: large lookup tables that could be computed, duplicated string literals, unused includes pulling in code
- **Speed**: unnecessary recomputation (missing caching), busy-waits that could be event-driven, redundant validation in hot paths
- **ESP32-specific**: `IRAM_ATTR` for ISR-called functions, `constexpr` for compile-time computation, `PROGMEM` for constant data, task stack right-sizing

### Simplification opportunities
- **Dead code**: unreachable branches, unused parameters, stale variables, orphaned includes
- **Internal redundancy**: copy-pasted logic within a file, repeated inline expressions that should be a file-local helper
- **Complexity**: deeply nested conditionals that could be early-returns, boolean parameters that should be enums, convoluted control flow
- **Branching cascades**: if/switch chains that map discrete inputs to outputs. Replace with lookup tables (`constexpr` arrays) for cleaner, faster code

**Not in scope** (hand off to other skills):
- Near-duplicate *public* functions that differ only by a parameter → **redesign** (API consolidation)
- Unnecessary abstraction layers or intermediate classes → **redesign** (questioning abstractions)
- Functions that belong in a different module → **refactoring** (structural relocation)

**Output**: Prioritized list of opportunities with estimated impact (high/medium/low) and risk (safe/moderate/risky). Present to user for approval.

**Gate**: Wait for user to select which optimizations to pursue. The user may reorder, reject, or add items.

## Step 3 — Verify Test Coverage

Before optimizing, ensure the affected code has adequate test coverage.

- Map which functions/paths have tests and which don't
- Write tests for any uncovered paths that the optimization will touch
- Run the full test suite to establish a green baseline
- For performance changes: note current build sizes for before/after comparison

If existing coverage is sufficient, skip test writing and note why.

**Baseline must be green before proceeding.**

## Step 4 — Apply Optimizations

Work through approved items one at a time — smallest and safest first.

For each optimization:
1. Mark it as in-progress
2. Apply the change — keep it minimal and focused on one concern
3. Build to verify compilation
4. Run tests to verify no regressions
5. For performance changes: compare build output (flash/RAM) and timing tests against baseline
6. Mark as complete with results ("saved 1.2KB flash", "reduced from 80 to 35 lines", "makeMove 12% faster")

### Rules during implementation
- **One concern per change** — don't combine a simplification with a performance fix
- **Preserve behavior exactly** — optimization changes *how* code works, never *what* it does
- **Measure after each change** — if a "performance" change shows no measurable improvement, revert it
- **Trust the compiler** — don't micro-optimize what the compiler already handles (strength reduction, loop unrolling, dead store elimination). Focus on algorithmic and structural improvements.
- **Don't fight the framework** — work with Arduino/ESP-IDF/FreeRTOS idioms, not against them

## Step 5 — Review & Clean

After all optimizations are applied, review the result holistically.

Check for:
- **Consistency**: Do the optimized sections still follow the same patterns as the rest of the codebase?
- **Readability**: Did any optimization make the code harder to understand? If so, add a brief comment explaining *why* the less-obvious approach was chosen.
- **Dead artifacts**: Comments describing the old approach, unused variables from removed code paths, includes no longer needed
- **Over-optimization**: Did any change add complexity for marginal gain? Revert it — simplicity wins over micro-optimization.

**Decision tree:**
- **Minor cleanup** (stale comment, dead variable): Fix immediately.
- **Readability concern** (clever code needs context): Add a one-line comment explaining the tradeoff.
- **Net negative** (more complex but negligible improvement): Revert the change.

## Step 6 — Final Validation & Report

- Run the complete test suite
- Build all targets
- Compare final metrics against Step 1 baselines

**Output**: Summary report with before/after measurements. Be specific:
- "Flash: 823,456 → 819,200 bytes (−4,256 bytes, −0.5%)"
- "Position::makeMove: 85 → 52 lines, max nesting 6 → 3"
- "makeMove×10000: 148ms → 131ms (−11.5%)"
- "Removed 3 unused functions (120 lines total)"

If any change had no measurable impact, say so honestly. If a change made things slower or larger, flag it as a candidate for revert.

## Step 7 — Update Documentation

Update all documentation that references the changed code.

- Instruction files (`.github/instructions/`) — if behavior or API surface changed
- Architecture docs — if structural simplifications altered component relationships
- Code comments — only where the old comment describes removed/changed logic

## Principles

- **Measure, don't guess** — every optimization claim must have a number attached. "Should be faster" is not acceptable.
- **Simplicity is optimization** — the simplest code is often the fastest code. Remove indirection, flatten nesting, eliminate unnecessary abstraction. Less code = less to execute, less to cache, less to maintain.
- **Behavior preservation is non-negotiable** — if a test fails, the optimization is wrong. Fix the optimization, never the test.
- **Smallest change, biggest impact first** — prioritize high-impact, low-risk changes. Save complex rewrites for last.
- **Reversibility** — if an optimization doesn't measurably help, revert it. Sunk cost doesn't justify keeping complex code.
- **ESP32 constraints are real** — 320KB RAM, 4MB flash, 8KB default task stacks, watchdog timers. These aren't theoretical limits — they're daily constraints. Optimize for them specifically.
- **Readability survives optimization** — performance-critical code that nobody can understand will be broken by the next maintainer. A well-placed comment costs nothing.
- **Verify plan completeness** — after finalizing the optimization plan (Step 2), cross-check every finding against the technical steps. Ensure no identified opportunity was dropped between analysis and plan.

## Related Skills

- **redesign** — use when optimization reveals that the module's API surface itself is the problem (too many near-duplicate functions, parameter proliferation, leaked internals). Redesign uses blank-slate thinking to find the ideal API; optimization works within the current design.
- **refactoring** — use when optimization reveals an architectural problem that requires cross-module structural changes (class extraction, hierarchy changes, module reorganization). Finish or pause the optimization, then invoke the refactoring workflow.
- **audit** — use when you want to explore the codebase for quality issues before deciding what to optimize. An audit report may identify targets for this optimization workflow.
