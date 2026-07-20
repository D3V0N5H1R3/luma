/* Unit tests for gui-renderer.js — the lit-html renderer core.
 *
 * Focus areas, in priority order:
 *   1. Security sanitisers (URL scheme allow-listing, CSS value/name/selector
 *      validation) — the browser-side second line of defence that mirrors the
 *      C++ host guards. A regression here is an XSS / style-injection hole.
 *   2. Pure style / ARIA / roving-focus helpers.
 *   3. Theme and stylesheet-injection window APIs (DOM-observable behaviour).
 *
 * Run: node --test external/gui-framework/tests/*.test.mjs
 *
 * SPDX-License-Identifier: MIT
 */

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { loadFramework, plain } from "./gui-test-harness.mjs";

const CAPTURE = [
    "sanitizeUrl",
    "sanitizeCssValue",
    "isSafeCssName",
    "isSafePseudoSelector",
    "toKebabCase",
    "composeStyle",
    "mergeClass",
    "buildInlineStyle",
    "buildStyleStr",
    "buildAriaAttrs",
    "rovingFocusIndex",
    "GUI_LINK_SCHEMES",
    "GUI_IMG_SCHEMES",
    "GUI_IMG_SCHEMES_LOCAL",
    "imgSchemes",
    "__gui_has_accessible_name",
];

// Pure helpers hold no cross-test state, so one shared load is fine.
const { internals: R } = loadFramework("gui-renderer.js", { capture: CAPTURE });

// Fresh instance for tests that mutate DOM/window state.
function freshRenderer() {
    return loadFramework("gui-renderer.js", { capture: CAPTURE });
}

describe("sanitizeUrl — scheme allow-listing", () => {
    const link = R.GUI_LINK_SCHEMES;
    const img = R.GUI_IMG_SCHEMES;

    it("exposes the expected allow-lists", () => {
        assert.deepEqual(plain(link), ["http", "https", "mailto", "tel"]);
        assert.deepEqual(plain(img), ["http", "https", "data", "blob"]);
    });

    it("blocks javascript: and vbscript: URLs", () => {
        assert.equal(R.sanitizeUrl("javascript:alert(1)", link), "");
        assert.equal(R.sanitizeUrl("vbscript:msgbox(1)", link), "");
    });

    it("defeats obfuscation with embedded tab/newline/CR in the scheme", () => {
        assert.equal(R.sanitizeUrl("java\tscript:alert(1)", link), "");
        assert.equal(R.sanitizeUrl("java\nscript:alert(1)", link), "");
        assert.equal(R.sanitizeUrl("ja\rvascript:alert(1)", link), "");
    });

    it("allows configured schemes and preserves the original string", () => {
        assert.equal(R.sanitizeUrl("https://example.com/x", link), "https://example.com/x");
        assert.equal(R.sanitizeUrl("mailto:a@b.com", link), "mailto:a@b.com");
        assert.equal(R.sanitizeUrl("tel:+15551234", link), "tel:+15551234");
    });

    it("treats the scheme case-insensitively", () => {
        assert.equal(R.sanitizeUrl("HTTPS://example.com", link), "HTTPS://example.com");
        assert.equal(R.sanitizeUrl("JavaScript:alert(1)", link), "");
    });

    it("allows relative URLs (no scheme, or non-scheme char before colon)", () => {
        assert.equal(R.sanitizeUrl("/path/to/page", link), "/path/to/page");
        assert.equal(R.sanitizeUrl("foo/bar", link), "foo/bar");
        // A "/" before the ":" means it is a path, not a scheme.
        assert.equal(R.sanitizeUrl("a/b:c", link), "a/b:c");
    });

    it("returns empty for empty or non-string input", () => {
        assert.equal(R.sanitizeUrl("", link), "");
        assert.equal(R.sanitizeUrl(null, link), "");
        assert.equal(R.sanitizeUrl(undefined, link), "");
        assert.equal(R.sanitizeUrl(42, link), "");
    });

    it("distinguishes link vs image scheme sets (data: only for images)", () => {
        const dataUrl = "data:image/png;base64,AAAA";
        assert.equal(R.sanitizeUrl(dataUrl, img), dataUrl);
        assert.equal(R.sanitizeUrl(dataUrl, link), "");
        assert.equal(R.sanitizeUrl("blob:abc-123", img), "blob:abc-123");
        assert.equal(R.sanitizeUrl("blob:abc-123", link), "");
    });
});

describe("imgSchemes — remote images off by default", () => {
    it("exposes the local-only scheme set", () => {
        assert.deepEqual(plain(R.GUI_IMG_SCHEMES_LOCAL), ["data", "blob"]);
    });

    it("returns only data:/blob: when the app has not opted in", () => {
        const r = freshRenderer();
        r.window.__gui_allow_remote_images = false;
        assert.deepEqual(plain(r.internals.imgSchemes()), ["data", "blob"]);
    });

    it("widens to the full remote set once opted in", () => {
        const r = freshRenderer();
        r.window.__gui_allow_remote_images = true;
        assert.deepEqual(plain(r.internals.imgSchemes()), ["http", "https", "data", "blob"]);
    });

    it("strips remote http(s) image sources by default but keeps data:/blob: and relative", () => {
        const r = freshRenderer();
        r.window.__gui_allow_remote_images = false;
        const schemes = r.internals.imgSchemes();
        assert.equal(r.internals.sanitizeUrl("https://tracker.example/pixel.png", schemes), "");
        assert.equal(r.internals.sanitizeUrl("http://tracker.example/pixel.png", schemes), "");
        assert.equal(r.internals.sanitizeUrl("data:image/png;base64,AAAA", schemes), "data:image/png;base64,AAAA");
        assert.equal(r.internals.sanitizeUrl("blob:abc-123", schemes), "blob:abc-123");
        assert.equal(r.internals.sanitizeUrl("/local/logo.png", schemes), "/local/logo.png");
    });

    it("permits remote http(s) image sources once opted in", () => {
        const r = freshRenderer();
        r.window.__gui_allow_remote_images = true;
        const schemes = r.internals.imgSchemes();
        assert.equal(
            r.internals.sanitizeUrl("https://cdn.example/logo.png", schemes),
            "https://cdn.example/logo.png",
        );
        assert.equal(
            r.internals.sanitizeUrl("http://cdn.example/logo.png", schemes),
            "http://cdn.example/logo.png",
        );
    });
});

describe("sanitizeCssValue — inline value hardening", () => {
    it("blocks the behavior and -moz-binding properties entirely", () => {
        assert.equal(R.sanitizeCssValue("behavior", "url(x.htc)"), null);
        assert.equal(R.sanitizeCssValue("Behavior", "url(x.htc)"), null);
        assert.equal(R.sanitizeCssValue("-moz-binding", "url(x.xml)"), null);
    });

    it("strips rule-breakout characters ; { }", () => {
        assert.equal(R.sanitizeCssValue("color", "red;}injected{"), "redinjected");
    });

    it("neutralises url(javascript:) and url(vbscript:) payloads", () => {
        const out = R.sanitizeCssValue("background", "url(javascript:alert(1))");
        assert.ok(!/javascript:/i.test(out), `expected javascript: to be neutralised, got ${out}`);
        assert.ok(out.includes("about:blank"));
    });

    it("neutralises the expression() IE hack", () => {
        const out = R.sanitizeCssValue("width", "expression(alert(1))");
        assert.ok(out.startsWith("blocked-expression("));
    });

    it("passes through ordinary values unchanged", () => {
        assert.equal(R.sanitizeCssValue("color", "red"), "red");
        assert.equal(R.sanitizeCssValue("padding", "8px 12px"), "8px 12px");
    });
});

describe("isSafeCssName — property-name validation", () => {
    it("accepts ordinary and vendor-prefixed / custom property names", () => {
        for (const name of ["color", "font-size", "-webkit-transform", "--custom-x"]) {
            assert.equal(R.isSafeCssName(name), true, name);
        }
    });

    it("rejects names that could break out of a rule or start invalidly", () => {
        for (const name of ["color}", "a{b", "1color", "", "color;", "co lor"]) {
            assert.equal(R.isSafeCssName(name), false, name);
        }
    });
});

describe("isSafePseudoSelector — selector validation", () => {
    it("accepts single-colon pseudo-classes", () => {
        for (const sel of [":hover", ":focus", ":focus-within", ":active"]) {
            assert.equal(R.isSafePseudoSelector(sel), true, sel);
        }
    });

    it("rejects breakout, missing-colon, and double-colon selectors", () => {
        for (const sel of [":hover{}", "hover", "::before", ":", ":a b"]) {
            assert.equal(R.isSafePseudoSelector(sel), false, sel);
        }
    });
});

describe("toKebabCase", () => {
    it("converts underscores to hyphens", () => {
        assert.equal(R.toKebabCase("font_size"), "font-size");
        assert.equal(R.toKebabCase("aria_label"), "aria-label");
        assert.equal(R.toKebabCase("a_b_c"), "a-b-c");
    });

    it("leaves hyphen-free names untouched", () => {
        assert.equal(R.toKebabCase("color"), "color");
    });
});

describe("composeStyle", () => {
    it("joins truthy parts with semicolons and drops falsy ones", () => {
        assert.equal(R.composeStyle("a", "", "b", null, undefined, "c"), "a;b;c");
        assert.equal(R.composeStyle("only"), "only");
        assert.equal(R.composeStyle(), "");
    });
});

describe("mergeClass", () => {
    it("appends an extra class only when present", () => {
        assert.equal(R.mergeClass("gui-btn", "extra"), "gui-btn extra");
        assert.equal(R.mergeClass("gui-btn", ""), "gui-btn");
        assert.equal(R.mergeClass("gui-btn", undefined), "gui-btn");
    });
});

describe("buildInlineStyle", () => {
    it("kebab-cases properties and sanitises values", () => {
        const { parts, cls, pseudoRules } = R.buildInlineStyle({
            font_size: "14px",
            color: "red",
        });
        assert.deepEqual(plain(parts), ["font-size:14px", "color:red"]);
        assert.equal(cls, "");
        assert.deepEqual(plain(pseudoRules), {});
    });

    it("extracts the class key and skips event/aria/id/role keys", () => {
        const { parts, cls } = R.buildInlineStyle({
            class: "myclass",
            on_click: "cb",
            id: "el",
            aria_label: "x",
            role: "button",
            color: "blue",
        });
        assert.equal(cls, "myclass");
        assert.deepEqual(plain(parts), ["color:blue"]);
    });

    it("routes pseudo-prefixed keys into pseudoRules", () => {
        const { parts, pseudoRules } = R.buildInlineStyle({ hover_background: "blue" });
        assert.deepEqual(plain(parts), []);
        assert.deepEqual(plain(pseudoRules), { ":hover": { background: "blue" } });
    });

    it("matches the longer focus_within_ prefix before focus_", () => {
        const { pseudoRules } = R.buildInlineStyle({ focus_within_color: "green" });
        assert.deepEqual(plain(pseudoRules), { ":focus-within": { color: "green" } });
    });

    it("handles an explicit pseudo object, including unknown pseudo-classes", () => {
        const { pseudoRules } = R.buildInlineStyle({
            pseudo: {
                hover: { text_color: "red" },
                "focus-visible": { outline: "1px" },
            },
        });
        assert.deepEqual(plain(pseudoRules), {
            ":hover": { "text-color": "red" },
            ":focus-visible": { outline: "1px" },
        });
    });

    it("drops blocked properties (behavior) from inline output", () => {
        const { parts } = R.buildInlineStyle({ behavior: "url(x.htc)", color: "red" });
        assert.deepEqual(plain(parts), ["color:red"]);
    });
});

describe("buildStyleStr", () => {
    it("returns empty style/class for a null style", () => {
        assert.deepEqual(plain(R.buildStyleStr(null)), { style: "", cls: "" });
    });

    it("joins inline parts and preserves the explicit class", () => {
        assert.deepEqual(plain(R.buildStyleStr({ class: "c", color: "red" })), {
            style: "color:red",
            cls: "c",
        });
    });

    it("interns pseudo rules into a scoped class name", () => {
        const { style, cls } = R.buildStyleStr({ hover_color: "red" });
        assert.equal(style, "");
        assert.match(cls, /^gui-ps-\d+$/);
    });

    it("combines an explicit class with the interned pseudo class", () => {
        const { cls } = R.buildStyleStr({ class: "base", hover_color: "red" });
        assert.match(cls, /^base gui-ps-\d+$/);
    });
});

describe("buildAriaAttrs", () => {
    it("maps _aria_* keys to aria-* attributes and applies default roles", () => {
        assert.deepEqual(plain(R.buildAriaAttrs({ _aria_label: "Save", type: "button" })), {
            "aria-label": "Save",
            role: "button",
        });
    });

    it("lets an explicit _role override the default role", () => {
        assert.deepEqual(plain(R.buildAriaAttrs({ type: "button", _role: "menuitem" })), {
            role: "menuitem",
        });
    });

    it("adds no role for an unknown widget type", () => {
        assert.deepEqual(plain(R.buildAriaAttrs({ type: "totally_unknown" })), {});
    });

    it("surfaces _element_id as the id attribute and stringifies aria values", () => {
        assert.deepEqual(
            plain(R.buildAriaAttrs({ _element_id: "el1", type: "image", _aria_hidden: true })),
            {
                id: "el1",
                role: "img",
                "aria-hidden": "true",
            },
        );
    });
});

describe("__gui_has_accessible_name", () => {
    // Minimal element stub exposing only the surface the predicate reads.
    function nameEl({ attrs = {}, text = "", kids = [] } = {}) {
        return {
            getAttribute: (n) =>
                (Object.prototype.hasOwnProperty.call(attrs, n) ? attrs[n] : null),
            textContent: text,
            querySelectorAll: () => kids,
        };
    }
    // Stub <img>/<svg> descendant with its own attribute + <title> surface.
    function iconEl({ alt = null, ariaLabel = null, title = null } = {}) {
        return {
            getAttribute: (n) =>
                (n === "alt" ? alt : n === "aria-label" ? ariaLabel : null),
            querySelector: (sel) =>
                (sel === "title" && title ? { textContent: title } : null),
        };
    }

    it("accepts an explicit aria-label", () => {
        assert.equal(R.__gui_has_accessible_name(nameEl({ attrs: { "aria-label": "Add" } })), true);
    });

    it("accepts a title attribute", () => {
        assert.equal(R.__gui_has_accessible_name(nameEl({ attrs: { title: "Add" } })), true);
    });

    it("accepts real visible text", () => {
        assert.equal(R.__gui_has_accessible_name(nameEl({ text: "Save" })), true);
        assert.equal(R.__gui_has_accessible_name(nameEl({ text: "OK" })), true);
    });

    it("rejects an empty control", () => {
        assert.equal(R.__gui_has_accessible_name(nameEl({})), false);
        assert.equal(R.__gui_has_accessible_name(nameEl({ attrs: { "aria-label": "  " } })), false);
    });

    it("rejects a bare symbol/glyph as a name", () => {
        assert.equal(R.__gui_has_accessible_name(nameEl({ text: "+" })), false);
        assert.equal(R.__gui_has_accessible_name(nameEl({ text: "×" })), false);
        assert.equal(R.__gui_has_accessible_name(nameEl({ text: "☰" })), false);
    });

    it("rejects an icon-only control with no labelled descendant", () => {
        assert.equal(
            R.__gui_has_accessible_name(nameEl({ kids: [iconEl({})] })),
            false,
        );
    });

    it("accepts a labelled image/icon descendant", () => {
        assert.equal(
            R.__gui_has_accessible_name(nameEl({ kids: [iconEl({ alt: "Add" })] })),
            true,
        );
        assert.equal(
            R.__gui_has_accessible_name(nameEl({ kids: [iconEl({ ariaLabel: "Add" })] })),
            true,
        );
        assert.equal(
            R.__gui_has_accessible_name(nameEl({ kids: [iconEl({ title: "Add" })] })),
            true,
        );
    });
});

describe("rovingFocusIndex", () => {
    it("returns -1 for an empty collection", () => {
        assert.equal(R.rovingFocusIndex("ArrowDown", 0, 0), -1);
    });

    it("handles Home and End regardless of orientation", () => {
        assert.equal(R.rovingFocusIndex("Home", 2, 3), 0);
        assert.equal(R.rovingFocusIndex("End", 0, 3), 2);
    });

    it("wraps vertically with ArrowUp/ArrowDown by default", () => {
        assert.equal(R.rovingFocusIndex("ArrowDown", 0, 3), 1);
        assert.equal(R.rovingFocusIndex("ArrowDown", 2, 3), 0);
        assert.equal(R.rovingFocusIndex("ArrowUp", 0, 3), 2);
        assert.equal(R.rovingFocusIndex("ArrowUp", 1, 3), 0);
    });

    it("ignores horizontal arrows in the default vertical orientation", () => {
        assert.equal(R.rovingFocusIndex("ArrowRight", 0, 3), -1);
        assert.equal(R.rovingFocusIndex("ArrowLeft", 0, 3), -1);
    });

    it("uses left/right when orientation is horizontal", () => {
        assert.equal(R.rovingFocusIndex("ArrowRight", 0, 3, "horizontal"), 1);
        assert.equal(R.rovingFocusIndex("ArrowLeft", 0, 3, "horizontal"), 2);
        assert.equal(R.rovingFocusIndex("ArrowDown", 0, 3, "horizontal"), -1);
    });

    it("accepts both axes when orientation is both", () => {
        assert.equal(R.rovingFocusIndex("ArrowRight", 0, 3, "both"), 1);
        assert.equal(R.rovingFocusIndex("ArrowUp", 0, 3, "both"), 2);
    });

    it("returns -1 for unrelated keys", () => {
        assert.equal(R.rovingFocusIndex("Enter", 0, 3), -1);
    });
});

describe("window.__gui_apply_theme", () => {
    it("maps theme keys to --gui-* custom properties", () => {
        const { window, document } = freshRenderer();
        window.__gui_apply_theme({ accent: "#f00", background: "#fff", text_color: "#111" });
        const style = document.documentElement.style;
        assert.equal(style.getPropertyValue("--gui-primary"), "#f00");
        assert.equal(style.getPropertyValue("--gui-bg"), "#fff");
        assert.equal(style.getPropertyValue("--gui-fg"), "#111");
        assert.deepEqual(window.__gui_theme, { accent: "#f00", background: "#fff", text_color: "#111" });
    });

    it("selects the dark/light variant of a mode-aware value", () => {
        const dark = freshRenderer();
        dark.window.__gui_apply_theme({ text_color: { light: "#000", dark: "#fff" }, mode: "dark" });
        assert.equal(dark.document.documentElement.style.getPropertyValue("--gui-fg"), "#fff");

        const light = freshRenderer();
        light.window.__gui_apply_theme({ text_color: { light: "#000", dark: "#fff" }, mode: "light" });
        assert.equal(light.document.documentElement.style.getPropertyValue("--gui-fg"), "#000");
    });

    it("maps custom_* keys to --gui-custom-* properties", () => {
        const { window, document } = freshRenderer();
        window.__gui_apply_theme({ custom_brand: "#123456" });
        assert.equal(document.documentElement.style.getPropertyValue("--gui-custom-brand"), "#123456");
    });

    it("maps text_muted to the --gui-text-muted property", () => {
        const { window, document } = freshRenderer();
        window.__gui_apply_theme({ text_muted: "#64748b" });
        assert.equal(document.documentElement.style.getPropertyValue("--gui-text-muted"), "#64748b");
    });

    it("disables motion via data-gui-motion when animations is false", () => {
        const { window, document } = freshRenderer();
        const root = document.documentElement;
        window.__gui_apply_theme({ animations: false });
        assert.equal(root.getAttribute("data-gui-motion"), "off");
    });

    it("re-enables motion when animations is not false", () => {
        const { window, document } = freshRenderer();
        const root = document.documentElement;
        window.__gui_apply_theme({ animations: false });
        assert.equal(root.getAttribute("data-gui-motion"), "off");
        window.__gui_apply_theme({ animations: true });
        assert.equal(root.getAttribute("data-gui-motion"), null);
        window.__gui_apply_theme({ accent: "#f00" });
        assert.equal(root.getAttribute("data-gui-motion"), null);
    });
});

describe("window.__gui_set_theme_mode", () => {
    it("sets and clears the data-theme attribute and color-scheme", () => {
        const { window, document } = freshRenderer();
        const root = document.documentElement;

        window.__gui_set_theme_mode("dark");
        assert.equal(root.getAttribute("data-theme"), "dark");
        assert.equal(root.style.getPropertyValue("color-scheme"), "dark");

        window.__gui_set_theme_mode("auto");
        assert.equal(root.getAttribute("data-theme"), null);
        assert.equal(root.style.getPropertyValue("color-scheme"), "");
    });
});

describe("window.__gui_inject_css", () => {
    it("injects a stylesheet once and deduplicates identical content", () => {
        const { window, document } = freshRenderer();
        window.__gui_inject_css(".a{color:red}");
        window.__gui_inject_css(".a{color:red}");
        assert.equal(document.head.children.length, 1);

        window.__gui_inject_css(".b{color:blue}");
        assert.equal(document.head.children.length, 2);
    });
});

describe("window.__gui_get_width", () => {
    it("reports the current window width", () => {
        const { window } = freshRenderer();
        assert.equal(window.__gui_get_width(), 1024);
    });
});

describe("window.__gui_error_toast — last-good-frame error overlay", () => {
    // Locates the toast among <body>'s children (load-time code may append
    // other nodes such as injected <style> elements).
    function findToast(document) {
        return document.body.children.find((c) => c.getAttribute("role") === "alert");
    }

    it("appends a single accessible alert to <body> without touching gui-root", () => {
        const { window, document } = freshRenderer();

        window.__gui_error_toast("boom");

        const toast = findToast(document);
        assert.ok(toast, "an alert element is appended to <body>");
        assert.equal(toast.id, "gui-error-toast");
        assert.equal(toast.getAttribute("aria-live"), "assertive");
        assert.equal(toast.firstChild.textContent, "boom");
    });

    it("reuses the same element on repeat calls rather than stacking overlays", () => {
        const { window, document } = freshRenderer();

        window.__gui_error_toast("first");
        window.__gui_error_toast("second");

        const alerts = document.body.children.filter((c) => c.getAttribute("role") === "alert");
        assert.equal(alerts.length, 1);
        assert.equal(alerts[0].firstChild.textContent, "second");
    });

    it("clears the toast on demand and detaches it from the document", () => {
        const { window, document } = freshRenderer();

        window.__gui_error_toast("boom");
        const toast = findToast(document);
        assert.ok(toast);

        window.__gui_clear_error_toast();
        assert.equal(findToast(document), undefined);
        assert.equal(toast.parentNode, null);
    });
});

describe("makeCommitKeyHandler / makeCommitChangeHandler — on_commit fires exactly once", () => {
    // The renderer's internal emit() calls the bare global __gui_event, so load
    // a fresh renderer with a recording __gui_event to observe commits. Returns
    // the captured handler factories plus the list of emitted payloads.
    function loadWithEmits() {
        const emits = [];
        const { internals } = loadFramework("gui-renderer.js", {
            capture: ["makeCommitKeyHandler", "makeCommitChangeHandler"],
            globals: { __gui_event: (json) => emits.push(JSON.parse(json)) },
        });
        return { internals, emits };
    }

    // Build a fake keyboard event whose target records blur() calls.
    function keyEvent(key, value, mods = {}) {
        let blurs = 0;
        const e = {
            key,
            ctrlKey: !!mods.ctrl,
            metaKey: !!mods.meta,
            defaultPrevented: false,
            preventDefault() {
                this.defaultPrevented = true;
            },
            target: {
                value,
                blur() {
                    blurs += 1;
                },
            },
        };
        return { e, blurCount: () => blurs };
    }

    it("text_input Enter blurs the field and does not emit directly", () => {
        const { internals, emits } = loadWithEmits();
        const onKey = internals.makeCommitKeyHandler("cb", false);

        const { e, blurCount } = keyEvent("Enter", "hello");
        onKey(e);

        assert.equal(blurCount(), 1, "Enter blurs the input");
        assert.equal(emits.length, 0, "the key handler itself emits nothing");
    });

    it("text_input Enter-then-native-change yields a single commit", () => {
        const { internals, emits } = loadWithEmits();
        const onKey = internals.makeCommitKeyHandler("cb", false);
        const onChange = internals.makeCommitChangeHandler("cb");

        // Real DOM order: keydown(Enter) fires first (→ blur), then the native
        // change the blur triggers. Only the change delivers the commit.
        const { e } = keyEvent("Enter", "hello");
        onKey(e);
        onChange({ target: { value: "hello" } });

        assert.equal(emits.length, 1, "exactly one commit reaches the host");
        assert.deepEqual(emits[0], { type: "change", id: "cb", value: "hello" });
    });

    it("text_area Ctrl+Enter commits once and suppresses the newline", () => {
        const { internals, emits } = loadWithEmits();
        const onKey = internals.makeCommitKeyHandler("cb", true);

        const plain = keyEvent("Enter", "line");
        onKey(plain.e);
        assert.equal(emits.length, 0, "a plain Enter inserts a newline, never commits");
        assert.equal(plain.e.defaultPrevented, false, "plain Enter keeps its default newline");
        assert.equal(plain.blurCount(), 0);

        const chord = keyEvent("Enter", "line", { ctrl: true });
        onKey(chord.e);
        assert.equal(emits.length, 1, "Ctrl+Enter commits");
        assert.deepEqual(emits[0], { type: "change", id: "cb", value: "line" });
        assert.equal(chord.e.defaultPrevented, true, "the committing newline is suppressed");
        assert.equal(chord.blurCount(), 0, "text_area commits in place without blurring");
    });

    it("ignores non-Enter keys on both variants", () => {
        const { internals, emits } = loadWithEmits();
        const onInputKey = internals.makeCommitKeyHandler("cb", false);
        const onAreaKey = internals.makeCommitKeyHandler("cb", true);

        const a = keyEvent("a", "hello");
        onInputKey(a.e);
        onAreaKey(a.e);

        assert.equal(emits.length, 0);
        assert.equal(a.blurCount(), 0);
    });
});

