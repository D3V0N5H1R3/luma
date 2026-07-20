import * as assert from "node:assert";

import type * as vscode from "vscode";

import { FeatureRegistry } from "../../utils/feature-registry";

function createDisposable(id: string, log: string[]): vscode.Disposable {
    return {
        dispose: () => {
            log.push(id);
        },
    };
}

suite("FeatureRegistry", () => {
    test("disposes registered items in reverse (LIFO) order", () => {
        const log: string[] = [];
        const registry = new FeatureRegistry();
        registry.register(
            createDisposable("first", log),
            createDisposable("second", log),
            createDisposable("third", log),
        );

        registry.dispose();

        assert.deepStrictEqual(log, ["third", "second", "first"]);
    });

    test("supports incremental registration across multiple calls", () => {
        const log: string[] = [];
        const registry = new FeatureRegistry();
        registry.register(createDisposable("a", log));
        registry.register(createDisposable("b", log));

        registry.dispose();

        assert.deepStrictEqual(log, ["b", "a"]);
    });

    test("clears the internal list so a second dispose is a no-op", () => {
        const log: string[] = [];
        const registry = new FeatureRegistry();
        registry.register(createDisposable("only", log));

        registry.dispose();
        registry.dispose();

        assert.deepStrictEqual(log, ["only"]);
    });

    test("dispose on an empty registry does nothing", () => {
        const registry = new FeatureRegistry();
        assert.doesNotThrow(() => registry.dispose());
    });
});
