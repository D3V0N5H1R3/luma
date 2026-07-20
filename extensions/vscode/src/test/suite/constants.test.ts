import * as assert from "node:assert";

import { BINARY_NAMES, CONFIG_SECTION, CONFIG_KEYS } from "../../utils/constants";

suite("BINARY_NAMES", () => {
    test("LSP should be luma_lsp", () => {
        assert.strictEqual(BINARY_NAMES.LSP, "luma_lsp");
    });

    test("DAP should be luma_dap", () => {
        assert.strictEqual(BINARY_NAMES.DAP, "luma_dap");
    });

    test("INTERPRETER should be luma", () => {
        assert.strictEqual(BINARY_NAMES.INTERPRETER, "luma");
    });
});

suite("CONFIG_SECTION", () => {
    test("should be 'luma'", () => {
        assert.strictEqual(CONFIG_SECTION, "luma");
    });
});

suite("CONFIG_KEYS", () => {
    test("LSP_PATH should be 'lsp.path'", () => {
        assert.strictEqual(CONFIG_KEYS.LSP_PATH, "lsp.path");
    });

    test("LSP_AUTO_UPDATE should be 'lsp.autoUpdate'", () => {
        assert.strictEqual(CONFIG_KEYS.LSP_AUTO_UPDATE, "lsp.autoUpdate");
    });

    test("DAP_PATH should be 'dap.path'", () => {
        assert.strictEqual(CONFIG_KEYS.DAP_PATH, "dap.path");
    });

    test("INTERPRETER_PATH should be 'path'", () => {
        assert.strictEqual(CONFIG_KEYS.INTERPRETER_PATH, "path");
    });
});
