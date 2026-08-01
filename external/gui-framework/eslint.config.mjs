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
                performance: "readonly",

                // GraphicalUi C++ bridge
                __gui_event: "readonly",

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
];
