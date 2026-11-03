import * as assert from "node:assert";

import { EDITOR_SHOW_REFERENCES, showReferences } from "../../lsp/show-references";

interface Call {
    readonly command: string;
    readonly args: unknown[];
}

function recorder(server_result?: unknown): {
    calls: Call[];
    requests: unknown[][];
    deps: Parameters<typeof showReferences>[1];
} {
    const calls: Call[] = [];
    const requests: unknown[][] = [];
    return {
        calls,
        requests,
        deps: {
            execute: (command, ...args) => {
                calls.push({ command, args });
                return Promise.resolve(undefined);
            },
            request: (uri, args) => {
                requests.push([uri, ...args]);
                return Promise.resolve(server_result);
            },
        },
    };
}

const URI = "file:///work/main.luma";
const POSITION = { line: 3, character: 7 };
const LOCATION = {
    uri: URI,
    range: { start: { line: 3, character: 7 }, end: { line: 3, character: 12 } },
};

suite("showReferences", () => {
    test("shows server-provided locations in the editor's references view", async () => {
        const { calls, requests, deps } = recorder([LOCATION]);

        await showReferences([URI, POSITION], deps);

        assert.deepStrictEqual(requests, [[URI, URI, POSITION]]);
        assert.strictEqual(calls.length, 1);
        assert.strictEqual(calls[0].command, EDITOR_SHOW_REFERENCES);
        const [uri, position, locations] = calls[0].args as [
            { toString(): string },
            { line: number; character: number },
            unknown[],
        ];
        assert.strictEqual(uri.toString(), URI);
        assert.deepStrictEqual({ line: position.line, character: position.character }, POSITION);
        assert.strictEqual(locations.length, 1);
    });

    test("uses caller-provided locations without asking the server", async () => {
        const { calls, requests, deps } = recorder();

        await showReferences([URI, POSITION, [LOCATION]], deps);

        assert.strictEqual(requests.length, 0);
        assert.strictEqual(calls.length, 1);
        // Regression: the handler must delegate to the editor's command, never
        // re-invoke luma.showReferences (which recursed forever).
        assert.strictEqual(calls[0].command, EDITOR_SHOW_REFERENCES);
    });

    test("ignores malformed arguments", async () => {
        const { calls, requests, deps } = recorder([LOCATION]);

        await showReferences([42, POSITION], deps);
        await showReferences([URI, { line: "3" }], deps);

        assert.strictEqual(requests.length, 0);
        assert.strictEqual(calls.length, 0);
    });

    test("shows nothing when the server returns no locations", async () => {
        const { calls, deps } = recorder(null);

        await showReferences([URI, POSITION], deps);

        assert.strictEqual(calls.length, 0);
    });
});
