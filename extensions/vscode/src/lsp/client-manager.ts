import * as vscode from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    RevealOutputChannelOn,
    ServerOptions,
    State,
} from "vscode-languageclient/node";
import { LSP_CONFIG, resolveBinaryCommand } from "../utils/binary-download";
import { extractErrorMessage } from "../utils/util";
import { reportError } from "../utils/report";
import { ServerState, setStatus } from "./status";
import {
    CONFIG_SECTION,
    CONFIG_KEYS,
    COMMANDS,
    GLOBAL_FOLDER_KEY,
    IDS,
    folderLspClientId,
} from "../utils/constants";

// Re-exported so existing importers (extension.ts) keep a single entry point,
// while the vscode-only status helpers live in ./status.ts — free of the
// vscode-languageclient dependency and therefore unit-testable in isolation.
export { ServerState, setStatus, createLanguageStatus } from "./status";

/** A language client bound to a specific workspace folder. */
export interface FolderClient {
    readonly client: LanguageClient;
    readonly folder: vscode.WorkspaceFolder;
}

// ─── Shared middleware for language clients ──────────────────────

const luma_middleware: LanguageClientOptions["middleware"] = {
    executeCommand: async (cmd: string, args: unknown[], next) => {
        if (cmd === COMMANDS.showReferences && args.length >= 3) {
            const [uri, position, locations] = args as [unknown, unknown, unknown];
            await vscode.commands.executeCommand(COMMANDS.showReferences, uri, position, locations);
            return;
        }
        return next(cmd, args);
    },
};

// ─── LSP config defaults ─────────────────────────────────────────
// LSP config defaults (e.g. inlayHints.enabled) are defined per-editor:
//   VS Code:  package.json ("luma.inlayHints.enabled")
//   Zed:      src/lib.rs        (language_server_workspace_configuration)

/** Options for creating a language client. */
interface ClientCreationOptions {
    id: string;
    name: string;
    document_selector: LanguageClientOptions["documentSelector"];
    folder?: vscode.WorkspaceFolder;
    file_events: vscode.FileSystemWatcher;
}

/**
 * Manages language server clients for all workspace folders.
 *
 * Encapsulates the mutable client map, output channel, and language status
 * item. External code accesses clients through read-only methods; mutation
 * is limited to methods on this class.
 */
export class ClientManager {
    private readonly folder_clients = new Map<string, FolderClient>();
    private readonly output_channel: vscode.OutputChannel;
    private readonly language_status: vscode.LanguageStatusItem;

    constructor(output_channel: vscode.OutputChannel, language_status: vscode.LanguageStatusItem) {
        this.output_channel = output_channel;
        this.language_status = language_status;
    }

    // ─── Read-only accessors ──────────────────────────────────────

    /** Returns true if a client exists for the given URI key. */
    hasClient(key: string): boolean {
        return this.folder_clients.has(key);
    }

    /** Iterates over all active folder clients. */
    forEachClient(callback: (fc: FolderClient, key: string) => void): void {
        this.folder_clients.forEach((fc, key) => callback(fc, key));
    }

    /** Returns the number of active clients. */
    clientCount(): number {
        return this.folder_clients.size;
    }

    /** Returns all active clients as an array. */
    allClients(): FolderClient[] {
        return [...this.folder_clients.values()];
    }

    // ─── Client lifecycle ─────────────────────────────────────────

    /** Starts LSP clients for all workspace folders. */
    async startAll(context: vscode.ExtensionContext): Promise<void> {
        const folders = vscode.workspace.workspaceFolders;
        if (folders && folders.length > 1) {
            for (const folder of folders) {
                await this.startClientForFolder(context, folder);
            }
        } else {
            await this.startGlobalClient(context);
        }
    }

    /** Starts a per-folder language client. */
    async startClientForFolder(
        context: vscode.ExtensionContext,
        folder: vscode.WorkspaceFolder,
    ): Promise<void> {
        const file_pattern = new vscode.RelativePattern(folder, "**/*.luma");
        const { client, folder: resolved_folder } = await this.createAndStartClient(context, {
            id: folderLspClientId(folder.name),
            name: `Luma LSP (${folder.name})`,
            document_selector: [
                { scheme: "file", language: "luma", pattern: `${folder.uri.fsPath}/**` },
            ],
            folder,
            file_events: vscode.workspace.createFileSystemWatcher(file_pattern),
        });
        this.folder_clients.set(folder.uri.toString(), { client, folder: resolved_folder });
    }

    /** Starts a global language client (single-root or no-folder mode). */
    async startGlobalClient(context: vscode.ExtensionContext): Promise<void> {
        const { client, folder } = await this.createAndStartClient(context, {
            id: IDS.lspClient,
            name: "Luma Language Server",
            document_selector: [
                { scheme: "file", language: "luma" },
                { scheme: "untitled", language: "luma" },
            ],
            file_events: vscode.workspace.createFileSystemWatcher("**/*.luma"),
        });
        this.folder_clients.set(GLOBAL_FOLDER_KEY, { client, folder });
    }

    /** Stops all active language clients. */
    async stopAll(): Promise<void> {
        const stops: Promise<void>[] = [];
        for (const [, fc] of this.folder_clients) {
            stops.push(fc.client.stop());
        }
        await Promise.allSettled(stops);
        this.folder_clients.clear();
    }

    /** Stops and removes the client for the given key. */
    async stopClient(key: string): Promise<void> {
        const fc = this.folder_clients.get(key);
        if (fc) {
            await fc.client.stop();
            this.folder_clients.delete(key);
        }
    }

    // ─── Private helpers ──────────────────────────────────────────

    private async createAndStartClient(
        context: vscode.ExtensionContext,
        options: ClientCreationOptions,
    ): Promise<{ client: LanguageClient; folder: vscode.WorkspaceFolder }> {
        const command = await resolveBinaryCommand(
            LSP_CONFIG,
            CONFIG_KEYS.LSP_PATH,
            context,
            this.output_channel,
        );
        const server_options: ServerOptions = { command, args: [] };

        const client_options: LanguageClientOptions = {
            documentSelector: options.document_selector,
            workspaceFolder: options.folder,
            outputChannel: this.output_channel,
            revealOutputChannelOn: RevealOutputChannelOn.Never,
            middleware: luma_middleware,
            synchronize: {
                fileEvents: options.file_events,
            },
        };

        const client = new LanguageClient(options.id, options.name, server_options, client_options);

        this.wireClientState(client, context, command);

        const folder = options.folder ??
            vscode.workspace.workspaceFolders?.[0] ?? {
                uri: vscode.Uri.parse(""),
                name: "",
                index: 0,
            };

        return { client, folder };
    }

    private wireClientState(
        client: LanguageClient,
        context: vscode.ExtensionContext,
        command: string,
    ): void {
        client.onDidChangeState((e) => {
            switch (e.newState) {
                case State.Starting:
                    setStatus(this.language_status, ServerState.Starting);
                    break;
                case State.Running:
                    setStatus(this.language_status, ServerState.Running);
                    break;
                case State.Stopped:
                    setStatus(this.language_status, ServerState.Stopped);
                    break;
            }
        });

        context.subscriptions.push(client);

        client.start().catch(async (err: unknown) => {
            const message = extractErrorMessage(err);
            setStatus(this.language_status, ServerState.Error);

            const is_not_found = message.includes("ENOENT") || message.includes("not found");
            const help = is_not_found
                ? `Could not find "${command}". Install luma_lsp or set "luma.lsp.path" in settings.`
                : `Language server error: ${message}`;

            const action = await reportError(
                this.output_channel,
                `Failed to start language server: ${message}`,
                help,
                "Open Settings",
                "Show Output",
            );
            if (action === "Open Settings") {
                void vscode.commands.executeCommand(
                    "workbench.action.openSettings",
                    `${CONFIG_SECTION}.${CONFIG_KEYS.LSP_PATH}`,
                );
            } else if (action === "Show Output") {
                this.output_channel.show(true);
            }
        });
    }
}
