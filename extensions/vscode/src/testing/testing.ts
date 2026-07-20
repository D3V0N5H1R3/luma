import * as vscode from "vscode";
import { extractErrorMessage, resolveInterpreterPath } from "../utils/util";
import { IDS } from "../utils/constants";
import { testFunctionPattern } from "../generated/test-discovery";
import {
    collectItems,
    extractErrorOutput,
    groupLeafTestItemsByFile,
    runInterpreterForFile,
} from "./utils";

/**
 * Registers a TestController that discovers @test functions in Luma files
 * and runs them via `luma --test <file>`.
 */
export function registerTestController(context: vscode.ExtensionContext): void {
    const controller = vscode.tests.createTestController(IDS.testController, "Luma Tests");
    context.subscriptions.push(controller);

    // Watch for .luma files.
    const watcher = vscode.workspace.createFileSystemWatcher("**/*.luma");
    context.subscriptions.push(watcher);

    // Discovery runs fire-and-forget from watcher events, so guard against a
    // rejected promise (e.g. the file is deleted between the event and the
    // openTextDocument read) to avoid an unhandled rejection.
    const discover = (uri: vscode.Uri): void => {
        void updateTestsForFile(controller, uri).catch((err: unknown) => {
            console.debug(
                `Failed to discover tests in ${uri.toString()}: ${extractErrorMessage(err)}`,
            );
        });
    };
    watcher.onDidCreate(discover);
    watcher.onDidChange(discover);
    watcher.onDidDelete((uri) => {
        controller.items.delete(uri.toString());
        test_discovery_cache.delete(uri.toString());
    });

    // Initial scan.
    controller.resolveHandler = async () => {
        const files = await vscode.workspace.findFiles("**/*.luma");
        for (const file of files) {
            await updateTestsForFile(controller, file);
        }
    };

    // Run handler.
    const run_profile = controller.createRunProfile(
        "Run",
        vscode.TestRunProfileKind.Run,
        (request, token) => runTests(controller, request, token),
    );
    context.subscriptions.push(run_profile);
}

// Test discovery pattern: Lines matching @test followed by a function declaration.
// Generated from extensions/shared/test-discovery-pattern.json.
// Tree-sitter equivalent: zed (runnables.scm).
const TEST_FUNCTION_PATTERN = testFunctionPattern();

/** File mtimes keyed by file URI, used to skip re-parsing unchanged files. */
const test_discovery_cache = new Map<string, number>();

async function updateTestsForFile(
    controller: vscode.TestController,
    uri: vscode.Uri,
): Promise<void> {
    const key = uri.toString();

    // Check file mtime to skip re-parsing unchanged files.
    let mtime: number;
    try {
        mtime = (await vscode.workspace.fs.stat(uri)).mtime;
    } catch {
        // File may have been deleted between the event and processing.
        console.debug(`Failed to stat ${key} during test discovery (file may have been deleted)`);
        return;
    }
    if (test_discovery_cache.get(key) === mtime) {
        return;
    }

    const doc = await vscode.workspace.openTextDocument(uri);
    const text = doc.getText();
    const file_item = controller.createTestItem(key, vscode.workspace.asRelativePath(uri), uri);

    let match: RegExpExecArray | null;
    TEST_FUNCTION_PATTERN.lastIndex = 0;
    let found = false;
    while ((match = TEST_FUNCTION_PATTERN.exec(text)) !== null) {
        found = true;
        const name = match[1];
        const line = doc.positionAt(match.index).line;
        const test_item = controller.createTestItem(`${key}::${name}`, name, uri);
        test_item.range = new vscode.Range(line, 0, line, 0);
        file_item.children.add(test_item);
    }

    if (found) {
        controller.items.add(file_item);
    } else {
        controller.items.delete(key);
    }

    // Record the mtime so unchanged files are skipped on the next scan.
    test_discovery_cache.set(key, mtime);
}

async function runTests(
    controller: vscode.TestController,
    request: vscode.TestRunRequest,
    token: vscode.CancellationToken,
): Promise<void> {
    const run = controller.createTestRun(request);
    const items_to_run = collectItems(controller, request);
    const file_map = groupTestsByFile(items_to_run, run);

    const luma_bin = resolveInterpreterPath();

    for (const [file_key, tests] of file_map) {
        if (token.isCancellationRequested) {
            for (const t of tests) {
                run.skipped(t);
            }
            continue;
        }

        const file_path = vscode.Uri.parse(file_key).fsPath;
        const start_time = Date.now();

        try {
            const result = await runInterpreterForFile(luma_bin, ["--test", file_path], token);
            const output = result.stdout + result.stderr;
            const elapsed = Date.now() - start_time;
            reportTestResults(tests, output, elapsed, run);
        } catch (err: unknown) {
            // Cancellation kills the child, rejecting the promise. Treat the
            // in-flight file's tests as skipped — consistent with the loop-top
            // handling of the remaining files — rather than reporting spurious
            // failures. `continue` (not `break`) lets that loop-top mark the
            // rest of the files as skipped too.
            if (token.isCancellationRequested) {
                for (const t of tests) {
                    run.skipped(t);
                }
                continue;
            }
            const elapsed = Date.now() - start_time;
            const output = extractErrorOutput(err);
            for (const t of tests) {
                run.failed(t, new vscode.TestMessage(output), elapsed);
            }
        }
    }

    run.end();
}

function groupTestsByFile(
    items: vscode.TestItem[],
    run: vscode.TestRun,
): Map<string, vscode.TestItem[]> {
    const file_map = groupLeafTestItemsByFile(items);
    for (const tests of file_map.values()) {
        for (const t of tests) {
            run.started(t);
        }
    }
    return file_map;
}

/**
 * Parses interpreter test output into a map of failed test label → message.
 *
 * Scans the output once, matching each `  FAIL  <label>: <message>` line — the
 * format `luma --test` prints (see the VM test reporter). Colours are stripped
 * because the extension captures the interpreter's output through a pipe, so the
 * line carries no ANSI escapes. When a failure line carries no message, the full
 * output is stored instead, mirroring the previous per-test fallback. Earlier
 * failures win for duplicate labels.
 *
 * Doing this once per file — rather than compiling and scanning a fresh RegExp
 * per test — turns result reporting from O(tests · output) into O(output +
 * tests) and drops the per-test regex compilations.
 */
export function parseFailedTestMessages(output: string): Map<string, string> {
    const failures = new Map<string, string>();
    const fail_pattern = /^[ \t]*FAIL[ \t]+(\w+):[ \t]*(.*)$/gm;
    let match: RegExpExecArray | null;
    while ((match = fail_pattern.exec(output)) !== null) {
        const label = match[1];
        if (!failures.has(label)) {
            const message = match[2];
            failures.set(label, message.length > 0 ? message : output);
        }
    }
    return failures;
}

function reportTestResults(
    tests: vscode.TestItem[],
    output: string,
    elapsed: number,
    run: vscode.TestRun,
): void {
    const failures = parseFailedTestMessages(output);
    for (const t of tests) {
        const message = failures.get(t.label);
        if (message !== undefined) {
            run.failed(t, new vscode.TestMessage(message), elapsed);
        } else {
            run.passed(t, elapsed);
        }
    }
}
