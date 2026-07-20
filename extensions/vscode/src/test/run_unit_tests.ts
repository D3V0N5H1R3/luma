import * as path from "node:path";
import Mocha from "mocha";
import { glob } from "glob";

/**
 * Standalone unit test runner — runs pure-utility tests without VS Code.
 * Use `npm run test:unit` to invoke.
 *
 * Requires --require ./out/test/vscode_stub.js to provide a vscode mock.
 */
async function main(): Promise<void> {
    const mocha = new Mocha({ ui: "tdd", color: true, timeout: 10000 });
    const suite_dir = path.resolve(__dirname, "./suite");

    const files = await glob("**/**.test.js", { cwd: suite_dir });

    // Exclude tests that require the real VS Code runtime (integration tests).
    // grammar.test.js depends only on node fs/path/assert, so it runs here too.
    const INTEGRATION_TESTS = new Set(["extension.test.js"]);
    const unit_files = files.filter((f) => !INTEGRATION_TESTS.has(path.basename(f)));

    for (const f of unit_files) {
        mocha.addFile(path.resolve(suite_dir, f));
    }

    return new Promise<void>((resolve, reject) => {
        mocha.run((failures) => {
            if (failures > 0) {
                reject(new Error(`${failures} test(s) failed.`));
            } else {
                resolve();
            }
        });
    });
}

main().catch((err: unknown) => {
    console.error("Unit tests failed:", err);
    process.exit(1);
});
