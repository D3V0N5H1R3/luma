// ─────────────────────────────────────────────────────────────
// Luma — Stylelint Configuration
//
// Lints the project's first-party CSS. Currently this is the GraphicalUi
// override sheet (external/gui-framework/gui-overrides.css); vendored,
// minified third-party stylesheets (*.min.css) are excluded.
//
// Extends stylelint-config-standard. The override sheet deliberately styles a
// classless framework (Pico CSS) via element and attribute selectors, so the
// BEM class-naming conventions from instructions/css.instructions.md are a
// human guideline rather than an enforced rule here. The CI gate lives in
// .github/workflows/ci-css.yml.
//
// no-descending-specificity is disabled: the sheet groups all interactive
// controls into a single shared :focus-visible ring (and similar shared
// hover/interaction rules) whose members' base rules are defined throughout
// the sheet. A strict ascending-specificity ordering is neither achievable nor
// meaningful for these cross-cutting state styles, so the rule only produces
// false positives here.
// ─────────────────────────────────────────────────────────────

export default {
    extends: ["stylelint-config-standard"],
    ignoreFiles: ["**/*.min.css", "**/node_modules/**"],
    rules: {
        "no-descending-specificity": null,
    },
};
