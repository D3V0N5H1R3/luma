import * as assert from "node:assert";

import { withRetry, fetchResponse } from "../../utils/http";

suite("withRetry", () => {
    test("should return value on first success", async () => {
        const result = await withRetry(async () => 42, { maxRetries: 3, baseDelay: 1 });
        assert.strictEqual(result, 42);
    });

    test("should retry on failure and succeed", async () => {
        let attempts = 0;
        const result = await withRetry(
            async () => {
                attempts++;
                if (attempts < 3) {
                    throw new Error("fail");
                }
                return "ok";
            },
            { maxRetries: 3, baseDelay: 1, maxDelay: 10 },
        );
        assert.strictEqual(result, "ok");
        assert.strictEqual(attempts, 3);
    });

    test("should throw after exhausting retries", async () => {
        let attempts = 0;
        await assert.rejects(
            () =>
                withRetry(
                    async () => {
                        attempts++;
                        throw new Error("always fails");
                    },
                    { maxRetries: 2, baseDelay: 1, maxDelay: 10 },
                ),
            (err: Error) => {
                assert.ok(err.message.includes("3 attempts"));
                assert.ok(err.message.includes("always fails"));
                return true;
            },
        );
        assert.strictEqual(attempts, 3); // initial + 2 retries
    });

    test("should include context in error message", async () => {
        // `context` is free-form descriptive text, not a URL to be validated.
        // Assert on a plain sentinel rather than a URL literal so this stays a
        // message-content check (a URL literal in .includes() trips CodeQL's
        // js/incomplete-url-substring-sanitization heuristic — a false positive
        // here since no URL is being sanitized).
        await assert.rejects(
            () =>
                withRetry(
                    async () => {
                        throw new Error("network error");
                    },
                    { maxRetries: 0, baseDelay: 1, context: "the release server" },
                ),
            (err: Error) => {
                assert.ok(err.message.includes("the release server"));
                return true;
            },
        );
    });

    test("should not retry when maxRetries is 0", async () => {
        let attempts = 0;
        await assert.rejects(() =>
            withRetry(
                async () => {
                    attempts++;
                    throw new Error("fail");
                },
                { maxRetries: 0, baseDelay: 1 },
            ),
        );
        assert.strictEqual(attempts, 1);
    });

    test("should handle non-Error thrown values", async () => {
        await assert.rejects(
            () =>
                withRetry(
                    async () => {
                        throw "string error";
                    },
                    { maxRetries: 0, baseDelay: 1 },
                ),
            (err: Error) => {
                assert.ok(err.message.includes("string error"));
                return true;
            },
        );
    });

    test("should use default options when none provided", async () => {
        const result = await withRetry(async () => "default");
        assert.strictEqual(result, "default");
    });

    test("should resolve with async values", async () => {
        const result = await withRetry(
            async () => {
                return new Promise<number>((resolve) => setTimeout(() => resolve(99), 5));
            },
            { maxRetries: 0, baseDelay: 1 },
        );
        assert.strictEqual(result, 99);
    });
});

suite("fetchResponse guards", () => {
    test("refuses a plain-HTTP URL (no network call)", async () => {
        await assert.rejects(
            () => fetchResponse("http://example.com/file"),
            /Refusing non-HTTPS request to http:\/\/example\.com\/file/,
        );
    });

    test("refuses a non-URL scheme", async () => {
        await assert.rejects(() => fetchResponse("ftp://example.com/file"), /Refusing non-HTTPS/);
    });

    test("rejects once the redirect budget is exceeded (before any request)", async () => {
        // Passing a redirect count above MAX_REDIRECTS (5) trips the cap before
        // https.get is ever called, so no network access happens.
        await assert.rejects(
            () => fetchResponse("https://example.com/file", 6),
            /Too many redirects/,
        );
    });
});
