import * as vscode from "vscode";
import { COMMANDS } from "../utils/constants";

/** Represents the state of the language server. */
export const enum ServerState {
    Starting = "starting",
    Running = "running",
    Stopped = "stopped",
    Error = "error",
}

/** Updates a language status item to reflect the given server state. */
export function setStatus(item: vscode.LanguageStatusItem, state: ServerState): void {
    switch (state) {
        case ServerState.Starting:
            item.text = "Starting…";
            item.severity = vscode.LanguageStatusSeverity.Information;
            item.busy = true;
            break;
        case ServerState.Running:
            item.text = "Running";
            item.severity = vscode.LanguageStatusSeverity.Information;
            item.busy = false;
            break;
        case ServerState.Stopped:
            item.text = "Stopped";
            item.severity = vscode.LanguageStatusSeverity.Warning;
            item.busy = false;
            break;
        case ServerState.Error:
            item.text = "Error";
            item.severity = vscode.LanguageStatusSeverity.Error;
            item.detail = "Click for details";
            item.busy = false;
            break;
    }
}

/** Creates the language status bar item shown in the editor. */
export function createLanguageStatus(): vscode.LanguageStatusItem {
    const item = vscode.languages.createLanguageStatusItem("luma.serverStatus", {
        language: "luma",
    });
    item.name = "Luma Language Server";
    item.command = {
        title: "Show Output",
        command: COMMANDS.showOutputChannel,
    };
    setStatus(item, ServerState.Starting);
    return item;
}
