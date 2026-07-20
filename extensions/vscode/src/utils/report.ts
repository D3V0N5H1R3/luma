// Severity-aware reporting seam.
//
// VS Code is the only Luma editor with both an output channel and a separate
// notification surface, so every reporting site used to hand-pair an
// `output.appendLine(...)` with a `vscode.window.show*Message(...)` and pick a
// severity ad hoc. These helpers centralise the contract in
// extensions/shared/error-handling.md — append to the channel (when one is
// available), then surface at the mapped severity — so the choices no longer
// drift. They mirror Zed's `StatusReporter`,
// giving every editor a single named seam for the same decision.
//
// Each helper appends the terse `log_message` to the channel and shows the
// actionable `notification` (error-handling.md rule 4). Optional action buttons
// are forwarded to the underlying VS Code API, and the chosen action (or
// `undefined`) is returned so callers offering buttons can await the result.

import * as vscode from "vscode";

/**
 * Modal error (contract level 1): only for problems that block the user's
 * intended action and need acknowledgment (e.g. checksum mismatch, LSP failed
 * to start).
 *
 * @param output - Channel to log to, or `undefined` when none is in scope.
 * @param log_message - Terse diagnostic line written to the output channel.
 * @param notification - Actionable message shown to the user.
 * @param actions - Optional action-button labels.
 * @returns The chosen action label, or `undefined` if dismissed.
 */
export function reportError(
    output: vscode.OutputChannel | undefined,
    log_message: string,
    notification: string,
    ...actions: string[]
): Thenable<string | undefined> {
    output?.appendLine(log_message);
    return vscode.window.showErrorMessage(notification, ...actions);
}

/**
 * Notification warning (contract level 2): a recoverable problem the user
 * should know about but that does not block their workflow (e.g. a download
 * failed but the binary can be installed manually).
 *
 * @param output - Channel to log to, or `undefined` when none is in scope.
 * @param log_message - Terse diagnostic line written to the output channel.
 * @param notification - Actionable message shown to the user.
 * @param actions - Optional action-button labels.
 * @returns The chosen action label, or `undefined` if dismissed.
 */
export function reportWarning(
    output: vscode.OutputChannel | undefined,
    log_message: string,
    notification: string,
    ...actions: string[]
): Thenable<string | undefined> {
    output?.appendLine(log_message);
    return vscode.window.showWarningMessage(notification, ...actions);
}

/**
 * Informational notification (contract level 2, info variant): confirms a
 * user-initiated action whose routine success would otherwise be log-only
 * (error-handling.md rule 2), e.g. a manual update that completed.
 *
 * @param output - Channel to log to, or `undefined` when none is in scope.
 * @param log_message - Line written to the output channel.
 * @param notification - Message shown to the user.
 * @param actions - Optional action-button labels.
 * @returns The chosen action label, or `undefined` if dismissed.
 */
export function reportInfo(
    output: vscode.OutputChannel | undefined,
    log_message: string,
    notification: string,
    ...actions: string[]
): Thenable<string | undefined> {
    output?.appendLine(log_message);
    return vscode.window.showInformationMessage(notification, ...actions);
}
