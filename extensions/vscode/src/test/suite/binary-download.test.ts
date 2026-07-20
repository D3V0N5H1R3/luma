import * as assert from "node:assert";

import { parseGithubRelease, LSP_CONFIG, DAP_CONFIG } from "../../utils/binary-download";
import { GITHUB_REPO } from "../../generated/config";
import { PLATFORM_MAP, OS_MAP, ARCH_MAP, getPlatformSuffix } from "../../generated/platform";
import { getPlatformAssetName } from "../../utils/binary/platform";

suite("parseGithubRelease", () => {
    test("should parse valid release data", () => {
        const data = {
            tag_name: "v1.0.0",
            assets: [
                {
                    name: "luma_lsp-linux-x86_64.tar.gz",
                    browser_download_url: "https://example.com/a",
                },
            ],
        };
        const result = parseGithubRelease(data);
        assert.strictEqual(result.tag_name, "v1.0.0");
        assert.strictEqual(result.assets.length, 1);
        assert.strictEqual(result.assets[0].name, "luma_lsp-linux-x86_64.tar.gz");
    });

    test("should parse release with empty assets", () => {
        const result = parseGithubRelease({ tag_name: "v2.0.0", assets: [] });
        assert.strictEqual(result.tag_name, "v2.0.0");
        assert.strictEqual(result.assets.length, 0);
    });

    test("should reject null input", () => {
        assert.throws(() => parseGithubRelease(null), /Invalid GitHub release response/);
    });

    test("should reject undefined input", () => {
        assert.throws(() => parseGithubRelease(undefined), /Invalid GitHub release response/);
    });

    test("should reject non-object input", () => {
        assert.throws(() => parseGithubRelease("string"), /Invalid GitHub release response/);
        assert.throws(() => parseGithubRelease(42), /Invalid GitHub release response/);
    });

    test("should reject missing tag_name", () => {
        assert.throws(() => parseGithubRelease({ assets: [] }), /Invalid GitHub release response/);
    });

    test("should reject missing assets", () => {
        assert.throws(
            () => parseGithubRelease({ tag_name: "v1.0.0" }),
            /Invalid GitHub release response/,
        );
    });

    test("should reject non-string tag_name", () => {
        assert.throws(
            () => parseGithubRelease({ tag_name: 123, assets: [] }),
            /Invalid GitHub release response/,
        );
    });

    test("should reject non-array assets", () => {
        assert.throws(
            () => parseGithubRelease({ tag_name: "v1.0.0", assets: "bad" }),
            /Invalid GitHub release response/,
        );
    });

    test("should preserve multiple assets", () => {
        const data = {
            tag_name: "v3.0.0",
            assets: [
                { name: "luma_lsp-linux-x86_64.tar.gz", browser_download_url: "https://a.com" },
                { name: "luma_lsp-windows-x86_64.zip", browser_download_url: "https://b.com" },
                { name: "SHA256SUMS", browser_download_url: "https://c.com" },
            ],
        };
        const result = parseGithubRelease(data);
        assert.strictEqual(result.assets.length, 3);
    });
});

suite("GITHUB_REPO", () => {
    test("should be the correct repository", () => {
        assert.strictEqual(GITHUB_REPO, "d3v0n5h1r3/luma");
    });
});

suite("BinaryConfig constants", () => {
    test("LSP_CONFIG should have correct name", () => {
        assert.strictEqual(LSP_CONFIG.name, "luma_lsp");
        assert.strictEqual(LSP_CONFIG.display_name, "language server");
        assert.ok(LSP_CONFIG.settings_key.includes("lsp.path"));
        assert.ok(LSP_CONFIG.installed_tag_key.includes("lsp"));
    });

    test("DAP_CONFIG should have correct name", () => {
        assert.strictEqual(DAP_CONFIG.name, "luma_dap");
        assert.strictEqual(DAP_CONFIG.display_name, "debug adapter");
        assert.ok(DAP_CONFIG.settings_key.includes("dap.path"));
        assert.ok(DAP_CONFIG.installed_tag_key.includes("dap"));
    });
});

suite("Platform mapping", () => {
    test("OS_MAP should map Node platform strings", () => {
        assert.strictEqual(OS_MAP["linux"], "linux");
        assert.strictEqual(OS_MAP["darwin"], "macos");
        assert.strictEqual(OS_MAP["win32"], "windows");
    });

    test("ARCH_MAP should map Node arch strings", () => {
        assert.strictEqual(ARCH_MAP["x64"], "x86_64");
        assert.strictEqual(ARCH_MAP["arm64"], "aarch64");
    });

    test("PLATFORM_MAP should have entries for all OS/arch combinations", () => {
        for (const os of ["linux", "macos", "windows"]) {
            for (const arch of ["x86_64", "aarch64"]) {
                assert.ok(PLATFORM_MAP[os]?.[arch], `Missing platform entry for ${os}/${arch}`);
            }
        }
    });

    test("Windows entries should use .zip extension", () => {
        for (const arch of Object.values(PLATFORM_MAP["windows"])) {
            assert.ok(arch.endsWith(".zip"), `Windows archive should be .zip: ${arch}`);
        }
    });

    test("Linux entries should use .tar.gz extension", () => {
        for (const arch of Object.values(PLATFORM_MAP["linux"])) {
            assert.ok(arch.endsWith(".tar.gz"), `Linux archive should be .tar.gz: ${arch}`);
        }
    });

    test("macOS entries should use .tar.gz extension", () => {
        for (const arch of Object.values(PLATFORM_MAP["macos"])) {
            assert.ok(arch.endsWith(".tar.gz"), `macOS archive should be .tar.gz: ${arch}`);
        }
    });

    test("getPlatformSuffix should return a string for this platform", () => {
        const suffix = getPlatformSuffix();
        // This test runs on a supported platform so suffix should be defined.
        assert.ok(typeof suffix === "string", "Should return a suffix for the current platform");
        assert.ok(suffix.length > 0, "Suffix should be non-empty");
    });

    test("Unknown OS should return undefined from getPlatformSuffix logic", () => {
        // Verify the lookup logic: unknown keys produce undefined.
        assert.strictEqual(OS_MAP["freebsd"], undefined);
        assert.strictEqual(ARCH_MAP["mips"], undefined);
    });
});

suite("getPlatformAssetName", () => {
    test("composes the binary prefix with the current platform suffix", () => {
        // Binds to the real function (not a hand-copied regex), so it fails if
        // the naming convention drifts — e.g. arch tokens are x86_64/aarch64,
        // not x64/arm64.
        const suffix = getPlatformSuffix();
        assert.ok(typeof suffix === "string", "test host should be a supported platform");
        assert.strictEqual(getPlatformAssetName("luma_lsp"), `luma_lsp-${suffix}`);
        assert.strictEqual(getPlatformAssetName("luma_dap"), `luma_dap-${suffix}`);
    });

    test("produces a name matching the canonical asset convention", () => {
        const name = getPlatformAssetName("luma_lsp");
        assert.match(
            name,
            /^luma_lsp-(linux|macos|windows)-(x86_64|aarch64)\.(tar\.gz|zip)$/,
            `Unexpected asset name: ${name}`,
        );
    });
});
