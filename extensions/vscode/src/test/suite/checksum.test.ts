import * as assert from "node:assert";
import * as fs from "node:fs";
import * as path from "node:path";
import * as crypto from "node:crypto";

import { parseChecksums, computeSha256, CHECKSUMS_FILENAME } from "../../utils/checksum";

suite("parseChecksums", () => {
    test("should parse standard two-space-separated format", () => {
        const text = "a".repeat(64) + "  luma_lsp-linux-x86_64.tar.gz";
        const result = parseChecksums(text);
        assert.strictEqual(result.size, 1);
        assert.strictEqual(result.get("luma_lsp-linux-x86_64.tar.gz"), "a".repeat(64));
    });

    test("should parse single-space-separated format", () => {
        const text = "b".repeat(64) + " luma_lsp-macos-aarch64.tar.gz";
        const result = parseChecksums(text);
        assert.strictEqual(result.size, 1);
        assert.strictEqual(result.get("luma_lsp-macos-aarch64.tar.gz"), "b".repeat(64));
    });

    test("should parse multiple entries", () => {
        const text = [
            "a".repeat(64) + "  luma_lsp-windows-x86_64.zip",
            "b".repeat(64) + "  luma_lsp-linux-x86_64.tar.gz",
            "c".repeat(64) + "  luma_lsp-macos-aarch64.tar.gz",
        ].join("\n");
        const result = parseChecksums(text);
        assert.strictEqual(result.size, 3);
        assert.strictEqual(result.get("luma_lsp-windows-x86_64.zip"), "a".repeat(64));
        assert.strictEqual(result.get("luma_lsp-linux-x86_64.tar.gz"), "b".repeat(64));
        assert.strictEqual(result.get("luma_lsp-macos-aarch64.tar.gz"), "c".repeat(64));
    });

    test("should skip empty lines", () => {
        const text = "a".repeat(64) + "  file.tar.gz\n\n\n";
        const result = parseChecksums(text);
        assert.strictEqual(result.size, 1);
    });

    test("should reject lines with non-hex hash", () => {
        const text = "xyz-not-a-hash  somefile.tar.gz";
        const result = parseChecksums(text);
        assert.strictEqual(result.size, 0);
    });

    test("should reject lines with short hash", () => {
        const text = "abcd1234  somefile.tar.gz";
        const result = parseChecksums(text);
        assert.strictEqual(result.size, 0);
    });

    test("should normalise hashes to lowercase", () => {
        const text = "A1B2C3D4" + "E5F6A1B2".repeat(7) + "  file.tar.gz";
        const result = parseChecksums(text);
        const hash = result.get("file.tar.gz");
        assert.ok(hash);
        assert.strictEqual(hash, hash.toLowerCase());
    });

    test("should handle Windows-style line endings", () => {
        const text = "a".repeat(64) + "  file1.tar.gz\r\n" + "b".repeat(64) + "  file2.zip\r\n";
        const result = parseChecksums(text);
        assert.strictEqual(result.size, 2);
    });

    test("should return empty map for empty input", () => {
        const result = parseChecksums("");
        assert.strictEqual(result.size, 0);
    });

    test("should trim whitespace around lines", () => {
        const text = "  " + "d".repeat(64) + "  file.tar.gz  ";
        const result = parseChecksums(text);
        assert.strictEqual(result.size, 1);
    });
});

suite("computeSha256", () => {
    const test_dir = path.join(__dirname, "..", "..", "..", "test-fixtures");

    suiteSetup(() => {
        fs.mkdirSync(test_dir, { recursive: true });
    });

    suiteTeardown(() => {
        fs.rmSync(test_dir, { recursive: true, force: true });
    });

    test("should compute correct SHA-256 for known content", async () => {
        const content = "hello world\n";
        const file_path = path.join(test_dir, "sha256-test.txt");
        fs.writeFileSync(file_path, content);

        const expected = crypto.createHash("sha256").update(content).digest("hex");
        const actual = await computeSha256(file_path);
        assert.strictEqual(actual, expected);
    });

    test("should compute correct SHA-256 for empty file", async () => {
        const file_path = path.join(test_dir, "sha256-empty.txt");
        fs.writeFileSync(file_path, "");

        const expected = crypto.createHash("sha256").update("").digest("hex");
        const actual = await computeSha256(file_path);
        assert.strictEqual(actual, expected);
    });

    test("should compute correct SHA-256 for binary content", async () => {
        const content = Buffer.from([0x00, 0x01, 0xff, 0xfe, 0x80]);
        const file_path = path.join(test_dir, "sha256-binary.bin");
        fs.writeFileSync(file_path, content);

        const expected = crypto.createHash("sha256").update(content).digest("hex");
        const actual = await computeSha256(file_path);
        assert.strictEqual(actual, expected);
    });

    test("should reject for non-existent file", async () => {
        await assert.rejects(
            () => computeSha256(path.join(test_dir, "nonexistent.txt")),
            (err: NodeJS.ErrnoException) => err.code === "ENOENT",
        );
    });
});

suite("CHECKSUMS_FILENAME", () => {
    test("should be SHA256SUMS", () => {
        assert.strictEqual(CHECKSUMS_FILENAME, "SHA256SUMS");
    });
});

suite("SHA256SUMS shared conformance fixture", () => {
    // extensions/shared/sha256sums-sample.txt is the golden input shared by the
    // VS Code and Zed parser tests. Running the real parseChecksums over
    // it proves the TypeScript parser agrees with the Zed implementation.
    const sample_path = path.join(
        __dirname,
        "..",
        "..",
        "..",
        "..",
        "shared",
        "sha256sums-sample.txt",
    );

    test("parses the fixture to the canonical entries", () => {
        const sample = fs.readFileSync(sample_path, "utf-8");
        const result = parseChecksums(sample);

        assert.strictEqual(result.size, 4);
        assert.strictEqual(result.get("luma_lsp-linux-x86_64.tar.gz"), "1".repeat(64));
        assert.strictEqual(result.get("luma_lsp-macos-aarch64.tar.gz"), "2".repeat(64));
        assert.strictEqual(result.get("luma_dap-windows-x86_64.zip"), "3".repeat(64));
        assert.strictEqual(result.get("luma-linux-x86_64.tar.gz"), "a".repeat(64));
    });

    test("ignores comments, blank lines and short hashes in the fixture", () => {
        const sample = fs.readFileSync(sample_path, "utf-8");
        const result = parseChecksums(sample);

        assert.strictEqual(result.get("ignored-short-hash.tar.gz"), undefined);
    });
});
