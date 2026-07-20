import { execFile } from "node:child_process";

import * as vscode from "vscode";

import { resolveInterpreterPath } from "../utils/util";
import { COMMANDS } from "../utils/constants";
import { luma_config } from "../utils/config";
import { generatePlaygroundHtml } from "./playground-html";

/**
 * Registers the Luma Playground webview panel, providing an interactive
 * scratch pad for evaluating small Luma code snippets directly in the editor.
 */
export function registerPlayground(context: vscode.ExtensionContext): void {
    context.subscriptions.push(
        vscode.commands.registerCommand(COMMANDS.openPlayground, () => {
            PlaygroundPanel.createOrShow(context);
        }),
    );
}

class PlaygroundPanel {
    private static panel: PlaygroundPanel | undefined;

    private readonly webview_panel: vscode.WebviewPanel;
    private readonly context: vscode.ExtensionContext;
    private _executing = false;

    static createOrShow(context: vscode.ExtensionContext): void {
        if (PlaygroundPanel.panel) {
            PlaygroundPanel.panel.webview_panel.reveal();
            return;
        }

        const panel = vscode.window.createWebviewPanel(
            "lumaPlayground",
            "Luma Playground",
            vscode.ViewColumn.Beside,
            {
                enableScripts: true,
                retainContextWhenHidden: true,
            },
        );

        PlaygroundPanel.panel = new PlaygroundPanel(panel, context);
    }

    private constructor(panel: vscode.WebviewPanel, context: vscode.ExtensionContext) {
        this.webview_panel = panel;
        this.context = context;

        panel.webview.html = generatePlaygroundHtml();

        panel.webview.onDidReceiveMessage(
            async (msg) => {
                if (msg.type === "run") {
                    const output = await this.runCode(msg.code);
                    panel.webview.postMessage({
                        type: "output",
                        text: output,
                    });
                }
            },
            undefined,
            context.subscriptions,
        );

        panel.onDidDispose(() => {
            PlaygroundPanel.panel = undefined;
        });
    }

    private async runCode(code: string): Promise<string> {
        if (this._executing) {
            return "Error: A previous execution is still running.";
        }

        this._executing = true;
        try {
            return await this.runCodeImpl(code);
        } finally {
            this._executing = false;
        }
    }

    private async runCodeImpl(code: string): Promise<string> {
        if (!vscode.workspace.isTrusted) {
            return "Error: Cannot run code in an untrusted workspace.";
        }

        const luma_bin = resolveInterpreterPath();
        const timeout = luma_config.playground_timeout;
        const max_buffer = luma_config.playground_max_output_size;

        return new Promise((resolve) => {
            const child = execFile(
                luma_bin,
                ["--eval"],
                { timeout, maxBuffer: max_buffer },
                (error, stdout, stderr) => {
                    if (error && !stdout && !stderr) {
                        const is_timeout = "killed" in error && error.killed;
                        const prefix = is_timeout ? "Timeout" : "Error";
                        resolve(`${prefix}: ${error.message}`);
                    } else {
                        resolve(stdout + stderr);
                    }
                },
            );

            if (!child.stdin) {
                child.kill();
                resolve("Error: failed to start process (stdin unavailable)");
                return;
            }
            child.stdin.write(code);
            child.stdin.end();
        });
    }
}
