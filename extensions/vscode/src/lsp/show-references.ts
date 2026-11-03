import * as vscode from "vscode";
import { isLspPosition, toVscodeLocations } from "./types";

/** VS Code's built-in references peek view. */
export const EDITOR_SHOW_REFERENCES = "editor.action.showReferences";

/** Collaborators of {@link showReferences}, injected so it is unit-testable. */
export interface ShowReferencesDeps {
    /** Runs a VS Code command (`vscode.commands.executeCommand`). */
    readonly execute: (command: string, ...args: unknown[]) => Thenable<unknown>;
    /** Sends `luma.showReferences` to the language server that owns `uri`. */
    readonly request: (uri: string, args: unknown[]) => Promise<unknown>;
}

/**
 * Handles `luma.showReferences(uri, position[, locations])`.
 *
 * The language server answers the command with the identifier's locations;
 * callers that already hold them pass them as the third argument. Either way
 * the raw LSP values are converted to VS Code types and shown in the
 * references peek view. Malformed arguments are ignored.
 */
export async function showReferences(args: unknown[], deps: ShowReferencesDeps): Promise<void> {
    const [uri, position, provided] = args;
    if (typeof uri !== "string" || !isLspPosition(position)) {
        return;
    }

    const locations = Array.isArray(provided) ? provided : await deps.request(uri, [uri, position]);
    if (!Array.isArray(locations)) {
        return;
    }

    await deps.execute(
        EDITOR_SHOW_REFERENCES,
        vscode.Uri.parse(uri),
        new vscode.Position(position.line, position.character),
        toVscodeLocations(locations),
    );
}
