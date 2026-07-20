import * as path from "node:path";
import { runTests } from "@vscode/test-electron";

async function main(): Promise<void> {
    const extension_development_path = path.resolve(__dirname, "../../");
    const extension_tests_path = path.resolve(__dirname, "./suite/index");

    await runTests({
        extensionDevelopmentPath: extension_development_path,
        extensionTestsPath: extension_tests_path,
        launchArgs: ["--disable-extensions"],
    });
}

main().catch((err: unknown) => {
    console.error("Failed to run tests:", err);
    process.exit(1);
});
