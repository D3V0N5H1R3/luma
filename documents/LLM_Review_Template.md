# LLM Project Review — Template & Prompt

## Purpose

This document defines the standard structure and prompt for multi-LLM project reviews. Each LLM produces a review document following the structure below, then a final consolidation LLM reviews all findings and acts on them.

---

## Output File Naming

Each reviewing LLM saves its findings as:

```
documents/reviews/<Model_Name>_Review.md
```

Examples: `Claude_Sonnet_Review.md`, `ChatGPT_Terra_Review.md`, `Gemini_Flash_Review.md`

---

## Review Document Structure

Every review document must follow this exact structure:

```markdown
# <Model Name> — Luma Project Review

**Date:** YYYY-MM-DD
**Model:** <full model identifier>
**Scope:** Full project review

## Executive Summary

<!-- 3–5 sentence high-level assessment of the project's health, maturity, and
     most critical findings. -->

## 1. Architecture & Design

### 1.1 Strengths
<!-- Bullet list of architectural decisions done well. -->

### 1.2 Issues
<!-- Each issue as a subsection: -->
#### Issue: <short title>
- **Severity:** Critical | High | Medium | Low
- **Location:** <file path(s) or module(s)>
- **Description:** <what the problem is>
- **Recommendation:** <concrete fix or improvement>

## 2. Code Quality & Style

### 2.1 Strengths

### 2.2 Issues
#### Issue: <short title>
- **Severity:** Critical | High | Medium | Low
- **Location:** <file path(s)>
- **Description:**
- **Recommendation:**

## 3. Correctness & Bugs

### 3.1 Confirmed Bugs
#### Bug: <short title>
- **Severity:** Critical | High | Medium | Low
- **Location:** <file path with line number if possible>
- **Reproduction:** <how to trigger>
- **Root Cause:** <why it happens>
- **Fix:** <proposed fix>

### 3.2 Suspected Bugs
#### Suspect: <short title>
- **Confidence:** High | Medium | Low
- **Location:**
- **Reasoning:**

## 4. Security

### 4.1 Vulnerabilities
#### Vulnerability: <short title>
- **Severity:** Critical | High | Medium | Low
- **CWE:** <CWE ID if applicable>
- **Location:**
- **Description:**
- **Mitigation:**

### 4.2 Hardening Recommendations
<!-- Bullet list of proactive security improvements. -->

## 5. Performance

### 5.1 Bottlenecks
#### Bottleneck: <short title>
- **Severity:** High | Medium | Low
- **Location:**
- **Impact:** <quantified if possible>
- **Recommendation:**

### 5.2 Optimization Opportunities
<!-- Bullet list. -->

## 6. Testing & Quality Assurance

### 6.1 Coverage Gaps
<!-- Areas lacking test coverage. -->

### 6.2 Test Quality Issues
<!-- Flaky tests, poor assertions, missing edge cases. -->

### 6.3 Recommendations

## 7. Documentation

### 7.1 Issues
#### Issue: <short title>
- **Severity:** High | Medium | Low
- **Location:**
- **Description:**

### 7.2 Missing Documentation
<!-- Bullet list of undocumented areas. -->

## 8. Dependencies & Build System

### 8.1 Issues
<!-- Dependency risks, build problems, CI gaps. -->

### 8.2 Recommendations

## 9. Usability & Developer Experience

### 9.1 Issues
<!-- Error messages, CLI UX, API ergonomics. -->

### 9.2 Recommendations

## 10. Summary Table

| # | Category | Title | Severity | Location |
|---|----------|-------|----------|----------|
| 1 | ...      | ...   | ...      | ...      |

<!-- Number every finding sequentially. This table must include ALL issues,
     bugs, vulnerabilities, and bottlenecks from sections 1–9. -->

## 11. Top 5 Priority Actions

1. ...
2. ...
3. ...
4. ...
5. ...
```

---

## Review Prompt

Copy the prompt below and provide it (along with project access) to each reviewing LLM:

---

### PROMPT START

You are performing a comprehensive project review of **Luma**, an interpreted, statically typed, expression-oriented programming language. The interpreter is written in C++20 with a bytecode compiler and stack-based VM. The project also includes an LSP language server, a DAP debugger, and editor extensions for VS Code and Zed.

**Your task:** Review the entire project and produce a structured review document following the exact template below. Be thorough, specific, and actionable. Every finding must include a file path or module reference. Do not report style nitpicks unless they indicate a deeper problem. Focus on:

1. **Architecture & Design** — modularity, coupling, separation of concerns, scalability
2. **Code Quality & Style** — consistency, readability, modern idioms, anti-patterns
3. **Correctness & Bugs** — logic errors, edge cases, undefined behavior, resource leaks
4. **Security** — injection, path traversal, resource exhaustion, unsafe operations
5. **Performance** — algorithmic inefficiency, unnecessary allocations, hot-path issues
6. **Testing & QA** — coverage gaps, assertion quality, flaky tests, missing edge cases
7. **Documentation** — accuracy, completeness, staleness, discoverability
8. **Dependencies & Build** — version pinning, CI robustness, reproducibility
9. **Usability & DX** — error messages, CLI ergonomics, API design, onboarding

**Key documents to read first:**
- `documents/DIRECTORY.md` — documentation index
- `.github/copilot-instructions.md` — project context and architecture
- `instructions/learnings.instructions.md` — known pitfalls and patterns
- `documents/Luma_Software_Architecture.md` — architecture design
- `documents/Luma_User_Manual.md` — language reference

**Constraints:**
- Only report issues you have high or medium confidence in. Mark confidence level.
- Provide concrete file paths and line numbers where possible.
- Each issue must have a severity (Critical / High / Medium / Low).
- Include a summary table at the end listing ALL findings.
- Finish with your top 5 priority actions.
- Save your output as `documents/reviews/<Your_Model_Name>_Review.md`.

**Use the exact document structure defined in `documents/LLM_Review_Template.md`.**

### PROMPT END

---

## Consolidation Phase

After all LLMs have submitted their reviews, provide the following prompt to the consolidating LLM:

---

### CONSOLIDATION PROMPT START

You are the consolidation reviewer. Multiple LLMs have independently reviewed the Luma project. Their review documents are in `documents/reviews/`.

**Your task:**

1. Read all review documents in `documents/reviews/`.
2. Cross-reference findings: identify consensus issues (reported by 2+ LLMs), unique findings, and contradictions.
3. Produce a consolidated report at `documents/reviews/Consolidated_Review.md` with:
   - **Consensus findings** (agreed by multiple reviewers) — highest priority
   - **Unique high-value findings** (reported by one LLM but clearly valid)
   - **Contradictions** (reviewers disagree — state both positions)
   - **False positives** (findings you determine are incorrect — explain why)
   - **Final priority-ordered action list** (top 10)
4. For each item in the action list that you determine should be fixed:
   - Implement the fix directly in the codebase.
   - Run relevant tests to verify the fix doesn't break anything.
   - Note what you fixed in the consolidated report.
5. For items you decide NOT to fix, document your reasoning.

**Output:** `documents/reviews/Consolidated_Review.md` + code fixes applied to the repository.

### CONSOLIDATION PROMPT END
