import * as assert from "node:assert";

import * as vscode from "vscode";

import { ServerState, setStatus } from "../../lsp/status";

function createStatusItem(): vscode.LanguageStatusItem {
    return {
        text: "",
        detail: undefined,
        busy: false,
        severity: vscode.LanguageStatusSeverity.Information,
    } as unknown as vscode.LanguageStatusItem;
}

suite("setStatus", () => {
    test("Starting shows a busy informational status", () => {
        const item = createStatusItem();
        setStatus(item, ServerState.Starting);
        assert.strictEqual(item.text, "Starting…");
        assert.strictEqual(item.severity, vscode.LanguageStatusSeverity.Information);
        assert.strictEqual(item.busy, true);
    });

    test("Running clears the busy flag", () => {
        const item = createStatusItem();
        item.busy = true;
        setStatus(item, ServerState.Running);
        assert.strictEqual(item.text, "Running");
        assert.strictEqual(item.severity, vscode.LanguageStatusSeverity.Information);
        assert.strictEqual(item.busy, false);
    });

    test("Stopped is a warning and not busy", () => {
        const item = createStatusItem();
        item.busy = true;
        setStatus(item, ServerState.Stopped);
        assert.strictEqual(item.text, "Stopped");
        assert.strictEqual(item.severity, vscode.LanguageStatusSeverity.Warning);
        assert.strictEqual(item.busy, false);
    });

    test("Error is an error severity with a detail hint", () => {
        const item = createStatusItem();
        item.busy = true;
        setStatus(item, ServerState.Error);
        assert.strictEqual(item.text, "Error");
        assert.strictEqual(item.severity, vscode.LanguageStatusSeverity.Error);
        assert.strictEqual(item.detail, "Click for details");
        assert.strictEqual(item.busy, false);
    });
});
