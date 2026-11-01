// Pure helpers behind the Luma task provider (tasks.ts), kept free of
// vscode.Task APIs so they are unit-testable.

import { mainAnnotationPattern, testAnnotationPattern } from "./generated/test-discovery";

/** The interpreter invocations the `luma` task type supports. */
export type LumaTaskCommand = "run" | "test" | "check";

const LUMA_TASK_COMMANDS: ReadonlySet<string> = new Set<LumaTaskCommand>(["run", "test", "check"]);

/**
 * Returns true for a supported task `command`. A tasks.json entry is only
 * schema-checked, so an unknown value reaches the provider and must be
 * rejected rather than crash it.
 */
export function isLumaTaskCommand(value: unknown): value is LumaTaskCommand {
    return typeof value === "string" && LUMA_TASK_COMMANDS.has(value);
}

/** Which tasks a Luma source file supports. */
export interface DiscoveredAnnotations {
    /** The file has an `@main` function, so it can be run. */
    readonly runnable: boolean;
    /** The file has `@test` functions. */
    readonly has_tests: boolean;
}

/** Finds the `@main` and `@test` annotations in a Luma source file. */
export function discoverAnnotations(text: string): DiscoveredAnnotations {
    return {
        runnable: mainAnnotationPattern().test(text),
        has_tests: testAnnotationPattern().test(text),
    };
}
