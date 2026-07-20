import * as vscode from "vscode";
import { DAP_CONFIG, resolveBinaryCommand } from "../utils/binary-download";
import { CONFIG_KEYS } from "../utils/constants";

/**
 * Debug adapter descriptor factory.
 *
 * When VS Code needs a debug adapter for a "luma" debug session, this
 * factory resolves the binary path using the standard resolution order
 * (user config → bundled → auto-download → PATH) and spawns it with
 * stdio transport.
 */
class LumaDebugAdapterFactory implements vscode.DebugAdapterDescriptorFactory {
    private readonly context: vscode.ExtensionContext;
    private readonly output: vscode.OutputChannel;

    constructor(context: vscode.ExtensionContext, output: vscode.OutputChannel) {
        this.context = context;
        this.output = output;
    }

    async createDebugAdapterDescriptor(
        _session: vscode.DebugSession,
        _executable: vscode.DebugAdapterExecutable | undefined,
    ): Promise<vscode.DebugAdapterDescriptor> {
        const binary = await resolveBinaryCommand(
            DAP_CONFIG,
            CONFIG_KEYS.DAP_PATH,
            this.context,
            this.output,
        );
        return new vscode.DebugAdapterExecutable(binary);
    }
}

/**
 * Register the Luma debug adapter with VS Code.
 */
export function registerDebugAdapter(
    context: vscode.ExtensionContext,
    output: vscode.OutputChannel,
): void {
    const factory = new LumaDebugAdapterFactory(context, output);
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory("luma", factory));
}
