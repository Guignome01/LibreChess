---
name: refactoring
description: "**WORKFLOW SKILL** — Structured multi-phase refactoring for cross-module structural transformations. USE FOR: moving code between modules; extracting/merging classes; splitting/merging files; class hierarchy changes (flattening inheritance, extracting base classes); dependency restructuring (breaking cycles, inverting dependencies); fixing systemic bugs across multiple files; codebase-wide convention standardization. DO NOT USE FOR: single-module API surface redesign (use redesign skill); single-file bug fixes; performance tuning without structural changes (use optimization skill); read-only quality review (use audit skill). INVOKES: file system tools, terminal (build/test), subagents for codebase exploration."
argument-hint: "Describe the structural transformation (e.g., 'merge base classes', 'extract shared logic into new module', 'flatten engine provider hierarchy')"
---

# Structured Refactoring Workflow

A disciplined, phased approach to cross-module structural transformations. Follow each step in order. Never skip the analysis or planning phases — rushing to implementation causes regressions and scope creep.

## Step 1 — Deep Analysis

Thoroughly understand the code before proposing changes.

- Read every file in the affected area (not just headers — read implementations)
- Read project documentation if available (architecture docs, READMEs, contributing guides, project instructions)
- Trace data flows and call chains end-to-end
- Identify all callers, dependents, and integration points
- Note existing patterns, conventions, and naming styles used in the codebase
- Map the test coverage: which files have tests, what's covered, what's missing

**Output**: Present findings to the user — what you found, what's working well, what the problems are. Include concrete code references (files and line numbers).

## Step 2 — Detailed Plan

Write a phased plan and present it for approval before any code changes.

The plan must cover:
- **Phases**: Logical groups of changes that can be implemented and verified independently
- **Scope per phase**: Exactly which files are created, modified, or deleted
- **Rationale**: Why each change is made — not just what, but why
- **Risk areas**: What could break, what needs extra care
- **Test strategy**: What tests need updating, what new tests are needed
- **Recommendations** (optional): If you see opportunities beyond the stated goal, mention them — but clearly separate "required by the plan" from "suggestion"

Keep phases scoped and sequential. Each phase should leave the codebase in a buildable, testable state.

After drafting the plan, cross-check every high-level requirement and finding against the technical steps — ensure nothing was dropped between analysis and plan.

**Gate**: Wait for user approval before proceeding. The user may adjust scope, reorder phases, or reject parts of the plan.

## Step 3 — Add Missing Tests (if needed)

Before changing production code, ensure adequate test coverage for the affected areas.

- Identify gaps: edge cases, untested branches, missing integration tests
- Write the tests against the **current** code — they must all pass before refactoring begins
- Run the full test suite to establish the green baseline

If no new tests are needed (existing coverage is sufficient, or the changes don't affect testable logic), skip this step and note why.

**Gate**: All tests must pass. Get user approval if significant new tests were added.

## Step 4 — Implement Phase by Phase

Work through the approved plan one phase at a time.

For each phase:
1. Mark the phase as in-progress (use task tracking)
2. Implement the changes for that phase only — do not leak into other phases
3. Build to verify compilation (if applicable)
4. Run tests to verify no regressions
5. Mark the phase as complete

Stay disciplined about scope. If you discover something that needs changing but isn't in the plan, note it for Step 5 — don't address it mid-phase unless it blocks progress.

## Step 5 — Self-Review

After all phases are complete, do a thorough review of everything that changed.

Check for:
- **Dead code**: Unused functions, unreachable branches, stale variables, orphaned includes
- **Outdated comments**: Comments that describe the old behavior, TODO markers that are now done
- **Naming inconsistencies**: Old names leaking through, mismatched terminology
- **Duplicated logic**: Code that was supposed to be extracted but got copied instead
- **Bug introduction**: Logic errors, off-by-one, missing null checks at boundaries
- **Pattern violations**: Changes that break conventions established elsewhere in the codebase
- **Unnecessary complexity**: Over-engineering that crept in — intermediate classes, abstractions, or indirection that don't earn their keep

**Decision tree for findings:**
- **Minor and scoped** (dead variable, stale comment, small inconsistency): Fix it immediately, then repeat Step 5 on the affected area.
- **Major or cross-cutting** (design issue, missed requirement, significant bug): Stop and present the finding to the user. Options:
  - Finish the current plan first, then start a new plan for the issue
  - Pause the current plan, return to Step 1 to revise it
  - Defer it entirely (document it for later)

Also offer suggestions if you see good improvement opportunities, clearly labeled as optional.

## Step 6 — Final Validation

Run the complete test suite one final time to confirm everything is green.

- Build all targets (if multiple build environments exist)
- Run all tests
- If any test fails, fix the production code (never modify a test to make it pass)

Skip this step only if the changes are purely non-functional (documentation-only, comment-only) and no tests exist for the affected area.

## Step 7 — Update Documentation

Update all documentation that references the changed code.

This includes:
- Architecture docs, API docs, project structure docs
- README files
- Project instruction files (copilot-instructions.md or equivalent)
- Inline code comments (only where the logic changed and the old comment is now wrong)
- Changelog or migration notes (if the project uses them)

Documentation must reflect the final state of the code, not the intermediate states.

## Principles

These principles govern every step:

- **Tests guard correctness** — never modify a test to accommodate a bug. If a test fails, fix the production code.
- **Each phase is self-contained** — the codebase must build and pass tests after every phase, not just at the end.
- **Plan first, code second** — the urge to "just start coding" leads to scope creep and regressions.
- **Scope discipline** — if it's not in the approved plan, it goes in the notes for Step 5, not in the current diff.
- **User gates matter** — don't proceed past a gate without explicit user approval.
- **Simplicity over cleverness** — prefer flat hierarchies, direct solutions, and minimal indirection. Every abstraction must earn its place.
- **Clean as you go** — remove dead code, fix stale comments, and update names in the same change that makes them stale. Don't leave cleanup for "later."
- **Verify plan completeness** — before seeking user approval, re-read the plan against the original findings and requirements. Every bullet from analysis must map to a concrete step.

## Related Skills

- **redesign** — use when the problem is a single module's API surface (too many functions, near-duplicate overloads, parameter proliferation). Redesign stays within one module's boundary and uses blank-slate thinking to find the ideal API. If a redesign reveals that the module boundary itself is wrong (code belongs elsewhere, dependency should be inverted), hand off to this refactoring skill.
- **optimization** — use when the goal is performance tuning, complexity reduction, or dead-code elimination *without* structural changes. If profiling reveals an architectural bottleneck, switch back to this refactoring skill.
- **audit** — use when you want to explore the codebase for quality issues before deciding what to refactor. An audit report may identify architectural violations or cross-cutting inconsistencies that become refactoring targets.
