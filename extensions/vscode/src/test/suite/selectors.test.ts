import * as assert from "node:assert";
import type * as vscode from "vscode";

import { folderDocumentSelector } from "../../lsp/selectors";

function folder(uri: string, index: number): vscode.WorkspaceFolder {
    return {
        uri: { toString: () => uri } as vscode.Uri,
        name: "work",
        index,
    };
}

suite("folderDocumentSelector", () => {
    test("matches the folder with a relative pattern, not an fsPath glob", () => {
        // A Windows path or glob characters in the folder name broke the old
        // `${fsPath}/**` string pattern.
        const uri = "file:///c%3A/Users/me/proj%5B1%5D";
        const [file_filter] = folderDocumentSelector(folder(uri, 1));

        assert.deepStrictEqual(file_filter, {
            scheme: "file",
            language: "luma",
            pattern: { baseUri: uri, pattern: "**/*" },
        });
    });

    test("only the first folder's client serves untitled documents", () => {
        const first = folderDocumentSelector(folder("file:///a", 0));
        const second = folderDocumentSelector(folder("file:///b", 1));

        assert.ok(
            first.some((f) => typeof f === "object" && "scheme" in f && f.scheme === "untitled"),
        );
        assert.ok(
            !second.some((f) => typeof f === "object" && "scheme" in f && f.scheme === "untitled"),
        );
    });
});
