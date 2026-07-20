import * as assert from "node:assert";

import type * as vscode from "vscode";

import {
    findIncludeMatches,
    MUTABLE_DIAGNOSTIC_PATTERN,
    ALREADY_MUTABLE_PATTERN,
    UNKNOWN_IDENT_DIAGNOSTIC_PATTERN,
    IDENT_NAME_PATTERN,
} from "../../lsp/code-actions";
import { LUMA_TYPE_PATTERN } from "../../utils/constants";

function createUri(fs_path: string): vscode.Uri {
    return {
        fsPath: fs_path,
        toString: () => fs_path,
    } as unknown as vscode.Uri;
}

suite("findIncludeMatches", () => {
    test("reads candidate files concurrently while preserving input order", async () => {
        const current_file = createUri("C:\\repo\\current.luma");
        const first_match = createUri("C:\\repo\\alpha.luma");
        const self_file = createUri("C:\\repo\\current.luma");
        const non_match = createUri("C:\\repo\\beta.luma");
        const second_match = createUri("C:\\repo\\gamma.luma");

        const contents = new Map<string, string>([
            [first_match.fsPath, "function void target() {}"],
            [self_file.fsPath, "function void target() {}"],
            [non_match.fsPath, "function void retargeted() {}"],
            [second_match.fsPath, "target = 1"],
        ]);

        const read_order: string[] = [];
        const matches = await findIncludeMatches(
            "target",
            current_file,
            [first_match, self_file, non_match, second_match],
            async (file) => {
                read_order.push(file.fsPath);
                return Buffer.from(contents.get(file.fsPath) ?? "", "utf-8");
            },
            (file) => file.fsPath.replace("C:\\repo\\", ""),
        );

        assert.deepStrictEqual(read_order, [
            "C:\\repo\\alpha.luma",
            "C:\\repo\\beta.luma",
            "C:\\repo\\gamma.luma",
        ]);
        assert.deepStrictEqual(
            matches.map((match) => match.relative),
            ["alpha.luma", "gamma.luma"],
        );
    });
});

suite("MUTABLE_DIAGNOSTIC_PATTERN", () => {
    test("matches immutable-assignment diagnostics", () => {
        assert.ok(MUTABLE_DIAGNOSTIC_PATTERN.test("cannot assign to immutable variable 'count'"));
        assert.ok(MUTABLE_DIAGNOSTIC_PATTERN.test("cannot mutate value of type 'string'"));
        assert.ok(MUTABLE_DIAGNOSTIC_PATTERN.test("variable 'x' is immutable"));
    });

    test("does not match unrelated diagnostics", () => {
        assert.ok(!MUTABLE_DIAGNOSTIC_PATTERN.test("type mismatch: expected integer, got string"));
    });
});

suite("ALREADY_MUTABLE_PATTERN", () => {
    test("matches a declaration already marked mutable (with indentation)", () => {
        assert.ok(ALREADY_MUTABLE_PATTERN.test("mutable integer count = 0"));
        assert.ok(ALREADY_MUTABLE_PATTERN.test('    mutable string name = "x"'));
    });

    test("does not match a plain immutable declaration", () => {
        assert.ok(!ALREADY_MUTABLE_PATTERN.test("integer count = 0"));
        assert.ok(!ALREADY_MUTABLE_PATTERN.test("mutablexyz = 1"));
    });
});

suite("UNKNOWN_IDENT_DIAGNOSTIC_PATTERN", () => {
    test("matches unknown identifier/module/function and not-defined diagnostics", () => {
        assert.ok(UNKNOWN_IDENT_DIAGNOSTIC_PATTERN.test("unknown identifier 'helper'"));
        assert.ok(UNKNOWN_IDENT_DIAGNOSTIC_PATTERN.test("unknown module 'Utils'"));
        assert.ok(UNKNOWN_IDENT_DIAGNOSTIC_PATTERN.test("unknown function 'compute'"));
        assert.ok(UNKNOWN_IDENT_DIAGNOSTIC_PATTERN.test("symbol 'foo' is not defined"));
    });

    test("does not match unrelated diagnostics", () => {
        assert.ok(!UNKNOWN_IDENT_DIAGNOSTIC_PATTERN.test("unused variable 'x'"));
    });
});

suite("IDENT_NAME_PATTERN", () => {
    test("captures the offending identifier name", () => {
        assert.strictEqual(IDENT_NAME_PATTERN.exec("unknown identifier 'helper'")?.[1], "helper");
        assert.strictEqual(IDENT_NAME_PATTERN.exec("unknown module `Utils`")?.[1], "Utils");
        assert.strictEqual(
            IDENT_NAME_PATTERN.exec("unknown function 'compute_sum'")?.[1],
            "compute_sum",
        );
    });

    test("does not capture from a not-defined phrasing without the quoted name", () => {
        assert.strictEqual(IDENT_NAME_PATTERN.exec("symbol 'foo' is not defined"), null);
    });
});

suite("LUMA_TYPE_PATTERN", () => {
    test("captures leading whitespace and the first word of a declaration", () => {
        const match = LUMA_TYPE_PATTERN.exec("  integer count = 0");
        assert.ok(match);
        assert.strictEqual(match[1], "  ");
        assert.strictEqual(match[2], "integer");
    });

    test("captures deeper indentation and the declared type", () => {
        const match = LUMA_TYPE_PATTERN.exec('    string name = "hello"');
        assert.ok(match);
        assert.strictEqual(match[1], "    ");
        assert.strictEqual(match[2], "string");
    });
});
