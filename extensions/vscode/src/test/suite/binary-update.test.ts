import * as assert from "node:assert";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

import type * as vscode from "vscode";

import { shouldUpdateBinary } from "../../utils/binary/update";
import { LSP_CONFIG } from "../../utils/binary/types";
import { getBinaryFilename } from "../../utils/binary/platform";
import type { GithubRelease } from "../../utils/binary/types";

function makeRelease(tag: string): GithubRelease {
    return { tag_name: tag, assets: [] };
}

function makeContext(
    installed_tag: string | undefined,
    storage_dir: string,
): vscode.ExtensionContext {
    return {
        globalState: {
            get: <T>(key: string): T | undefined =>
                key === LSP_CONFIG.installed_tag_key ? (installed_tag as unknown as T) : undefined,
        },
        globalStorageUri: { fsPath: storage_dir },
    } as unknown as vscode.ExtensionContext;
}

suite("shouldUpdateBinary", () => {
    let storage_dir: string;

    setup(() => {
        storage_dir = fs.mkdtempSync(path.join(os.tmpdir(), "luma-update-"));
    });

    teardown(() => {
        fs.rmSync(storage_dir, { recursive: true, force: true });
    });

    test("returns false when the installed tag already matches the release", () => {
        const context = makeContext("v1.2.0", storage_dir);
        assert.strictEqual(
            shouldUpdateBinary(LSP_CONFIG, context, makeRelease("v1.2.0"), "silent"),
            false,
        );
    });

    test("returns true when the installed tag differs from the release", () => {
        const context = makeContext("v1.0.0", storage_dir);
        assert.strictEqual(
            shouldUpdateBinary(LSP_CONFIG, context, makeRelease("v1.2.0"), "silent"),
            true,
        );
    });

    test("returns false when nothing is installed and no bundled binary exists", () => {
        const context = makeContext(undefined, storage_dir);
        assert.strictEqual(
            shouldUpdateBinary(LSP_CONFIG, context, makeRelease("v1.2.0"), "silent"),
            false,
        );
    });

    test("returns true when nothing is installed but a bundled binary is present", () => {
        const bin_dir = path.join(storage_dir, "bin");
        fs.mkdirSync(bin_dir, { recursive: true });
        fs.writeFileSync(path.join(bin_dir, getBinaryFilename(LSP_CONFIG.name)), "stub");

        const context = makeContext(undefined, storage_dir);
        assert.strictEqual(
            shouldUpdateBinary(LSP_CONFIG, context, makeRelease("v1.2.0"), "silent"),
            true,
        );
    });

    test("interactive up-to-date check still returns false", () => {
        const context = makeContext("v2.0.0", storage_dir);
        assert.strictEqual(
            shouldUpdateBinary(LSP_CONFIG, context, makeRelease("v2.0.0"), "interactive"),
            false,
        );
    });
});
