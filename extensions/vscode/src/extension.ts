import * as vscode from "vscode";
import { extractErrorMessage } from "./utils/util";
import { CONFIG_SECTION, CONFIG_KEYS, COMMANDS, GLOBAL_FOLDER_KEY } from "./utils/constants";
import {
    LSP_CONFIG,
    DAP_CONFIG,
    checkForBinaryUpdate,
    type BinaryConfig,
} from "./utils/binary-download";
import { luma_config } from "./utils/config";
import { registerTaskProvider, registerRunCommand } from "./tasks";
import { registerTestController } from "./testing/testing";
import { registerDebugAdapter } from "./debugger/debug";
import { registerDebugVisualizer } from "./debugger/visualizer";
import { registerPlayground } from "./playground/playground";
import { registerCommands } from "./lsp/commands";
import { MutableKeywordFixer, IncludePathFixer } from "./lsp/code-actions";
import { FeatureRegistry } from "./utils/feature-registry";
import { ClientManager, createLanguageStatus, setStatus, ServerState } from "./lsp/client-manager";

// ─── Module-level client manager (needed by deactivate) ──────────

let active_client_manager: ClientManager | undefined;

// ─── Activation ───────────────────────────────────────────────────

/** Activates the Luma extension, starting LSP clients and registering commands. */
export async function activate(context: vscode.ExtensionContext): Promise<void> {
    const registry = new FeatureRegistry();
    context.subscriptions.push(registry);

    // vscode-languageclient 10 requires the client's `outputChannel` to be a
    // `LogOutputChannel` (it logs through the channel's level-aware methods), so
    // create a log channel with `{ log: true }`.
    const output_channel = vscode.window.createOutputChannel("Luma Language Server", {
        log: true,
    });
    registry.register(output_channel);

    const language_status = createLanguageStatus();
    registry.register(language_status);

    const client_manager = new ClientManager(output_channel, language_status);
    active_client_manager = client_manager;

    // Register features independently so one failure doesn't prevent others.
    const features: Array<[string, () => void]> = [
        ["commands", () => registerCommands(context, output_channel, client_manager)],
        ["task provider", () => registerTaskProvider(context)],
        ["run command", () => registerRunCommand(context)],
        ["test controller", () => registerTestController(context)],
        ["debug adapter", () => registerDebugAdapter(context, output_channel)],
        ["debug visualizer", () => registerDebugVisualizer(context)],
    ];

    for (const [name, register] of features) {
        try {
            register();
        } catch (err) {
            console.error(`[luma] Failed to register ${name}:`, err);
        }
    }

    const playground_enabled = luma_config.playground_enabled;
    if (playground_enabled) {
        try {
            registerPlayground(context);
        } catch (err) {
            console.error("[luma] Failed to register playground:", err);
        }
    }
    registry.register(createConfigurationWatcher());
    registry.register(createWorkspaceFolderWatcher(context, client_manager));
    registry.register(createCodeActionProvider());

    if (!vscode.workspace.isTrusted) {
        activateRestrictedMode(context, registry, client_manager, language_status, output_channel);
        return;
    }

    await client_manager.startAll(context);
    safeAutoUpdate(context, output_channel);
}

// ─── Configuration Change Handling ────────────────────────────────

function createConfigurationWatcher(): vscode.Disposable {
    return vscode.workspace.onDidChangeConfiguration(async (e) => {
        if (e.affectsConfiguration(`${CONFIG_SECTION}.${CONFIG_KEYS.LSP_PATH}`)) {
            const action = await vscode.window.showInformationMessage(
                "Luma LSP path changed. Restart the language server?",
                "Restart",
                "Later",
            );
            if (action === "Restart") {
                await vscode.commands.executeCommand(COMMANDS.restartServer);
            }
        }
    });
}

// ─── Workspace Folder Tracking ────────────────────────────────────

function createWorkspaceFolderWatcher(
    context: vscode.ExtensionContext,
    manager: ClientManager,
): vscode.Disposable {
    return vscode.workspace.onDidChangeWorkspaceFolders(async (e) => {
        await removeStaleClients(manager, e.removed);
        await reconcileClientsAfterAdd(context, manager, e);
        await collapseToGlobalIfSingleRoot(context, manager);
    });
}

async function removeStaleClients(
    manager: ClientManager,
    removed: readonly vscode.WorkspaceFolder[],
): Promise<void> {
    for (const folder of removed) {
        const key = folder.uri.toString();
        if (manager.hasClient(key)) {
            try {
                await manager.stopClient(key);
            } catch (err) {
                console.error(`[luma] Failed to stop client for ${folder.name}:`, err);
            }
        }
    }
}

async function reconcileClientsAfterAdd(
    context: vscode.ExtensionContext,
    manager: ClientManager,
    e: vscode.WorkspaceFoldersChangeEvent,
): Promise<void> {
    if (manager.hasClient(GLOBAL_FOLDER_KEY) && e.added.length > 0) {
        await manager.stopClient(GLOBAL_FOLDER_KEY);
        const folders = vscode.workspace.workspaceFolders ?? [];
        for (const folder of folders) {
            if (!manager.hasClient(folder.uri.toString())) {
                await manager.startClientForFolder(context, folder);
            }
        }
    } else {
        for (const added of e.added) {
            await manager.startClientForFolder(context, added);
        }
    }
}

async function collapseToGlobalIfSingleRoot(
    context: vscode.ExtensionContext,
    manager: ClientManager,
): Promise<void> {
    if (
        !manager.hasClient(GLOBAL_FOLDER_KEY) &&
        manager.clientCount() === 1 &&
        (vscode.workspace.workspaceFolders?.length ?? 0) <= 1
    ) {
        let remaining_key: string | undefined;
        manager.forEachClient((_fc, key) => {
            remaining_key = key;
        });
        if (remaining_key) {
            await manager.stopClient(remaining_key);
        }
        await manager.startGlobalClient(context);
    }
}

// ─── Code Actions ─────────────────────────────────────────────────

function createCodeActionProvider(): vscode.Disposable {
    const selector: vscode.DocumentFilter = { language: "luma", scheme: "file" };
    const options = { providedCodeActionKinds: [vscode.CodeActionKind.QuickFix] };
    return vscode.Disposable.from(
        vscode.languages.registerCodeActionsProvider(selector, new MutableKeywordFixer(), options),
        vscode.languages.registerCodeActionsProvider(selector, new IncludePathFixer(), options),
    );
}

// ─── Workspace Trust Gate ─────────────────────────────────────────

function activateRestrictedMode(
    context: vscode.ExtensionContext,
    registry: FeatureRegistry,
    manager: ClientManager,
    language_status: vscode.LanguageStatusItem,
    output_channel: vscode.OutputChannel,
): void {
    setStatus(language_status, ServerState.Stopped);
    language_status.detail = "Restricted mode — LSP disabled";
    registry.register(
        vscode.workspace.onDidGrantWorkspaceTrust(async () => {
            await manager.startAll(context);
            safeAutoUpdate(context, output_channel);
        }),
    );
}

// ─── Auto-Update ──────────────────────────────────────────────────

// Auto-update is gated by a single setting (luma.lsp.autoUpdate) and applies to
// every managed binary that is already installed. The language server offers a
// restart after updating; the debug adapter has no long-lived process to
// restart, so it updates silently. Each check catches its own errors so one
// failure never blocks the other or extension activation.
function safeAutoUpdate(
    context: vscode.ExtensionContext,
    output_channel: vscode.OutputChannel,
): void {
    if (!luma_config.lsp_auto_update) {
        return;
    }

    const targets: Array<[BinaryConfig, string | undefined]> = [
        [LSP_CONFIG, COMMANDS.restartServer],
        [DAP_CONFIG, undefined],
    ];

    void (async () => {
        for (const [config, restart_command] of targets) {
            try {
                await checkForBinaryUpdate(
                    config,
                    context,
                    output_channel,
                    "silent",
                    restart_command,
                );
            } catch (err: unknown) {
                output_channel.appendLine(
                    `Auto-update check failed for ${config.name}: ${extractErrorMessage(err)}`,
                );
            }
        }
    })().catch((err: unknown) => {
        console.error("[luma] Auto-update failed:", err);
    });
}

// ─── Deactivation ─────────────────────────────────────────────────

/** Deactivates the extension and stops all running LSP clients. */
export async function deactivate(): Promise<void> {
    await active_client_manager?.stopAll();
    active_client_manager = undefined;
}
