// @ts-check
import js from "@eslint/js";
import tseslint from "typescript-eslint";
import globals from "globals";

/**
 * Flat ESLint configuration for the Luma VS Code extension.
 *
 * Scope is correctness-oriented static analysis, not formatting. Layout
 * concerns (indentation, quotes, line length) are owned by `.editorconfig`
 * and the TypeScript compiler, so no stylistic rules are enabled here.
 *
 * Naming-convention rules are intentionally omitted: this package follows the
 * documented house deviation that uses `snake_case` for locals, parameters,
 * and fields to match the surrounding Lua, Rust, and C++ sources.
 */
export default tseslint.config(
    {
        ignores: ["out/**", "node_modules/**", "src/generated/**", "**/*.d.ts"],
    },
    js.configs.recommended,
    ...tseslint.configs.recommended,
    {
        files: ["src/**/*.ts"],
        languageOptions: {
            ecmaVersion: 2021,
            sourceType: "module",
            globals: {
                ...globals.node,
            },
        },
        rules: {
            "@typescript-eslint/no-unused-vars": [
                "error",
                {
                    argsIgnorePattern: "^_",
                    varsIgnorePattern: "^_",
                    caughtErrorsIgnorePattern: "^_",
                },
            ],
        },
    },
    {
        files: ["**/*.js"],
        languageOptions: {
            ecmaVersion: 2021,
            sourceType: "commonjs",
            globals: {
                ...globals.node,
            },
        },
        rules: {
            "@typescript-eslint/no-require-imports": "off",
            "no-unused-vars": [
                "error",
                {
                    argsIgnorePattern: "^_",
                    varsIgnorePattern: "^_",
                    caughtErrorsIgnorePattern: "^_",
                },
            ],
        },
    },
);
