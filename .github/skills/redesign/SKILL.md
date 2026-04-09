---
name: redesign
description: "**WORKFLOW SKILL** — Module-level API redesign using blank-slate methodology. USE FOR: redesigning a module's public API surface; reducing function count and parameter proliferation; questioning abstraction layers; finding whether the current API is at a local vs global optimum; standardizing overload patterns; consolidating near-duplicate functions. DO NOT USE FOR: performance tuning without API changes (use optimization); cross-module structural changes (use refactoring); read-only quality review (use audit). INVOKES: file system tools, terminal (build/test), subagents for codebase exploration."
argument-hint: "Describe the target module and design concern (e.g., 'review movegen public API', 'redesign engine provider interface', 'simplify History API surface')"
---

# Module Design Review Workflow

A structured methodology for redesigning a module's API by dissociating requirements from current implementation. The goal is to find the **globally optimal** API — the one you'd build if the module didn't exist yet — then selectively close the gap between it and the current code.

**Core insight**: Incremental optimization converges to a local minimum of the *current* architecture. Blank-slate redesign asks "what do callers actually need?" and designs the API purely from requirements — unconstrained by existing function boundaries, naming, or layering. The delta between current and ideal reveals the real design debt.

## Step 1 — Profile the Module

Understand the current state before redesigning.

- **Read everything**: Header + implementation, not just the public surface. Understand the internal helper stack.
- **Load instruction files**: Check `.github/instructions/` for the module's instruction file and its parent library instruction file. Understand documented design rationale.
- **Measure**:
  - Line count (header vs implementation)
  - Public function count (the API surface)
  - Internal helper count and layering depth
  - Parameter counts per function (decomposed params inflate this)
  - Struct/type count
- **Map the internal call graph**: Which helpers call which? How many layers of delegation exist? Draw the stack: `public API → layer 1 → layer 2 → ... → leaf logic`.

**Output**: Present the module profile with concrete numbers. Identify which areas look clean and which look overgrown.

## Step 2 — Map External Usage

Understand how the module is actually consumed. This is the foundation for the blank-slate redesign — you need to know what callers *do*, not just which functions they call.

- **Search all callers**: For every public function, find every call site across the codebase. Use subagents for parallel exploration if the module has many public functions.
- **Build a usage table**:
  | Function | Callers | Call sites | Notes |
  |----------|---------|------------|-------|
  | `foo()` | position.cpp, search.cpp | 3 | Always called with same args |
  | `bar()` | notation.cpp | 1 | Only one caller |
- **Identify patterns**:
  - Functions with zero callers → dead code
  - Functions with one caller → existence questionable
  - Groups of functions that differ only by one parameter → consolidation candidate
  - Overloads that exist for "convenience" → evaluate whether the convenience justifies the API surface
  - Functions that always receive the same subset of args → parameter reduction candidate

**Output**: Usage table with patterns highlighted.

## Step 3 — Blank-Slate Redesign

This is the core of the skill. Forget the current code exists entirely.

### The question
> "Given only the **requirements** this module must satisfy (the set of operations callers need), how would I design its API from scratch?"

### Process

1. **Extract requirements from usage**: From the usage table (Step 2), rewrite what each caller *actually needs* as a requirement — not which function it calls, but what operation it performs.

   Bad: "search.cpp calls `generateCaptures(ctx, moves)`"
   Good: "search needs to generate only capture moves, reusing a pre-built legality context"

   This dissociation is critical. Current function boundaries are an implementation artifact, not a requirement. A caller that uses three functions today might need only one well-designed function.

2. **Design the ideal API**: Propose function signatures that satisfy all requirements with:
   - **Minimal surface**: Fewest functions that cover all use cases. Use parameters (enums, templates, flags) to collapse near-duplicates into a single function.
   - **Consistent patterns**: Same style of overloading, same parameter conventions, same naming scheme.
   - **Clear semantics**: Each function has one obvious purpose. No "convenience" variants unless they genuinely simplify common call patterns.
   - **No leaked internals**: The API should not expose implementation details (helper types, intermediate steps) that callers don't need.

3. **Map current → ideal**: For each current public function, show where it maps in the new design (merged into X, eliminated, kept as-is). Document the rationale for each decision.

4. **Count the reduction**: Current API has N functions → ideal has M.

**Output**: The ideal API design with function signatures, the current-to-ideal mapping, and the rationale.

## Step 4 — Gap Analysis

Compare the current API against the ideal design. For each difference:

- **What changed**: Which functions were merged, eliminated, or reparameterized
- **Caller impact**: How many call sites need updating? Is it a simple signature change or a logic restructuring?
- **Risk**: Does this change touch hot paths, test infrastructure, or external interfaces?
- **Worth it?**: Is the API improvement worth the churn?
  - High-churn, low-gain → skip (document as "known debt" in instruction file)
  - Low-churn, high-gain → implement
  - High-churn, high-gain → implement if the module is actively maintained

**Output**: Prioritized list of changes with caller impact and go/no-go recommendation.

**Gate**: Present the gap analysis. User selects which changes to implement.

## Step 5 — Implement the Delta

Apply the approved changes, working from the current state toward the ideal.

### Ordering strategy
1. **Internal changes first**: Rename/restructure internal helpers before touching the public API. This keeps the public surface stable while internals settle.
2. **One public API change at a time**: Each change should leave the codebase buildable and testable.
3. **Update callers atomically**: When changing a function signature, update ALL callers in the same operation. Never leave broken call sites.
4. **Build + test after each change**: Every intermediate state must be green.

### For each change:
1. Mark as in-progress
2. Apply the change (implementation + all callers + header)
3. Build
4. Run tests
5. Mark as complete with results

## Step 6 — Validate & Document

After all changes are applied:

- **Run full test suite**: All tests must pass
- **Compare final API surface**: List the before/after public function count and signatures
- **Update documentation**:
  - Module instruction file (`.github/instructions/*.instructions.md`)
  - Parent library instruction file (`core.instructions.md` or `game-library.instructions.md`)
  - Architecture docs if API relationships changed
  - Code comments for any non-obvious design choices
- **Record the ideal**: If any gap analysis items were deferred (high-churn, documented as known debt), note them in the instruction file so future work can close the gap.

**Output**: Summary report:
- Public API: N → M functions
- Implementation: X → Y lines
- Key design changes and their rationale
- Any deferred items (known debt)

## Principles

- **Requirements, not functions** — the starting point for redesign is "what do callers need to do?", not "what functions exist today?". Current function boundaries are an implementation detail, not a requirement.
- **Dissociate before designing** — never look at the current signatures while designing the ideal API. Write the requirements in plain language first, then design from those. If you catch yourself thinking "well, there's already a function for that" — you're anchored to the current design.
- **Parameters over proliferation** — when multiple functions differ only by a mode (all vs captures vs quiets), collapse them into one function with a mode parameter. The mode can be an enum, a template parameter, or a FilterMode-style flag.
- **Not every gap is worth closing** — the gap analysis may reveal changes that are theoretically better but not worth the churn. A 20-caller change to save one function signature is rarely justified. Document it and move on.
- **Preserve behavior exactly** — this is a design transformation, not a feature addition. If a test fails, the redesign is wrong. Fix the design, not the test.
- **Name things for what they do** — after restructuring, ensure every function name clearly communicates its purpose. If you can't name it concisely, the abstraction is wrong.

## Related Skills

- **optimization** — use for behavior-preserving performance tuning and simplification within the current API surface (dead code removal, inlining wrappers, reducing complexity). Optimization works *within* the current design; redesign questions the design itself.
- **refactoring** — use when the redesign reveals that the module boundary itself is wrong: code that belongs in another module, dependency cycles, class hierarchy problems, file reorganization. Redesign stays within one module's API; refactoring crosses module boundaries.
- **audit** — use for broad, read-only codebase exploration before deciding which modules need redesign. An audit report may identify modules with overgrown APIs as candidates for this workflow.
