import js from "@eslint/js";

export default [
    js.configs.recommended,
    {
        languageOptions: {
            ecmaVersion: 2022,
            sourceType: "script",
            globals: {
                // Browser globals
                window: "readonly",
                document: "readonly",
                console: "readonly",
                setTimeout: "readonly",
                clearTimeout: "readonly",
                setInterval: "readonly",
                clearInterval: "readonly",
                requestAnimationFrame: "readonly",
                cancelAnimationFrame: "readonly",
                MutationObserver: "readonly",
                ResizeObserver: "readonly",
                IntersectionObserver: "readonly",
                HTMLElement: "readonly",
                Event: "readonly",
                fetch: "readonly",
                URL: "readonly",
                CSS: "readonly",
                getComputedStyle: "readonly",
                performance: "readonly",

                // GraphicalUi C++ bridge
                __gui_event: "readonly",

                // Lucide icon data injected into the renderer at build time
                // (const __lucide_icons = …, see scripts/generate_gui_assets.mjs)
                __lucide_icons: "readonly",

                // Shared tables populated by renderer fragments
                WIDGET_RENDERERS: "writable",
                CHART_TYPES: "writable",

                // Module-private helpers from the gui-renderer.js IIFE scope,
                // available to the concatenated renderer fragments
                html: "readonly",
                nothing: "readonly",
                svg: "readonly",
                render: "readonly",
                renderWidget: "readonly",
                renderChildren: "readonly",
                mergeClass: "readonly",
                boundHandler: "readonly",
                joinStyle: "readonly",
                toKebabCase: "readonly",
                sanitizeUrl: "readonly",
                buildAriaAttrs: "readonly",
                emit: "readonly",

                // Vendored libraries exposed on window
                lucide: "readonly",
                uPlot: "readonly",
            },
        },
        rules: {
            "no-unused-vars": ["warn", { argsIgnorePattern: "^_" }],
            "no-undef": "error",
            "no-redeclare": "error",
            "no-constant-binary-expression": "error",
            "no-self-compare": "error",
            "no-template-curly-in-string": "warn",
            eqeqeq: ["error", "always", { null: "ignore" }],
        },
    },
    {
        // The renderers/*.js fragments are spliced into gui-renderer.js's IIFE
        // at build time (scripts/generate_gui_assets.mjs), so they reference its
        // module-private helpers and define helpers used by sibling fragments.
        // Neither cross-scope rule can be resolved when linting a fragment on
        // its own; the concatenated framework is exercised by ci-gui-framework's
        // node --test suite, which catches genuine undefined references.
        files: ["renderers/**/*.js"],
        rules: {
            "no-undef": "off",
            "no-unused-vars": "off",
        },
    },
    {
        // gui-renderer.js defines module-private helpers consumed only by the
        // spliced-in fragments, so they read as unused when it is linted alone.
        files: ["gui-renderer.js"],
        rules: {
            "no-unused-vars": "off",
        },
    },
];
