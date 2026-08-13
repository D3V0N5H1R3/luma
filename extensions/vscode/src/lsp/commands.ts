import * as vscode from "vscode";
import { LSP_CONFIG, checkForBinaryUpdate } from "../utils/binary-download";
import { ClientManager } from "./client-manager";
import { COMMANDS } from "../utils/constants";

/**
 * Registers all user-facing commands provided by the Luma extension.
 */
export function registerCommands(
    context: vscode.ExtensionContext,
    output_channel: vscode.OutputChannel,
    manager: ClientManager,
): void {
    context.subscriptions.push(
        vscode.commands.registerCommand(COMMANDS.restartServer, async () => {
            output_channel.appendLine("Restarting all language server instances…");
            const restarts = manager.allClients().map((fc) => fc.client.restart());
            const results = await Promise.allSettled(restarts);
            const failures = results.filter((r) => r.status === "rejected");
            if (failures.length > 0) {
                output_channel.appendLine(
                    `Failed to restart ${failures.length} language server instance(s).`,
                );
            }
        }),

        vscode.commands.registerCommand(COMMANDS.showOutputChannel, () => {
            output_channel.show(true);
        }),

        vscode.commands.registerCommand(COMMANDS.updateServer, async () => {
            await checkForBinaryUpdate(
                LSP_CONFIG,
                context,
                output_channel,
                "interactive",
                COMMANDS.restartServer,
            );
        }),

        // NOTE: luma.showReferences is intentionally NOT registered here.
        // The language server advertises it via executeCommandProvider, so
        // vscode-languageclient's ExecuteCommandFeature registers it
        // automatically.  The middleware in client-manager.ts intercepts
        // the call and delegates to editor.action.showReferences.
    );
}
