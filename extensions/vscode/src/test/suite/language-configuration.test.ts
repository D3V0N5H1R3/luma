import * as assert from "node:assert";
import * as fs from "node:fs";
import * as path from "node:path";

// Compiled tests run from out/test/suite; the sources sit three levels up.
const CONFIG_PATH = path.resolve(__dirname, "..", "..", "..", "language-configuration.json");

interface OnEnterRule {
    beforeText: string;
    afterText?: string;
    action: { indent: string; appendText?: string };
}

function onEnterRules(): OnEnterRule[] {
    const config = JSON.parse(fs.readFileSync(CONFIG_PATH, "utf-8")) as {
        onEnterRules: OnEnterRule[];
    };
    return config.onEnterRules;
}

suite("language-configuration.json", () => {
    test("on-Enter rules never add indentation on top of their indent action", () => {
        // VS Code already inserts one indent level for `indent` and
        // `indentOutdent`; whitespace in appendText indented the new line a
        // second time (and as hard-coded spaces, whatever the user's tab size).
        for (const rule of onEnterRules()) {
            const append = rule.action.appendText;
            assert.ok(
                append === undefined || append.trim() !== "",
                `rule ${rule.beforeText} appends whitespace to an "${rule.action.indent}" action`,
            );
        }
    });

    test("every opening bracket indents the next line", () => {
        const rules = onEnterRules();
        for (const sample of ["if ready {", "array<integer> xs = [", "print("]) {
            assert.ok(
                rules.some(
                    (rule) =>
                        rule.action.indent === "indent" && new RegExp(rule.beforeText).test(sample),
                ),
                `no indent rule applies to "${sample}"`,
            );
        }
    });
});
