import * as assert from "node:assert";

import { escapeHtml } from "../../utils/html";

suite("escapeHtml", () => {
    test("escapes the ampersand first so entities are not double-encoded", () => {
        assert.strictEqual(escapeHtml("a & b"), "a &amp; b");
        assert.strictEqual(escapeHtml("&lt;"), "&amp;lt;");
    });

    test("escapes angle brackets to neutralise tags", () => {
        assert.strictEqual(escapeHtml("<b>"), "&lt;b&gt;");
        assert.strictEqual(
            escapeHtml("<script>alert(1)</script>"),
            "&lt;script&gt;alert(1)&lt;/script&gt;",
        );
    });

    test("escapes double quotes so attribute contexts stay safe", () => {
        assert.strictEqual(escapeHtml('say "hi"'), "say &quot;hi&quot;");
    });

    test("escapes all special characters in one pass", () => {
        assert.strictEqual(
            escapeHtml('<a href="x">A & B</a>'),
            "&lt;a href=&quot;x&quot;&gt;A &amp; B&lt;/a&gt;",
        );
    });

    test("leaves strings without special characters untouched", () => {
        assert.strictEqual(escapeHtml("plain text 123"), "plain text 123");
        assert.strictEqual(escapeHtml(""), "");
    });

    test("does not escape single quotes (not required for the double-quoted contexts used)", () => {
        assert.strictEqual(escapeHtml("it's"), "it's");
    });
});
