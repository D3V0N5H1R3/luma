import * as assert from "node:assert";
import type * as vscode from "vscode";
import { Minimatch } from "minimatch";
import { matchGlobPattern } from "vscode-languageclient/$test/common/utils/globPattern";

import { installMinimatchCompat } from "../../lsp/minimatch-compat";

type CompatMinimatch = Minimatch & { makeRe(): boolean };

function resource(fs_path: string): vscode.Uri {
    return { fsPath: fs_path } as vscode.Uri;
}

suite("installMinimatchCompat", () => {
    suiteSetup(() => {
        installMinimatchCompat();
    });

    test("restores makeRe as the validity check vscode-languageclient expects", () => {
        assert.ok((new Minimatch("**/*.luma") as CompatMinimatch).makeRe());
        // minimatch 10 returned false for patterns that compile to an empty set.
        assert.strictEqual((new Minimatch("# comment") as CompatMinimatch).makeRe(), false);
        assert.strictEqual((new Minimatch("") as CompatMinimatch).makeRe(), false);
    });

    test("is idempotent", () => {
        const before = (Minimatch.prototype as CompatMinimatch).makeRe;
        installMinimatchCompat();
        assert.strictEqual((Minimatch.prototype as CompatMinimatch).makeRe, before);
    });

    test("lets vscode-languageclient match document-selector glob patterns", () => {
        // matchGlobPattern calls makeRe() before match(); without the compat
        // method it throws a TypeError under minimatch 11.
        assert.strictEqual(matchGlobPattern("/work/**/*.luma", resource("/work/src/a.luma")), true);
        assert.strictEqual(matchGlobPattern("/work/**/*.luma", resource("/work/src/a.txt")), false);
        assert.strictEqual(matchGlobPattern("# comment", resource("/work/src/a.luma")), false);
    });
});
