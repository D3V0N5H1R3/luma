import * as vscode from "vscode";
import { DAP_CONFIG, resolveBinaryCommand } from "../utils/binary-download";
import { CONFIG_KEYS } from "../utils/constants";

/** Default launch configuration used when no `launch.json` exists. */
const DEFAULT_DEBUG_CONFIG: vscode.DebugConfiguration = {
    type: "luma",
    request: "launch",
    name: "Debug Current File",
    program: "${file}",
    stopOnEntry: false,
};

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
 * Debug configuration provider.
 *
 * Supplies a default launch configuration when the user presses F5 with a
 * `.luma` file open and no `launch.json` exists, enabling one-click debugging.
 * Also populates the configuration dropdown via the Dynamic trigger kind so
 * "Debug Current File" appears without requiring a `launch.json`.
 */
class LumaDebugConfigurationProvider implements vscode.DebugConfigurationProvider {
    resolveDebugConfiguration(
        _folder: vscode.WorkspaceFolder | undefined,
        config: vscode.DebugConfiguration,
        _token?: vscode.CancellationToken,
    ): vscode.ProviderResult<vscode.DebugConfiguration> {
        // When launch.json is missing or empty, VS Code passes an almost-empty
        // config with only `type` (and sometimes `request`).  Fill in the
        // defaults so the session can start immediately.
        if (!config.type && !config.request && !config.name) {
            const editor = vscode.window.activeTextEditor;
            if (editor && editor.document.languageId === "luma") {
                Object.assign(config, DEFAULT_DEBUG_CONFIG);
            }
        }

        if (!config.program) {
            return vscode.window
                .showInformationMessage("Cannot debug: open a .luma file first.")
                .then(() => undefined);
        }

        return config;
    }

    provideDebugConfigurations(
        _folder: vscode.WorkspaceFolder | undefined,
        _token?: vscode.CancellationToken,
    ): vscode.ProviderResult<vscode.DebugConfiguration[]> {
        return [{ ...DEFAULT_DEBUG_CONFIG }];
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

    const provider = new LumaDebugConfigurationProvider();
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider("luma", provider),
    );

    // Register the same provider with the Dynamic trigger kind so the
    // configuration dropdown in the Run and Debug sidebar is populated
    // without requiring a launch.json.
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider(
            "luma",
            provider,
            vscode.DebugConfigurationProviderTriggerKind.Dynamic,
        ),
    );
}
