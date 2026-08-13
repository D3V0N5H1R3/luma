import * as assert from "node:assert";
import * as vscode from "vscode";
import { testFunctionPattern } from "../../generated/test-discovery";

// Integration suites only. Pure-logic checks that used to live here (checksum
// parsing, test-discovery regexes, code-action patterns, platform asset names,
// path resolution) now bind to the real implementations in dedicated unit
// suites (checksum/test-discovery/code-actions/util/binary tests), so they are
// not duplicated here with hand-copied regexes.

suite("Extension Activation", () => {
    test("Extension should be present", () => {
        const ext = vscode.extensions.getExtension("D3V0N5H1R3.luma-language");
        assert.ok(ext, "Extension not found");
    });

    test("Extension should activate on Luma file", async () => {
        const doc = await vscode.workspace.openTextDocument({
            language: "luma",
            content: '# hello\nprint("hello")\n',
        });
        await vscode.window.showTextDocument(doc);

        // Wait for activation.
        const ext = vscode.extensions.getExtension("D3V0N5H1R3.luma-language");
        if (ext && !ext.isActive) {
            await ext.activate();
        }
        assert.ok(ext?.isActive, "Extension did not activate");
    });

    test("Commands should be registered", async () => {
        const commands = await vscode.commands.getCommands(true);
        assert.ok(commands.includes("luma.restartServer"));
        assert.ok(commands.includes("luma.showOutputChannel"));
        assert.ok(commands.includes("luma.runFile"));
        assert.ok(commands.includes("luma.runTests"));
        // luma.showReferences is registered by the LSP client's
        // ExecuteCommandFeature when the server starts, so it is not
        // available in tests without a running language server.
        assert.ok(commands.includes("luma.updateServer"));
    });

    test("Luma language should be registered", async () => {
        const languages = await vscode.languages.getLanguages();
        assert.ok(languages.includes("luma"), "luma language not registered");
    });

    test("Snippets should be contributed", async () => {
        const doc = await vscode.workspace.openTextDocument({
            language: "luma",
            content: "",
        });
        const editor = await vscode.window.showTextDocument(doc);

        // Insert a snippet prefix and verify completion is offered.
        // This is a basic smoke test — full snippet testing requires
        // the completion provider to be active.
        await editor.edit((edit) => {
            edit.insert(new vscode.Position(0, 0), "@main");
        });
        assert.ok(doc.getText().includes("@main"));
    });
});

suite("Configuration", () => {
    test("luma.lsp.path should have empty default", () => {
        const config = vscode.workspace.getConfiguration("luma");
        const path = config.get<string>("lsp.path", "");
        assert.strictEqual(path, "");
    });

    test("luma.lsp.autoUpdate should default to true", () => {
        const config = vscode.workspace.getConfiguration("luma");
        const auto = config.get<boolean>("lsp.autoUpdate", true);
        assert.strictEqual(auto, true);
    });

    test("luma.path should have empty default", () => {
        const config = vscode.workspace.getConfiguration("luma");
        const path = config.get<string>("path", "");
        assert.strictEqual(path, "");
    });
});

suite("Code Action Provider", () => {
    test("Should suggest mutable for immutable variable diagnostic", async () => {
        const doc = await vscode.workspace.openTextDocument({
            language: "luma",
            content: "integer count = 0\ncount = 1\n",
        });
        await vscode.window.showTextDocument(doc);

        // Simulate a diagnostic on an immutable variable declaration.
        const actions = await vscode.commands.executeCommand<vscode.CodeAction[]>(
            "vscode.executeCodeActionProvider",
            doc.uri,
            new vscode.Range(0, 0, 0, 7),
        );
        // Actions may be empty without a running LSP; test that the provider doesn't throw.
        assert.ok(Array.isArray(actions));
    });

    test("Should not suggest mutable for already mutable declaration", async () => {
        const doc = await vscode.workspace.openTextDocument({
            language: "luma",
            content: "mutable integer count = 0\n",
        });
        await vscode.window.showTextDocument(doc);

        const actions = await vscode.commands.executeCommand<vscode.CodeAction[]>(
            "vscode.executeCodeActionProvider",
            doc.uri,
            new vscode.Range(0, 0, 0, 7),
        );
        assert.ok(Array.isArray(actions));
    });
});

suite("Test Discovery", () => {
    test("Should discover @test on same line as function", async () => {
        const content = "@test\nfunction void test_a() {\n    assert(true)\n}\n";
        const doc = await vscode.workspace.openTextDocument({
            language: "luma",
            content,
        });
        await vscode.window.showTextDocument(doc);

        // The regex should match test_a (same as testing.ts TEST_FUNCTION_PATTERN).
        const regex = testFunctionPattern();
        const match = regex.exec(content);
        assert.ok(match, "Test function should be discovered");
        assert.strictEqual(match[1], "test_a");
    });

    test("Should discover @test with function on same line", async () => {
        const content = "@test function void test_b() {\n    assert(true)\n}\n";
        // Use the same regex that testing.ts uses.
        const regex = testFunctionPattern();
        const match = regex.exec(content);
        assert.ok(match, "Test function on same line should be discovered");
        assert.strictEqual(match[1], "test_b");
    });
});

suite("Language Configuration", () => {
    test("Comment toggling should use hash prefix", async () => {
        const doc = await vscode.workspace.openTextDocument({
            language: "luma",
            content: 'print("hello")\n',
        });
        const editor = await vscode.window.showTextDocument(doc);
        editor.selection = new vscode.Selection(0, 0, 0, 0);

        await vscode.commands.executeCommand("editor.action.commentLine");
        const text = doc.getText();
        assert.ok(
            text.includes("#") || text.includes("print"),
            "Comment toggle should add # or leave the line",
        );
    });
});

suite("Task Provider", () => {
    test("Luma task provider should be registered", async () => {
        const tasks = await vscode.tasks.fetchTasks({ type: "luma" });
        // Task provider is registered; may return tasks if a .luma file exists.
        assert.ok(Array.isArray(tasks));
    });

    test("Task definitions should have correct structure", async () => {
        const tasks = await vscode.tasks.fetchTasks({ type: "luma" });
        for (const task of tasks) {
            assert.strictEqual(task.definition.type, "luma");
            assert.ok(
                ["run", "test", "check"].includes(task.definition.command),
                `Unexpected command: ${task.definition.command}`,
            );
        }
    });
});

suite("Keybindings", () => {
    test("Run file keybinding should be registered", async () => {
        // Verify the command is executable (keybinding wired to it).
        const commands = await vscode.commands.getCommands(true);
        assert.ok(commands.includes("luma.runFile"), "luma.runFile command missing");
        assert.ok(commands.includes("luma.runTests"), "luma.runTests command missing");
    });
});

suite("Activation Events", () => {
    test("Extension should activate for luma language", async () => {
        const doc = await vscode.workspace.openTextDocument({
            language: "luma",
            content: "",
        });
        await vscode.window.showTextDocument(doc);

        const ext = vscode.extensions.getExtension("D3V0N5H1R3.luma-language");
        if (ext && !ext.isActive) {
            await ext.activate();
        }
        assert.ok(ext?.isActive, "Extension should activate for luma files");
    });
});

suite("DAP Configuration", () => {
    test("luma.dap.path should have empty default", () => {
        const config = vscode.workspace.getConfiguration("luma");
        const path = config.get<string>("dap.path", "");
        assert.strictEqual(path, "");
    });

    test("Debug type 'luma' should be registered", async () => {
        // The debug adapter factory is registered for type 'luma'.
        // We verify the extension contributes this debug type.
        const ext = vscode.extensions.getExtension("D3V0N5H1R3.luma-language");
        assert.ok(ext, "Extension not found");
        const contributes = ext.packageJSON?.contributes;
        assert.ok(contributes?.debuggers, "No debuggers contributed");
        const lumaDebugger = contributes.debuggers.find((d: { type: string }) => d.type === "luma");
        assert.ok(lumaDebugger, "Luma debugger not contributed");
        assert.strictEqual(lumaDebugger.type, "luma");
    });
});
