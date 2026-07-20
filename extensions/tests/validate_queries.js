#!/usr/bin/env node
/**
 * Tree-sitter Query Validation
 *
 * Validates that all .scm query files are syntactically correct and
 * that highlight queries produce expected captures for known code patterns.
 *
 * Usage:
 *   cd extensions/zed/grammars/tree-sitter-luma
 *   npm install
 *   node ../../tests/validate_queries.js
 *
 * Requires: tree-sitter and tree-sitter-luma installed in the grammar directory.
 */

const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");

const GRAMMAR_DIR = path.resolve(
    __dirname,
    "../zed/grammars/tree-sitter-luma",
);
const ZED_QUERIES_DIR = path.resolve(
    __dirname,
    "../zed/languages/luma",
);

// ── Expected highlight captures for known code patterns ────────
//
// Each snippet is highlighted against the Zed query with `tree-sitter
// query` (see validate_highlights) and must yield the listed capture.
// These expectations track the Zed capture names: Zed labels boolean
// literals `@constant.builtin` and every control-flow keyword
// `@keyword.control` (the `none` literal is `@constant.builtin` too).
// Expression-only snippets are wrapped in a function body because a bare
// expression is not a valid top-level declaration.

const HIGHLIGHT_TESTS = [
    { name: "comments", code: "# this is a comment", expect: "comment" },
    {
        name: "annotation",
        code: "@test\nfunction void test_a() {}",
        expect: "attribute",
    },
    {
        name: "string literal",
        code: 'function void f() { string s = "hello world" }',
        expect: "string",
    },
    { name: "integer literal", code: "integer x = 42", expect: "number" },
    {
        name: "boolean literal",
        code: "boolean flag = true",
        expect: "constant.builtin",
    },
    {
        name: "control flow keywords",
        code: "function integer f() { if x > 0 { return 1 } else { return 0 } }",
        expect: "keyword.control",
    },
    {
        name: "declaration keywords",
        code: "record Point { number x, number y }",
        expect: "keyword",
    },
    { name: "built-in type", code: "integer count = 0", expect: "type.builtin" },
    {
        name: "function declaration name",
        code: "function void greet() {}",
        expect: "function",
    },
    {
        name: "mutable modifier",
        code: "mutable integer count = 0",
        expect: "keyword.modifier",
    },
    {
        name: "none literal",
        code: "function void f() { optional<integer> x = none }",
        expect: "constant.builtin",
    },
    {
        name: "qualified identifier",
        code: 'function void f() { string result = String.length(s) }',
        expect: "function.call",
    },
    {
        name: "pipe operator",
        code: "function void f() { x |> String.length() }",
        expect: "operator",
    },
    {
        name: "generic type parameter",
        code: "record Box<T> { number value }",
        expect: "type.parameter",
    },
    {
        name: "bounded generic type parameter",
        code: "function<T: Printable> void f(T x) {}",
        expect: "type.parameter",
    },
];

// ── Expected regex-injection targets for RegularExpression.* calls ──
//
// The regex injection must fire only on the *pattern* operand — never on
// the subject text or a replacement string. Each case lists the string
// literals that must (`include`) and must not (`exclude`) receive an
// `@injection.content` capture from injections.scm.

const INJECTION_TESTS = [
    {
        name: "matches — only the pattern operand is injected",
        code: 'function void f() { boolean b = RegularExpression.matches("SUBJ", "PAT") }',
        include: ["PAT"],
        exclude: ["SUBJ"],
    },
    {
        name: "replace — neither subject nor replacement is injected",
        code: 'function void f() { string r = RegularExpression.replace("SUBJ", "PAT", "REPL") }',
        include: ["PAT"],
        exclude: ["SUBJ", "REPL"],
    },
    {
        name: "is_valid — the sole argument is injected",
        code: 'function void f() { boolean b = RegularExpression.is_valid("PAT") }',
        include: ["PAT"],
        exclude: [],
    },
];

// ── Query syntax validation ────────────────────────────────────

function validate_query_syntax(query_dir, label) {
    if (!fs.existsSync(query_dir)) {
        console.log(`  SKIP  ${label} (directory not found)`);
        return 0;
    }

    const files = fs
        .readdirSync(query_dir)
        .filter((f) => f.endsWith(".scm"));

    if (files.length === 0) {
        console.log(`  SKIP  ${label} (no .scm files)`);
        return 0;
    }

    let failures = 0;

    for (const file of files) {
        const file_path = path.join(query_dir, file);
        const content = fs.readFileSync(file_path, "utf-8");

        // Validate basic syntax: balanced parentheses, no stray characters
        let paren_depth = 0;
        let bracket_depth = 0;
        let in_string = false;
        let in_comment = false;

        for (let i = 0; i < content.length; i++) {
            const ch = content[i];

            if (in_comment) {
                if (ch === "\n") {
                    in_comment = false;
                }
                continue;
            }

            if (ch === ";") {
                in_comment = true;
                continue;
            }

            if (ch === '"') {
                in_string = !in_string;
                continue;
            }

            if (in_string) {
                continue;
            }

            if (ch === "(") paren_depth++;
            if (ch === ")") paren_depth--;
            if (ch === "[") bracket_depth++;
            if (ch === "]") bracket_depth--;

            if (paren_depth < 0) {
                console.log(
                    `  FAIL  ${label}/${file}: unmatched ')' at offset ${i}`,
                );
                failures++;
                break;
            }
            if (bracket_depth < 0) {
                console.log(
                    `  FAIL  ${label}/${file}: unmatched ']' at offset ${i}`,
                );
                failures++;
                break;
            }
        }

        if (paren_depth !== 0) {
            console.log(
                `  FAIL  ${label}/${file}: unbalanced parentheses (depth=${paren_depth})`,
            );
            failures++;
        } else if (bracket_depth !== 0) {
            console.log(
                `  FAIL  ${label}/${file}: unbalanced brackets (depth=${bracket_depth})`,
            );
            failures++;
        } else {
            // Validate capture names follow @word pattern
            const captures = content.match(/@[\w.]+/g) || [];
            const invalid = captures.filter(
                (c) => !/^@[\w][\w.]*$/.test(c),
            );
            if (invalid.length > 0) {
                console.log(
                    `  FAIL  ${label}/${file}: invalid captures: ${invalid.join(", ")}`,
                );
                failures++;
            } else {
                console.log(`  PASS  ${label}/${file} (${captures.length} captures)`);
            }
        }
    }

    return failures;
}

// ── Highlight capture validation via tree-sitter ───────────────
//
// `tree-sitter query` loads the grammar from the current directory and
// applies an explicit query file, printing each match as
// `capture: N - <name>, ...`. Unlike `tree-sitter highlight`, it needs
// no global theme or parser-directory config, so it works directly
// against the in-repo grammar. Running it validates that the highlight
// query (a) compiles against the current grammar — catching dead
// "impossible" patterns that reference structures the grammar can never
// produce — and (b) assigns the expected capture to known snippets.

// Specific markers that mean the query itself failed to compile against the
// grammar (a real defect). The broader ERROR_DISPLAY_MARKERS set is only used
// to extract a human-readable line, so it must not drive the skip-vs-fail
// decision — otherwise a tree-sitter-unavailable environment could be
// misreported as a compile failure.
const QUERY_ERROR_MARKERS = ["Impossible pattern", "Query error"];
const ERROR_DISPLAY_MARKERS = [...QUERY_ERROR_MARKERS, "Error:"];

function run_query(query_file, source_code) {
    const tmp_file = path.join(GRAMMAR_DIR, "_validate_queries_probe.luma");
    try {
        fs.writeFileSync(tmp_file, source_code + "\n");
        const output = execSync(
            `npx tree-sitter query "${query_file}" "${tmp_file}" 2>&1`,
            { cwd: GRAMMAR_DIR, encoding: "utf-8", timeout: 30000 },
        );
        return { ok: true, output };
    } catch (err) {
        return { ok: false, output: (err.stdout || err.message || "").toString() };
    } finally {
        try {
            fs.unlinkSync(tmp_file);
        } catch {}
    }
}

function captured_names(output) {
    const names = new Set();
    const re = /capture:\s*\d+\s*-\s*([\w.]+)/g;
    let match;
    while ((match = re.exec(output)) !== null) {
        names.add(match[1]);
    }
    return names;
}

function captured_injection_texts(output) {
    const texts = [];
    const re = /capture:\s*\d+\s*-\s*injection\.content,[^\n]*?text:\s*`([^`]*)`/g;
    let match;
    while ((match = re.exec(output)) !== null) {
        texts.push(match[1]);
    }
    return texts;
}

function first_error_line(output) {
    return (
        output
            .split("\n")
            .map((line) => line.trim())
            .find((line) => ERROR_DISPLAY_MARKERS.some((marker) => line.includes(marker))) ||
        "unknown error"
    );
}

function is_query_error(output) {
    return QUERY_ERROR_MARKERS.some((marker) => output.includes(marker));
}

function validate_highlights() {
    let failures = 0;

    const zed_highlights = path.join(ZED_QUERIES_DIR, "highlights.scm");

    if (!fs.existsSync(zed_highlights)) {
        console.log("  SKIP  highlight tests (zed/highlights.scm not found)");
        return 0;
    }

    // Probe availability: skip the whole section if tree-sitter cannot run
    // here, but treat a genuine query-compile error as a real failure.
    const probe = run_query(zed_highlights, "integer x = 0");
    if (!probe.ok && !is_query_error(probe.output)) {
        console.log("  SKIP  highlight tests (tree-sitter query unavailable)");
        return 0;
    }

    for (const test of HIGHLIGHT_TESTS) {
        const result = run_query(zed_highlights, test.code);
        if (!result.ok) {
            console.log(
                `  FAIL  highlight: "${test.name}" — query did not compile: ${first_error_line(result.output)}`,
            );
            failures++;
            continue;
        }

        const names = captured_names(result.output);
        if (names.has(test.expect)) {
            console.log(`  PASS  highlight: "${test.name}"`);
        } else {
            console.log(
                `  FAIL  highlight: "${test.name}" — expected capture '${test.expect}', got [${[...names].sort().join(", ")}]`,
            );
            failures++;
        }
    }

    return failures;
}

function validate_injections() {
    let failures = 0;

    const zed_injections = path.join(ZED_QUERIES_DIR, "injections.scm");

    if (!fs.existsSync(zed_injections)) {
        console.log("  SKIP  injection tests (zed/injections.scm not found)");
        return 0;
    }

    // Probe availability with the same policy as highlights: skip when
    // tree-sitter query cannot run, but fail on a genuine query-compile error.
    const probe = run_query(zed_injections, "integer x = 0");
    if (!probe.ok && !is_query_error(probe.output)) {
        console.log("  SKIP  injection tests (tree-sitter query unavailable)");
        return 0;
    }

    for (const test of INJECTION_TESTS) {
        const result = run_query(zed_injections, test.code);
        if (!result.ok) {
            console.log(
                `  FAIL  injection: "${test.name}" — query did not compile: ${first_error_line(result.output)}`,
            );
            failures++;
            continue;
        }

        const texts = captured_injection_texts(result.output);
        const missing = test.include.filter((s) => !texts.includes(s));
        const leaked = test.exclude.filter((s) => texts.includes(s));

        if (missing.length === 0 && leaked.length === 0) {
            console.log(`  PASS  injection: "${test.name}"`);
        } else {
            const parts = [];
            if (missing.length > 0) {
                parts.push(`missing injection for [${missing.join(", ")}]`);
            }
            if (leaked.length > 0) {
                parts.push(`unexpected injection for [${leaked.join(", ")}]`);
            }
            console.log(
                `  FAIL  injection: "${test.name}" — ${parts.join("; ")} (captured [${texts.join(", ")}])`,
            );
            failures++;
        }
    }

    return failures;
}

// ── Main ────────────────────────────────────────────────────────

function main() {
    console.log("=== Tree-sitter Query Validation ===\n");

    let total_failures = 0;

    // 1. Validate query syntax.
    console.log("--- Zed Query Syntax ---");
    total_failures += validate_query_syntax(ZED_QUERIES_DIR, "zed");

    // 2. Validate highlights produce expected scopes.
    console.log("\n--- Highlight Capture Tests ---");
    total_failures += validate_highlights();

    // 3. Validate injections target only the regex pattern operand.
    console.log("\n--- Injection Capture Tests ---");
    total_failures += validate_injections();

    // Summary.
    console.log(
        `\n${total_failures === 0 ? "All checks passed." : `${total_failures} failure(s).`}`,
    );

    if (total_failures > 0) {
        process.exit(1);
    }
}

main();
