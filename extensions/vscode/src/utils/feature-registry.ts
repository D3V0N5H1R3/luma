import * as vscode from "vscode";

/**
 * Centralised owner of feature disposables for the Luma extension.
 *
 * Push the registry itself into `context.subscriptions` once; every
 * feature registered through it is disposed automatically when the
 * extension deactivates.
 */
export class FeatureRegistry implements vscode.Disposable {
    private readonly disposables: vscode.Disposable[] = [];

    /** Add one or more disposables to the registry. */
    register(...items: vscode.Disposable[]): void {
        this.disposables.push(...items);
    }

    /** Dispose every registered item in reverse-registration order. */
    dispose(): void {
        for (let i = this.disposables.length - 1; i >= 0; i--) {
            this.disposables[i].dispose();
        }
        this.disposables.length = 0;
    }
}
