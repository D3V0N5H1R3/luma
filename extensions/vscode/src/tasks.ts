import * as vscode from "vscode";
import { resolveInterpreterPath } from "./utils/util";
import { COMMANDS } from "./utils/constants";
import { mainAnnotationPattern, testAnnotationPattern } from "./generated/test-discovery";

// Test/main discovery patterns.
// Generated from extensions/shared/test-discovery-pattern.json.
// Tree-sitter equivalent: zed (runnables.scm).
const MAIN_ANNOTATION_PATTERN = mainAnnotationPattern();
const TEST_ANNOTATION_PATTERN = testAnnotationPattern();

interface LumaTaskConfig {
    args: (file: string) => string[];
    label: string;
    matcher: string;
    group?: vscode.TaskGroup;
}

const TASK_CONFIGS: Record<string, LumaTaskConfig> = {
    run: { args: (f) => [f], label: "Luma: Run File", matcher: "$luma" },
    test: {
        args: (f) => ["--test", f],
        label: "Luma: Run Tests",
        matcher: "$luma-test",
        group: vscode.TaskGroup.Test,
    },
    check: {
        args: (f) => ["--check", f],
        label: "Luma: Check File",
        matcher: "$luma",
        group: vscode.TaskGroup.Build,
    },
};

/**
 * Build a Luma task definition with the appropriate flags, matcher, and group.
 */
function createLumaTask(
    command: "run" | "test" | "check",
    file: string,
    label?: string,
): vscode.Task {
    const cfg = TASK_CONFIGS[command];
    const task_label = label ?? cfg.label;
    const args = cfg.args(file);

    const definition: vscode.TaskDefinition = {
        type: "luma",
        command,
        file,
    };

    const luma_bin = resolveInterpreterPath();
    const exec = new vscode.ProcessExecution(luma_bin, args);
    const task = new vscode.Task(
        definition,
        vscode.TaskScope.Workspace,
        task_label,
        "luma",
        exec,
        cfg.matcher,
    );
    if (cfg.group !== undefined) {
        task.group = cfg.group;
    }
    task.presentationOptions = {
        reveal: vscode.TaskRevealKind.Always,
        panel: vscode.TaskPanelKind.Shared,
    };
    return task;
}

/**
 * Registers a TaskProvider that supplies "Run" and "Test" tasks for Luma files.
 */
export function registerTaskProvider(context: vscode.ExtensionContext): void {
    const provider = new LumaTaskProvider();
    context.subscriptions.push(vscode.tasks.registerTaskProvider("luma", provider));
}

class LumaTaskProvider implements vscode.TaskProvider {
    private tasks: vscode.Task[] | undefined;

    async provideTasks(): Promise<vscode.Task[]> {
        this.tasks = await this.discoverTasks();
        return this.tasks;
    }

    resolveTask(task: vscode.Task): vscode.Task | undefined {
        const definition = task.definition;
        if (definition.type === "luma") {
            return createLumaTask(
                definition.command ?? "run",
                definition.file ?? "${file}",
                definition.label,
            );
        }
        return undefined;
    }

    private async discoverTasks(): Promise<vscode.Task[]> {
        const tasks: vscode.Task[] = [
            createLumaTask("run", "${file}"),
            createLumaTask("test", "${file}"),
            createLumaTask("check", "${file}"),
        ];

        // Auto-detect @main and @test files in the workspace.
        const luma_files = await vscode.workspace.findFiles("**/*.luma", "**/node_modules/**", 500);

        for (const file of luma_files) {
            try {
                const doc = await vscode.workspace.openTextDocument(file);
                const text = doc.getText();
                const relative = vscode.workspace.asRelativePath(file);

                if (MAIN_ANNOTATION_PATTERN.test(text)) {
                    tasks.push(createLumaTask("run", file.fsPath, `Run: ${relative}`));
                }

                if (TEST_ANNOTATION_PATTERN.test(text)) {
                    tasks.push(createLumaTask("test", file.fsPath, `Test: ${relative}`));
                }
            } catch {
                // Skip files that can't be opened.
            }
        }

        return tasks;
    }
}

/**
 * Creates and executes a Luma task for the active editor's file.
 */
function executeFileTask(command: "run" | "test"): void {
    if (!vscode.workspace.isTrusted) {
        vscode.window.showWarningMessage("Cannot run Luma files in an untrusted workspace.");
        return;
    }
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document?.languageId !== "luma") {
        vscode.window.showWarningMessage("No active Luma file.");
        return;
    }
    vscode.tasks.executeTask(createLumaTask(command, editor.document.uri.fsPath));
}

/**
 * Registers "Run Luma File" and "Run Luma Tests" commands.
 */
export function registerRunCommand(context: vscode.ExtensionContext): void {
    context.subscriptions.push(
        vscode.commands.registerCommand(COMMANDS.runFile, () => {
            executeFileTask("run");
        }),
        vscode.commands.registerCommand(COMMANDS.runTests, () => {
            executeFileTask("test");
        }),
    );
}
