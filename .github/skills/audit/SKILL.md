---
name: audit
description: "**WORKFLOW SKILL** — Exploratory codebase analysis that surfaces quality issues, inconsistencies, security concerns, and unclear code. USE FOR: broad code quality review; architecture compliance checks; finding inconsistencies across files; spotting security issues; identifying doc/code drift; evaluating test coverage gaps; asking clarifying questions about ambiguous code. DO NOT USE FOR: applying fixes (use optimization or refactoring skills); single-file bug investigation; adding features; performance benchmarking. INVOKES: file system tools, subagents for parallel codebase exploration, vscode_askQuestions for interactive clarification."
argument-hint: "Describe the audit scope (e.g., 'audit src/engine', 'audit the full codebase', 'audit security of WiFi and API code', 'audit docs for consistency with code')"
---

# Code Audit Workflow

A structured exploratory workflow that examines the codebase and produces a findings report with actionable recommendations. Unlike optimization and refactoring, this skill **never modifies code** — it discovers and categorizes issues, then recommends which skill or approach to use for each fix.

## Step 1 — Scope & Context

Determine what to audit and load all relevant context.

### If the user specifies an area (e.g., "audit src/engine"):
- Read every file in the target area — implementations, not just headers
- Load the relevant instruction files (check `applyTo` patterns in `.github/instructions/`)
- Load `copilot-instructions.md` for architecture principles
- Note the area's role in the overall architecture

### If the user requests a full-codebase audit:
- Use parallel Explore subagents to read major areas simultaneously:
  - `lib/core/` — chess library
  - `src/` (excluding `web/`) — firmware, drivers, game modes, engine providers
  - `src/web/` — frontend
  - `test/` — test suite
  - `docs/` + `.github/instructions/` — documentation
- Load `copilot-instructions.md` and all instruction files
- Build a mental map of the codebase before analyzing

### For either scope:
- Identify which audit dimensions (Step 2) are relevant. Skip dimensions that don't apply to the target area (e.g., skip "ESP32 resource hygiene" when auditing docs, skip "documentation health" when the user only cares about security).
- Tell the user which dimensions you'll cover and which you'll skip, with a brief reason for each skip.

## Step 2 — Systematic Analysis

Work through each applicable audit dimension. For every finding, record:
- **What**: The specific issue, with file and line references
- **Severity**: `critical` (security risk, correctness bug, data loss potential), `warning` (inconsistency, code smell, maintenance risk), or `suggestion` (improvement opportunity, style concern)
- **Why it matters**: One sentence on the impact if left unaddressed
- **Recommendation**: What to do about it, and which skill to use (optimization, refactoring, or a direct fix)

### Audit Dimensions

Work through these in order. Skip any that don't apply to the scoped area.

#### 1. Architecture Compliance
Cross-reference code against the principles in `copilot-instructions.md` and scoped instruction files:
- **Separation of concerns** — does each class own a single responsibility? Is hardware mixed with logic? Network with chess rules?
- **Loose coupling** — pointer injection, no global state, minimal public APIs. Are there hidden dependencies or tight coupling?
- **Game as sole entry point** — does any firmware code (`src/`) include `Position`, `History`, `movegen`, `rules`, or `iterator` directly?
- **Composition over inheritance** — are there unnecessary inheritance chains? Intermediate classes that add indirection without value?
- **Design decisions respected** — check each "Design Decisions" section in instruction files against the actual code

#### 2. Security
Focus on system boundaries where untrusted input enters:
- **Input validation** — API endpoints, FEN parsing from user input, WiFi credentials, engine responses
- **Credential handling** — are secrets (API keys, passwords) stored securely in NVS? Exposed in logs or API responses?
- **API exposure** — are endpoints properly authenticated? Can one endpoint be used to access data it shouldn't?
- **TLS usage** — are external API calls (Stockfish, Lichess) made over HTTPS?
- **OTA security** — is firmware update authenticated and verified?
- **Injection vectors** — string concatenation in HTTP responses, unsanitized user input in HTML/JSON

#### 3. Consistency
Look for patterns that vary without reason across the codebase:
- **Naming conventions** — do similar things have similar names? Are abbreviations consistent (e.g., `pos` vs `position`, `cfg` vs `config`)?
- **Error handling** — is error handling uniform? Some functions return bool, others return error codes, others use exceptions?
- **Logging** — consistent use of `ILogger`? Log levels used appropriately? Missing logs where other similar operations log?
- **Parameter ordering** — do similar functions take parameters in the same order?
- **Include style** — consistent include ordering, guards vs pragma once, relative vs absolute paths?
- **Code patterns** — similar operations implemented differently in different files without justification
- **Helper extraction opportunities** — repeated multi-line logic blocks across files that could become shared helpers: identical struct construction, duplicated conditional patterns, same ternary expression (3+ occurrences), same loop structure with minor parameter differences. Flag as DRY violations with concrete extraction recommendations.
- **Helper generalization opportunities** — multiple specific helpers or inline patterns that do almost the same thing but vary slightly. Look for: inverse converter pairs with mirrored switch/mapping logic, functions that differ only in a hard-coded parameter value, private helpers whose logic is reimplemented elsewhere because they aren't accessible, and scattered inline expressions computing the same derived value. Recommend a single general helper parameterized to cover all cases.
- **Symmetric per-color code** — in chess evaluation, look for white/black mirrored logic that should be parameterized into a single color-agnostic loop. Compare each eval function's color loop pattern: does it declare `Color color = static_cast<Color>(c);` at the top, or use inline casts and hardcoded indices? Inconsistency signals missed unification.

#### 4. Code Clarity
Identify sections that would confuse a developer reading them for the first time:
- **Missing "why" comments** — non-obvious logic, workarounds, magic numbers, or surprising behavior with no explanation
- **Convoluted control flow** — deeply nested conditionals, long switch cascades, goto-like patterns
- **Magic numbers** — unnamed constants with unclear meaning
- **Boolean parameters** — functions with boolean flags that should be enums for readability
- **Doc/code drift** — documentation (README, instruction files, inline docs) that describes behavior different from what the code actually does
- **Misleading names** — variables or functions whose names suggest something different from what they do

#### 5. Test Coverage
Evaluate the quality and completeness of the test suite:
- **Untested public functions** — public methods in `lib/core/` that have no corresponding test
- **Missing edge cases** — tested functions that lack boundary condition tests (empty input, max values, error paths)
- **File mirroring convention** — does every `lib/core/src/*.cpp` have a matching `test/test_core/test_*.cpp`?
- **Assertion quality** — tests that call functions but don't meaningfully assert the result (testing that it "doesn't crash" isn't sufficient)
- **Test isolation** — tests that depend on execution order or share mutable state

#### 6. ESP32 Resource Hygiene
Evaluate resource usage patterns specific to the embedded target:
- **Stack sizing** — FreeRTOS task stack allocations vs actual usage. Over-allocated or dangerously tight?
- **Heap patterns** — dynamic allocation in hot paths, `String` concatenation loops, `std::vector` growth
- **String types** — `String` vs `std::string` mixing, unnecessary conversions at boundaries
- **Watchdog risks** — long-running loops without `yield()` or `vTaskDelay()`, blocking operations on the main loop
- **Flash wear** — frequent writes to NVS or LittleFS, missing wear-leveling considerations
- **Task priorities** — FreeRTOS task priority assignments, potential priority inversion

#### 7. Dead Code & Unused Exports
Identify code that serves no purpose:
- **Uncalled functions** — public/extern functions with zero callers. For each suspect, classify callers as *production* (`lib/`, `src/`) vs *test-only* (`test/`). Functions called only by tests are dead production code — the tests test themselves.
- **Unreachable branches** — conditions that can never be true given the data flow
- **Stale includes** — headers included but nothing from them is used
- **Orphaned files** — source files not compiled by any build target
- **Unused defines/constants** — `#define` or `constexpr` values referenced nowhere
- **Commented-out code** — blocks of commented code left "just in case"

#### 8. Documentation Health
Check that documentation accurately reflects the current codebase:
- **Instruction file sync** — do `.github/instructions/*.instructions.md` files match current code? Tables, API descriptions, design decisions still accurate?
- **README accuracy** — features listed that don't exist, missing features that do exist, outdated setup instructions
- **Architecture docs** — `docs/` files that describe removed or restructured components
- **Cross-reference integrity** — links between docs that point to renamed or deleted sections
- **Missing documentation** — significant features or components with no documentation at all

## Step 3 — Interactive Clarification

When the analysis encounters code that is ambiguous, potentially intentional, or hard to classify:

- **Pause and ask immediately** — use `vscode_askQuestions` to present the code context and what seems off
- **Frame questions neutrally** — "This pattern differs from the rest of the codebase. Is this intentional?" not "This code is wrong."
- **Use answers to refine findings** — reclassify, dismiss, or add context to the finding based on the user's response
- **Don't ask about obvious issues** — if it's clearly a bug or clearly violates a stated principle, just report it. Save questions for genuinely ambiguous cases.

Limit questions to avoid fatigue — batch related questions when possible, and skip questions where the finding is low-severity and the answer wouldn't change the recommendation.

## Step 4 — Findings Report

Present a structured report organized by audit dimension.

### Report Format

For each dimension that produced findings:

**Dimension Name** (X findings: Y critical, Z warnings, W suggestions)

| # | Severity | File(s) | Finding | Recommendation |
|---|----------|---------|---------|----------------|
| 1 | critical | `file.cpp:42` | Description | What to do + which skill |

### Report Summary

End with:
- **Top priorities** — the 3–5 most impactful findings, regardless of dimension
- **Quick wins** — findings that can be fixed in minutes with minimal risk
- **Deeper investigations** — findings that need more analysis before acting (potential optimization or refactoring skill invocations)
- **Overall health assessment** — one paragraph on the general state of the audited area

### What the report does NOT include:
- Code changes (audit never edits files)
- Performance benchmarks (use the optimization skill for that)
- Refactoring plans (use the refactoring skill for that)
- File creation or modification of any kind

## Principles

- **Observe, don't modify** — the audit skill produces findings and recommendations, never code changes. The user decides what to act on and which skill to use.
- **Evidence over opinion** — every finding must reference a specific file and line. "The code feels messy" is not a finding. "Function X in file.cpp:42 has 8 nesting levels and 3 boolean parameters" is.
- **Severity honesty** — don't inflate severity to make findings seem important. A naming inconsistency is a suggestion, not a warning. A missing input validation on a public API is critical.
- **Architecture principles as baseline** — the project's own documented principles (in `copilot-instructions.md` and instruction files) are the standard. Don't impose external style preferences — measure the code against its own stated goals.
- **Respect intentional decisions** — if an instruction file documents *why* something is done a certain way, don't flag it as an issue. If unsure whether a pattern is intentional, ask (Step 3).
- **Diminishing returns awareness** — a full-codebase audit doesn't need to find every issue. Focus on the most impactful findings. Stop a dimension when findings become low-value.
- **Actionable recommendations** — every finding must include a concrete next step. "This could be improved" is not actionable. "Extract the shared validation logic into a helper; use the optimization skill" is.

## Related Skills

- **optimization** — use to act on findings related to performance, complexity reduction, dead code elimination, or simplification. An audit finding like "these two functions share 90% of their logic" becomes an optimization target.
- **refactoring** — use to act on findings related to architectural violations, structural problems, or cross-cutting inconsistencies that require multi-file changes. An audit finding like "firmware code bypasses Game and includes Position directly" becomes a refactoring target.
