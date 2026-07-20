import * as vscode from "vscode";
import { COMMANDS } from "../utils/constants";
import { extractErrorMessage } from "../utils/util";
import { renderValue, renderError, renderEmptyHint } from "./visualizer-renderers";

/**
 * Registers the Luma Debug Visualizer panel, which shows structured
 * runtime values (arrays, records, trees) in a graphical view during
 * debugging.
 */
export function registerDebugVisualizer(context: vscode.ExtensionContext): void {
    const provider = new LumaDebugVisualizerProvider(context);

    context.subscriptions.push(
        vscode.window.registerWebviewViewProvider("luma.debugVisualizer", provider),
        vscode.commands.registerCommand(COMMANDS.visualizeVariable, async () => {
            const session = vscode.debug.activeDebugSession;
            if (!session) {
                void vscode.window.showInformationMessage("No active debug session.");
                return;
            }

            const input = await vscode.window.showInputBox({
                prompt: "Expression to visualize",
                placeHolder: "e.g., my_array, my_record",
            });

            if (input) {
                await provider.evaluateAndDisplay(session, input);
            }
        }),
        // Reset the panel to its empty state whenever the active debug session
        // changes, since a value visualized for one session no longer applies.
        vscode.debug.onDidChangeActiveDebugSession(() => {
            provider.refresh();
        }),
    );
}

class LumaDebugVisualizerProvider implements vscode.WebviewViewProvider {
    private view?: vscode.WebviewView;

    constructor(private readonly context: vscode.ExtensionContext) {}

    resolveWebviewView(
        webviewView: vscode.WebviewView,
        _context: vscode.WebviewViewResolveContext,
        _token: vscode.CancellationToken,
    ): void {
        this.view = webviewView;
        webviewView.webview.options = {
            // The visualizer renders static, fully-escaped HTML guarded by a strict
            // CSP (default-src 'none'); it never executes scripts or loads local
            // resources, so both capabilities stay disabled (defence in depth).
            enableScripts: false,
            localResourceRoots: [],
        };
        webviewView.onDidDispose(() => {
            this.view = undefined;
        });
        this.setEmptyContent();
    }

    /** Resets the panel to its empty state (e.g. when the debug session changes). */
    refresh(): void {
        if (this.view) {
            this.setEmptyContent();
        }
    }

    async evaluateAndDisplay(session: vscode.DebugSession, expression: string): Promise<void> {
        try {
            const response = await session.customRequest("evaluate", {
                expression,
                context: "hover",
            });

            const html = renderValue(expression, response);

            if (this.view) {
                this.view.webview.html = html;
            }
        } catch (err: unknown) {
            const msg = extractErrorMessage(err);
            if (this.view) {
                this.view.webview.html = renderError(expression, msg);
            }
        }
    }

    private setEmptyContent(): void {
        if (this.view) {
            this.view.webview.html = renderEmptyHint();
        }
    }
}
