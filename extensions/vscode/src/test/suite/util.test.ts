import * as assert from "node:assert";

import * as vscode from "vscode";

import {
    extractErrorMessage,
    escapeRegex,
    resolvePath,
    resolveInterpreterPath,
} from "../../utils/util";

suite("extractErrorMessage", () => {
    test("should extract message from Error object", () => {
        assert.strictEqual(extractErrorMessage(new Error("test error")), "test error");
    });

    test("should convert string to message", () => {
        assert.strictEqual(extractErrorMessage("string error"), "string error");
    });

    test("should convert number to message", () => {
        assert.strictEqual(extractErrorMessage(42), "42");
    });

    test("should convert null to message", () => {
        assert.strictEqual(extractErrorMessage(null), "null");
    });

    test("should convert undefined to message", () => {
        assert.strictEqual(extractErrorMessage(undefined), "undefined");
    });

    test("should handle nested Error", () => {
        const err = new TypeError("type mismatch");
        assert.strictEqual(extractErrorMessage(err), "type mismatch");
    });
});

suite("escapeRegex", () => {
    test("escapes every regex metacharacter", () => {
        assert.strictEqual(
            escapeRegex(".*+?^${}()|[]\\"),
            "\\.\\*\\+\\?\\^\\$\\{\\}\\(\\)\\|\\[\\]\\\\",
        );
    });

    test("leaves ordinary characters untouched", () => {
        assert.strictEqual(escapeRegex("plain_name123"), "plain_name123");
    });

    test("produces a pattern that matches the original text literally", () => {
        const literal = "a.b(c)+d";
        const re = new RegExp(escapeRegex(literal));
        assert.ok(re.test(literal));
        assert.ok(!re.test("axb(c)+d"));
    });
});

// resolvePath reads vscode.workspace.workspaceFolders at call time, so testing
// its branches means controlling that value. That requires mutating the vscode
// namespace, which only works under the unit stub — the real electron runtime
// exposes workspaceFolders as a read-only getter and would throw on assignment.
// This *.test.ts file is loaded by BOTH the unit and integration runners, so the
// suite is registered only when the stub is active and skipped otherwise.
const UNDER_UNIT_STUB = (vscode as unknown as { IS_UNIT_STUB?: boolean }).IS_UNIT_STUB === true;

(UNDER_UNIT_STUB ? suite : suite.skip)("resolvePath", () => {
    const original_folders = vscode.workspace.workspaceFolders;

    function setFolders(folders: { name: string; fsPath: string }[] | undefined): void {
        (vscode.workspace as { workspaceFolders: unknown }).workspaceFolders = folders?.map(
            (f) => ({
                name: f.name,
                uri: { fsPath: f.fsPath },
            }),
        );
    }

    teardown(() => {
        (vscode.workspace as { workspaceFolders: unknown }).workspaceFolders = original_folders;
    });

    test("returns the input unchanged when it has no variables", () => {
        setFolders(undefined);
        assert.strictEqual(resolvePath("/absolute/path/to/luma"), "/absolute/path/to/luma");
    });

    test("expands ${workspaceFolder} to the first folder", () => {
        setFolders([{ name: "root", fsPath: "C:\\ws" }]);
        assert.strictEqual(resolvePath("${workspaceFolder}/build/luma"), "C:\\ws/build/luma");
    });

    test("throws when ${workspaceFolder} is used with no folder open", () => {
        setFolders(undefined);
        assert.throws(() => resolvePath("${workspaceFolder}/luma"), /no workspace folder is open/);
    });

    test("expands ${workspaceFolder:name} to the named folder", () => {
        setFolders([
            { name: "app", fsPath: "C:\\app" },
            { name: "lib", fsPath: "C:\\lib" },
        ]);
        assert.strictEqual(resolvePath("${workspaceFolder:lib}/luma"), "C:\\lib/luma");
    });

    test("throws when the named workspace folder is not found", () => {
        setFolders([{ name: "app", fsPath: "C:\\app" }]);
        assert.throws(
            () => resolvePath("${workspaceFolder:missing}/luma"),
            /'missing' was not found/,
        );
    });
});

suite("resolveInterpreterPath", () => {
    test("falls back to the PATH binary name when nothing is configured", () => {
        // The unit stub returns config defaults, so luma.path is empty here.
        assert.strictEqual(resolveInterpreterPath(), "luma");
    });
});
