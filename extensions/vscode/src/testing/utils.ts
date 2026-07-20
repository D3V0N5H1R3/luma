import { execFile } from "node:child_process";
import type * as vscode from "vscode";

/**
 * Iterates each leaf TestItem in the provided collection.
 *
 * For every top-level item, if it has children the callback is invoked on
 * each child; otherwise the callback is invoked on the item itself.
 */
export function forEachLeafTestItem(
    items: readonly vscode.TestItem[] | Iterable<vscode.TestItem>,
    callback: (item: vscode.TestItem) => void,
): void {
    for (const item of items) {
        if (item.children.size > 0) {
            item.children.forEach(callback);
        } else {
            callback(item);
        }
    }
}

/**
 * Groups the leaf TestItems of `items` by their file URI.
 *
 * Makes a single pass over the leaves (via {@link forEachLeafTestItem}),
 * building a `file URI → leaves` map so callers can index by file in O(1)
 * instead of re-filtering the whole item list per file.
 */
export function groupLeafTestItemsByFile(
    items: readonly vscode.TestItem[] | Iterable<vscode.TestItem>,
): Map<string, vscode.TestItem[]> {
    const file_map = new Map<string, vscode.TestItem[]>();

    forEachLeafTestItem(items, (leaf) => {
        const key = leaf.uri?.toString() ?? "";
        let tests = file_map.get(key);
        if (!tests) {
            tests = [];
            file_map.set(key, tests);
        }
        tests.push(leaf);
    });

    return file_map;
}

/** Captured output of running the interpreter over a single file. */
export interface InterpreterRunResult {
    stdout: string;
    stderr: string;
}

/** Error from child-process execution, carrying the captured stdout/stderr. */
interface ExecError extends Error {
    stdout: string;
    stderr: string;
}

/**
 * Runs `luma <...args>` as a child process, resolving with its captured
 * stdout/stderr on success and rejecting with an error that carries the same
 * captured output on failure. Cancelling `token` kills the child process.
 */
export function runInterpreterForFile(
    luma_bin: string,
    args: readonly string[],
    token: vscode.CancellationToken,
): Promise<InterpreterRunResult> {
    return new Promise((resolve, reject) => {
        const child = execFile(luma_bin, args, (error, stdout, stderr) => {
            if (error) {
                reject(Object.assign(new Error(error.message), { stdout, stderr }) as ExecError);
            } else {
                resolve({ stdout, stderr });
            }
        });
        token.onCancellationRequested(() => {
            child.kill();
        });
    });
}

/** Collects test items from a run request, or all controller items if none. */
export function collectItems(
    controller: vscode.TestController,
    request: vscode.TestRunRequest,
): vscode.TestItem[] {
    const items: vscode.TestItem[] = [];
    if (request.include) {
        items.push(...request.include);
    } else {
        controller.items.forEach((item) => items.push(item));
    }
    return items;
}

/** Extracts combined stdout/stderr/message text from a child-process error. */
export function extractErrorOutput(err: unknown): string {
    if (typeof err === "object" && err !== null) {
        const obj = err as Record<string, unknown>;
        const stdout = typeof obj.stdout === "string" ? obj.stdout : "";
        const stderr = typeof obj.stderr === "string" ? obj.stderr : "";
        const message = typeof obj.message === "string" ? obj.message : "";
        return stdout + stderr + message;
    }
    return String(err);
}
