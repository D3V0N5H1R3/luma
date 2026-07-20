/* GraphicalUi renderer — lit-html based.
 *
 * Uses lit-html's efficient tagged-template rendering engine.
 *
 * SPDX-License-Identifier: MIT
 */
(function() {
    "use strict";

    const html = window.litHtml.html;
    const render = window.litHtml.render;
    const nothing = window.litHtml.nothing;
    const svg = window.litHtml.svg;

    // ── Shared helpers ───────────────────────────────
    // Single event-emission entry point, shared
    // with the subscription manager via window.__gui_emit.
    const emit = (payload) => {
        __gui_event(JSON.stringify(payload));
    };
    window.__gui_emit = emit;

    // snake_case → kebab-case conversion, used for
    // CSS properties, ARIA attributes, and theme keys.
    function toKebabCase(name) {
        return name.replace(/_/g, "-");
    }

    // Compose a style string from parts, dropping
    // empty/falsey segments.
    function composeStyle(...parts) {
        return parts.filter(Boolean).join(";");
    }

    // ── Stylesheet injection & deduplication ─────────
    // Map keyed on full CSS string to avoid hash
    // collisions from the previous 32-bit DJB2 hash.
    const __gui_injected_css = new Map();

    /**
     * Injects a CSS stylesheet into the document head,
     * deduplicating by content.
     */
    window.__gui_inject_css = (css) => {
        if (__gui_injected_css.has(css)) {
            return;
        }
        __gui_injected_css.set(css, true);
        const s = document.createElement("style");
        s.textContent = css;
        document.head.appendChild(s);
    };

    // ── Pseudo-class CSS generation (incremental) ──────
    const __gui_pseudo_map = {
        "hover_": ":hover",
        "focus_": ":focus",
        "active_": ":active",
        "disabled_": ":disabled",
        "focus_within_": ":focus-within",
    };
    const __gui_pseudo_prefixes = [
        "focus_within_", "disabled_", "hover_", "focus_", "active_",
    ];

    // ── Window width tracking ──────────────────────
    let __gui_window_width = window.innerWidth || 800;
    window.addEventListener("resize", () => {
        __gui_window_width = window.innerWidth || 800;
    });

    // ── CSS value sanitisation ────────────────────
    // Extracted from pseudo-class generation so it
    // can be unit-tested independently and reused for inline
    // styles (which previously had no sanitisation).
    function sanitizeCssValue(property, value) {
        const pl = property.toLowerCase();
        if (pl === "behavior" || pl === "-moz-binding") {
            return null;
        }
        let sv = String(value).replace(/[;{}]/g, "");
        sv = sv.replace(
            /url\s*\(\s*['"]?\s*(javascript|vbscript|data\s*:\s*text)\s*:/gi,
            "url(about:blank/*",
        );
        sv = sv.replace(
            /expression\s*\(/gi,
            "blocked-expression(",
        );
        return sv;
    }

    // CSS identifiers — property names and
    // pseudo selectors — are concatenated into <style> blocks, so they
    // must be validated, not merely escaped: a name/selector containing
    // "}" or "{" breaks out of the current rule. sanitizeCssValue only
    // guards values, leaving these identifier paths open.
    function isSafeCssName(name) {
        return /^-{0,2}[a-zA-Z][a-zA-Z0-9-]*$/.test(name);
    }
    function isSafePseudoSelector(selector) {
        return /^:[a-zA-Z][a-zA-Z0-9-]*$/.test(selector);
    }

    // ── URL sanitisation ──────────────────────────
    // Model-derived strings flow into anchor href and image src bindings.
    // lit-html escapes attribute values but does NOT validate URL schemes, so a
    // "javascript:" / "vbscript:" href would execute script (and can reach the
    // __gui_event bridge to C++). Mirror the C++ has_allowed_url_scheme guard
    // (graphicalui_commands.cpp): strip tab/newline/CR, lowercase the scheme, and
    // allow only the given schemes; relative URLs (no scheme) are always allowed.
    const GUI_LINK_SCHEMES = ["http", "https", "mailto", "tel"];
    const GUI_IMG_SCHEMES = ["http", "https", "data", "blob"];
    // Remote images are OFF by default: only self-contained sources (data:/blob:)
    // and relative URLs load unless the app opts in with "allow_remote_images".
    // This keeps a beginner app from silently leaking the user's IP / "app opened"
    // signal to third-party servers via <img> tracking pixels. When opted in, the
    // full remote set applies (and the host CSP img-src is widened to match).
    const GUI_IMG_SCHEMES_LOCAL = ["data", "blob"];

    function imgSchemes() {
        return window.__gui_allow_remote_images ? GUI_IMG_SCHEMES : GUI_IMG_SCHEMES_LOCAL;
    }

    function sanitizeUrl(url, allowedSchemes) {
        if (typeof url !== "string" || url === "") {
            return "";
        }
        const cleaned = url.replace(/[\t\n\r]/g, "");
        const colon = cleaned.indexOf(":");
        let i = 0;
        while (i < cleaned.length && cleaned.charCodeAt(i) <= 0x20) {
            i += 1;
        }
        for (let j = i; j < colon; j += 1) {
            const c = cleaned[j];
            if (!/[a-zA-Z0-9+.-]/.test(c)) {
                return cleaned; // Non-scheme char before ":" → relative URL.
            }
        }
        if (colon < 0) {
            return cleaned; // No scheme → relative URL.
        }
        const scheme = cleaned.slice(i, colon).toLowerCase();
        return allowedSchemes.includes(scheme) ? cleaned : "";
    }

    // ── Style application helpers ──────────────────
    // Split the monolithic buildStyleStr into
    // focused helpers.

    /**
     * Extracts inline CSS properties from a style object.
     * Returns {parts, cls, pseudoRules}.
     */
    function buildInlineStyle(style) {
        const parts = [];
        const pseudoRules = {};
        let cls = "";

        for (const key in style) {
            if (
                key.startsWith("on_") || key === "id" ||
                key.startsWith("aria_") || key === "role"
            ) {
                continue;
            }
            if (key === "class") {
                cls = style[key] || "";
                continue;
            }
            if (
                key === "pseudo" &&
                typeof style[key] === "object" &&
                style[key] !== null
            ) {
                for (const pseudo in style[key]) {
                    const selector =
                        __gui_pseudo_map[pseudo + "_"] ||
                        (":" + pseudo);
                    if (!pseudoRules[selector]) {
                        pseudoRules[selector] = {};
                    }
                    const propsObj = style[key][pseudo];
                    for (const pp in propsObj) {
                        pseudoRules[selector][
                            toKebabCase(pp)
                        ] = propsObj[pp];
                    }
                }
                continue;
            }
            if (key === "events" || key === "aria") {
                continue;
            }
            // Match a pseudo-class prefix via
            // startsWith and Array.find instead of an index loop.
            const pfx = __gui_pseudo_prefixes.find(
                (p) => key.length > p.length && key.startsWith(p),
            );
            if (pfx) {
                const pseudo = __gui_pseudo_map[pfx];
                const cssProp = toKebabCase(key.substring(pfx.length));
                if (!pseudoRules[pseudo]) {
                    pseudoRules[pseudo] = {};
                }
                pseudoRules[pseudo][cssProp] = style[key];
                continue;
            }
            const prop = toKebabCase(key);
            if (
                window.__gui_devtools &&
                !CSS.supports(prop, "initial")
            ) {
                console.warn(
                    "[GraphicalUi] Unknown CSS property: " +
                    prop,
                );
            }
            // Apply sanitisation to inline styles.
            const sanitised = sanitizeCssValue(prop, style[key]);
            if (sanitised !== null) {
                parts.push(prop + ":" + sanitised);
            }
        }

        return { parts, cls, pseudoRules };
    }

    // ── Pseudo-class style engine (encapsulated) ──────
    // All pseudo-class interning state and the
    // post-render flush live in one closure instead of five
    // parallel module globals.
    const pseudoStyles = (() => {
        let counter = 0;
        let styleEl = null;
        const cache = {};
        const reverse = {};
        const rules = {};
        let refs = {};

        /**
         * Interns a set of pseudo-class rules, returning a
         * stable scoped class name (or "" when empty).
         */
        function intern(pseudoRules) {
            const pseudoKeys = Object.keys(pseudoRules);
            if (pseudoKeys.length === 0) {
                return "";
            }
            pseudoKeys.sort();
            let cacheKey = "";
            for (let si = 0; si < pseudoKeys.length; si++) {
                const sel = pseudoKeys[si];
                cacheKey += sel + "{";
                const props =
                    Object.keys(pseudoRules[sel]).sort();
                for (let pi2 = 0; pi2 < props.length; pi2++) {
                    cacheKey += props[pi2] + ":" +
                        pseudoRules[sel][props[pi2]] + ";";
                }
                cacheKey += "}";
            }
            let pcls = cache[cacheKey];
            if (!pcls) {
                pcls = "gui-ps-" + (++counter);
                cache[cacheKey] = pcls;
                reverse[pcls] = cacheKey;
                let css = "";
                for (
                    let si2 = 0;
                    si2 < pseudoKeys.length;
                    si2++
                ) {
                    const sel2 = pseudoKeys[si2];
                    if (!isSafePseudoSelector(sel2)) {
                        continue;
                    }
                    css += "." + pcls + sel2 + "{";
                    for (const p in pseudoRules[sel2]) {
                        if (!isSafeCssName(p)) {
                            continue;
                        }
                        const sv = sanitizeCssValue(
                            p, pseudoRules[sel2][p],
                        );
                        if (sv !== null) {
                            css += p + ":" + sv + ";";
                        }
                    }
                    css += "}";
                }
                rules[pcls] = css;
            }
            refs[pcls] = (refs[pcls] || 0) + 1;
            return pcls;
        }

        /** Resets reference counts at the start of a render pass. */
        function beginPass() {
            refs = {};
        }

        /** Garbage-collects unreferenced classes and syncs the <style> element. */
        function flush() {
            for (const cls in rules) {
                if (!refs[cls]) {
                    const rkey = reverse[cls];
                    if (rkey) {
                        delete cache[rkey];
                    }
                    delete reverse[cls];
                    delete rules[cls];
                }
            }
            let allCss = "";
            for (const cls in rules) {
                allCss += rules[cls];
            }
            if (allCss) {
                if (!styleEl) {
                    styleEl =
                        document.createElement("style");
                    document.head.appendChild(styleEl);
                }
                styleEl.textContent = allCss;
            } else if (styleEl) {
                styleEl.remove();
                styleEl = null;
            }
        }

        return { intern, beginPass, flush };
    })();

    /**
     * Builds a style string and CSS class from a widget style
     * object. Returns {style, cls}.
     */
    function buildStyleStr(style) {
        if (!style) {
            return { style: "", cls: "" };
        }
        const { parts, cls, pseudoRules } =
            buildInlineStyle(style);
        const pseudoCls = pseudoStyles.intern(pseudoRules);
        const finalCls = pseudoCls
            ? (cls ? cls + " " + pseudoCls : pseudoCls)
            : cls;
        return { style: parts.join(";"), cls: finalCls };
    }

    function mergeClass(base, extraCls) {
        if (extraCls) {
            return base + " " + extraCls;
        }
        return base;
    }

    // ── Event helpers ────────────────────────────
    // Data-driven factories for widget events whose payload is derived from the
    // DOM event.  Each spec maps an emit `type` to an optional value extractor;
    // makeWidgetHandler turns a spec into a `(cbId) => (e) => …` factory, so
    // adding a widget event is a single map entry.
    const WIDGET_EVENT_SPECS = {
        click: { type: "click" },
        change: { type: "change", value: (e) => e.target.value },
        toggle: { type: "toggle", value: (e) => e.target.checked },
        slide: { type: "slide", value: (e) => Number(e.target.value) },
        dropdown: { type: "select", value: (e) => e.target.value },
    };

    function makeWidgetHandler(kind) {
        const spec = WIDGET_EVENT_SPECS[kind];
        return (cbId) => (e) => {
            const payload = { type: spec.type, id: cbId };
            if (spec.value) {
                payload.value = spec.value(e);
            }
            emit(payload);
        };
    }

    const makeClickHandler = makeWidgetHandler("click");
    const makeChangeHandler = makeWidgetHandler("change");
    const makeToggleHandler = makeWidgetHandler("toggle");
    const makeSlideHandler = makeWidgetHandler("slide");
    const makeDropdownHandler = makeWidgetHandler("dropdown");

    // Kept separate: the select payload value comes from a bound closure
    // argument (the chosen option), not from the DOM event.
    function makeSelectHandler(cbId, value) {
        return () => {
            emit({ type: "select", id: cbId, value: value });
        };
    }
    function makeWidgetEventHandler(cbId, evtType) {
        return (e) => {
            if (evtType === "contextmenu") {
                e.preventDefault();
            }
            emit({
                type: "widget_event",
                id: cbId,
                event_type: evtType,
                x: e.offsetX || 0,
                y: e.offsetY || 0,
                button: ["left", "middle", "right"][e.button]
                    || "left",
                ctrl: e.ctrlKey,
                shift: e.shiftKey,
                alt: e.altKey,
            });
        };
    }

    // Extracted reusable event handler factories
    // for file_input, date_picker, time_picker, color_picker.
    function makeCallbackResultHandler(cbId) {
        return (e) => {
            emit({
                type: "callback_result",
                id: cbId,
                value: e.target.value,
            });
        };
    }

    function makeFileInputHandler(cbId) {
        return (e) => {
            const files = Array.from(
                e.target.files || [],
            ).map((f) => f.name);
            emit({
                type: "callback_result",
                id: cbId,
                value: files.join(","),
            });
        };
    }

    // Commit handlers for text_input / text_area on_commit: fire the committed
    // value once the edit is "done" rather than on every keystroke. The change
    // variant runs on the native change event (blur, or Enter on a text input);
    // the key variant handles Enter. Both reuse the "change" event type, so the
    // host routes the value through the widget's on_commit callback id.
    function makeCommitChangeHandler(cbId) {
        return (e) => {
            emit({ type: "change", id: cbId, value: e.target.value });
        };
    }

    function makeCommitKeyHandler(cbId, requireModifier) {
        return (e) => {
            if (e.key !== "Enter") {
                return;
            }
            if (requireModifier) {
                // text_area: only Ctrl/Cmd+Enter commits, so a plain Enter still
                // inserts a newline. That chord does not blur the field and so
                // fires no native change event — emit directly (and suppress the
                // newline the chord would otherwise add).
                if (!(e.ctrlKey || e.metaKey)) {
                    return;
                }
                e.preventDefault();
                emit({ type: "change", id: cbId, value: e.target.value });
                return;
            }
            // text_input: plain Enter commits. A text input already fires a
            // native `change` on Enter (as well as on blur), and that change is
            // bound to the commit handler above, so emitting here too would
            // double-fire the commit. Instead just blur: the single native
            // change then delivers exactly one commit — and because blur fires
            // `change` uniformly across WebView2/WebKit backends, Enter commits
            // reliably everywhere without depending on engine-specific
            // Enter-fires-change behaviour.
            e.target.blur();
        };
    }

    // Bind an event handler only when a callback id is present, otherwise leave
    // the binding undefined (no listener). Makes the pervasive
    // `id ? makeX(id) : undefined` guard at every event binding explicit.
    const boundHandler = (id, factory) => (id ? factory(id) : undefined);

    // ── ARIA / event attribute helpers ──────────────
    const __gui_default_roles = {
        "button": "button",
        "text_input": "textbox",
        "text_area": "textbox",
        "checkbox": "checkbox",
        "slider": "slider",
        "dropdown": "listbox",
        "radio_group": "radiogroup",
        "toggle": "switch",
        "tabs": "tablist",
        "list": "list",
        "table": "table",
        "dialog": "dialog",
        "alert": "alert",
        "toolbar": "toolbar",
        "progress": "progressbar",
        "link": "link",
        "navigation_link": "link",
        "image": "img",
        "separator": "separator",
    };

    function buildAriaAttrs(w) {
        const attrs = {};
        for (const k in w) {
            if (
                k.length > 6 &&
                k.startsWith("_aria_")
            ) {
                attrs[toKebabCase(k.substring(1))] =
                    String(w[k]);
            }
        }
        if (w._role) {
            attrs["role"] = w._role;
        }
        if (!attrs["role"] && __gui_default_roles[w.type]) {
            attrs["role"] = __gui_default_roles[w.type];
        }
        if (w._element_id) {
            attrs["id"] = w._element_id;
        }
        return attrs;
    }

    // Extracted event handler builder from
    // renderWidget into a reusable helper.
    const __gui_evt_map = {
        "_on_click_id": "click",
        "_on_double_click_id": "dblclick",
        "_on_right_click_id": "contextmenu",
        "_on_mouse_enter_id": "mouseenter",
        "_on_mouse_leave_id": "mouseleave",
        "_on_mouse_move_id": "mousemove",
    };

    function buildEventHandlers(w) {
        const evtHandlers = {};
        for (const prop in __gui_evt_map) {
            if (w[prop]) {
                evtHandlers[__gui_evt_map[prop]] =
                    makeWidgetEventHandler(
                        w[prop], __gui_evt_map[prop],
                    );
            }
        }
        return evtHandlers;
    }

    // ── Lucide icon renderer (lit-html SVG templates) ──
    function svgChild(c) {
        const tag = c[0];
        const a = c[1] || {};
        switch (tag) {
            case "path":
                return svg`<path d=${a.d || ""} />`;
            case "circle":
                return svg`<circle cx=${a.cx || 0} cy=${a.cy || 0} r=${a.r || 0} />`;
            case "line":
                return svg`<line x1=${a.x1 || 0} y1=${a.y1 || 0} x2=${a.x2 || 0} y2=${a.y2 || 0} />`;
            case "polyline":
                return svg`<polyline points=${a.points || ""} />`;
            case "rect":
                return svg`<rect x=${a.x || 0} y=${a.y || 0} width=${a.width || 0} height=${a.height || 0} rx=${a.rx || nothing} ry=${a.ry || nothing} />`;
            case "ellipse":
                return svg`<ellipse cx=${a.cx || 0} cy=${a.cy || 0} rx=${a.rx || 0} ry=${a.ry || 0} />`;
            default:
                return nothing;
        }
    }

    function mkIcon(name, size) {
        const d = (typeof __lucide_icons !== "undefined")
            ? __lucide_icons[name]
            : null;
        if (!d) {
            return null;
        }
        const sz = size || 24;
        return svg`<svg xmlns="http://www.w3.org/2000/svg" width=${sz} height=${sz}
            viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" class="gui-icon"
            >${d.map(svgChild)}</svg>`;
    }

    // ── Severity → icon mapping for alerts / toasts ────
    // Pairs each severity with a distinct icon so meaning is never carried by
    // colour alone (WCAG 1.4.1).
    const __gui_severity_icons = {
        info: "info",
        success: "check-circle",
        warning: "alert-triangle",
        error: "alert-circle",
    };

    function severityIcon(severity) {
        const name = __gui_severity_icons[severity]
            || __gui_severity_icons.info;
        return mkIcon(name, 18);
    }

    // ── Focus helpers (shared by dialog and popover) ───
    const FOCUSABLE_SEL =
        "a[href],button:not([disabled]),input:not([disabled])," +
        "select:not([disabled]),textarea:not([disabled])," +
        '[tabindex]:not([tabindex="-1"])';

    function getFocusable(container) {
        return Array.from(
            container.querySelectorAll(FOCUSABLE_SEL),
        ).filter((el) =>
            el.offsetWidth > 0 || el.offsetHeight > 0 ||
            el === document.activeElement
        );
    }

    // ── Popup disclosure (menu / popover / combobox) ───
    // Open state lives in the DOM (data-open) because it is ephemeral UI
    // state that must survive model-driven re-renders. The trigger carrying
    // aria-haspopup also reflects aria-expanded.
    function setPopupOpen(root, open) {
        if (!root) {
            return;
        }
        root.setAttribute("data-open", open ? "true" : "false");
        const trigger = root.querySelector("[aria-haspopup]");
        if (trigger) {
            trigger.setAttribute(
                "aria-expanded", open ? "true" : "false",
            );
        }
    }

    function closeAllPopups(except) {
        document
            .querySelectorAll('[data-gui-popup][data-open="true"]')
            .forEach((el) => {
                if (el !== except) {
                    setPopupOpen(el, false);
                }
            });
    }

    // Close popups on outside click (capture phase, before widget handlers)
    // and on Escape (restoring focus to the trigger).
    document.addEventListener("click", (e) => {
        const inside = (e.target && e.target.closest)
            ? e.target.closest("[data-gui-popup]")
            : null;
        closeAllPopups(inside);
    }, true);

    document.addEventListener("keydown", (e) => {
        if (e.key !== "Escape") {
            return;
        }
        const open = document.querySelector(
            '[data-gui-popup][data-open="true"]',
        );
        if (open) {
            const trigger = open.querySelector("[aria-haspopup]");
            setPopupOpen(open, false);
            if (trigger && trigger.focus) {
                trigger.focus();
            }
        }
    });

    // ── Dialog focus management (true modal a11y) ──────
    // Tracks open/close transitions to move focus into the dialog, trap Tab
    // within it, close on Escape, and restore focus to the prior element.
    let __gui_dialog_open = false;
    let __gui_dialog_restore = null;

    function syncDialogFocus(root) {
        const dialog = root.querySelector(".gui-dialog");
        if (dialog && !__gui_dialog_open) {
            __gui_dialog_open = true;
            __gui_dialog_restore =
                (document.activeElement instanceof HTMLElement)
                    ? document.activeElement
                    : null;
            const focusables = getFocusable(dialog);
            (focusables[0] || dialog).focus();
        } else if (!dialog && __gui_dialog_open) {
            __gui_dialog_open = false;
            if (
                __gui_dialog_restore &&
                __gui_dialog_restore.isConnected
            ) {
                __gui_dialog_restore.focus();
            }
            __gui_dialog_restore = null;
        }
    }

    document.addEventListener("keydown", (e) => {
        if (!__gui_dialog_open) {
            return;
        }
        const root =
            document.getElementById("gui-root") || document.body;
        const dialog = root.querySelector(".gui-dialog");
        if (!dialog) {
            return;
        }
        if (e.key === "Escape") {
            const closeId = dialog.getAttribute("data-close-id");
            if (closeId) {
                e.preventDefault();
                emit({ type: "click", id: closeId });
            }
            return;
        }
        if (e.key === "Tab") {
            const focusables = getFocusable(dialog);
            if (focusables.length === 0) {
                e.preventDefault();
                dialog.focus();
                return;
            }
            const first = focusables[0];
            const last = focusables[focusables.length - 1];
            const active = document.activeElement;
            if (e.shiftKey && (active === first || active === dialog)) {
                e.preventDefault();
                last.focus();
            } else if (!e.shiftKey && active === last) {
                e.preventDefault();
                first.focus();
            }
        }
    }, true);

    // ── ID counters ─────────────────────────────
    // Reset per render pass to avoid unbounded
    // growth in long-running sessions.
    let __gui_chart_id = 0;
    let __gui_anim_id = 0;
    let __gui_vlist_id = 0;
    let __gui_acc_id = 0;
    let __gui_attr_id = 0;
    let __gui_dialog_seq = 0;
    let __gui_popup_seq = 0;

    // ── Generic container helper ──────────
    function renderContainer(w, baseCls, style, extraCls, aria) {
        const containerStyle = composeStyle(
            style,
            w.gap ? "gap:" + w.gap + "px" : "",
            w.align ? "align-items:" + w.align : "",
        );
        return html`<div class=${mergeClass(baseCls, extraCls)} style=${containerStyle}
            id=${aria.id || nothing} role=${aria.role || nothing}
            >${(w.children || []).map(renderWidget)}</div>`;
    }

    // ── Chart rendering ────────────────────────
    // Collect chart render requests during the
    // render pass and flush them after render completes,
    // instead of querying the DOM via querySelector per chart.
    let __gui_pending_charts = [];

    // Collect accessible widget ARIA attribute
    // requests and apply them after render completes.
    let __gui_pending_accessible = [];

    // ARIA attributes set on ANY widget via its style dictionary's `aria_*`
    // keys (serialised to `_aria_*` by the C++ side and collected in
    // buildAriaAttrs) cannot be bound through lit-html's static templates, so
    // they are applied here after render. Located by element id — the widget
    // gets a synthetic id when the author did not supply one.
    let __gui_pending_attrs = [];

    // Virtual lists populate their viewport after render (see flushPendingVlists)
    // so the scroll handler is the sole owner of that node's children.
    let __gui_pending_vlists = [];

    function renderChart(w, style) {
        const chartId = "__chart_" + (++__gui_chart_id);
        __gui_pending_charts.push({ chartId, w });
        return html`<div class="gui-chart-container" data-chart-id=${chartId} style=${style}></div>`;
    }

    function flushPendingCharts() {
        for (const { chartId, w } of __gui_pending_charts) {
            const el = document.querySelector(
                '[data-chart-id="' + chartId + '"]',
            );
            if (
                el && el.isConnected &&
                typeof window.__gui_render_chart === "function"
            ) {
                window.__gui_render_chart(el, w);
            }
        }
        __gui_pending_charts = [];
    }

    function flushPendingAccessible() {
        for (const { accId, aAttrs } of __gui_pending_accessible) {
            const el = document.querySelector(
                '[data-acc-id="' + accId + '"]',
            );
            if (!el || !el.isConnected) {
                continue;
            }
            for (const ak in aAttrs) {
                if (ak === "role") {
                    el.setAttribute("role", aAttrs[ak]);
                } else {
                    el.setAttribute(
                        "aria-" + toKebabCase(ak),
                        String(aAttrs[ak]),
                    );
                }
            }
            el.removeAttribute("data-acc-id");
        }
        __gui_pending_accessible = [];
    }

    // Apply per-widget ARIA attributes (aria-label, aria-describedby, …) that
    // were supplied through a widget's style dictionary. The attribute names are
    // already kebab-cased by buildAriaAttrs; locate by id (getElementById needs
    // no selector escaping, so author-supplied ids are safe).
    function flushPendingAttrs() {
        for (const { id, attrs } of __gui_pending_attrs) {
            const el = document.getElementById(id);
            if (!el || !el.isConnected) {
                continue;
            }
            for (const name in attrs) {
                el.setAttribute(name, attrs[name]);
            }
        }
        __gui_pending_attrs = [];
    }

    // Render only the visible window of a virtual list into its viewport. Reads
    // live data from container.__vlMeta so it stays correct across model updates
    // even though the scroll listener is bound only once.
    function renderVlistWindow(container) {
        const meta = container.__vlMeta;
        if (!meta) {
            return;
        }
        const viewport = container.querySelector(".gui-vlist-viewport");
        if (!viewport) {
            return;
        }
        const startIdx = Math.min(
            Math.floor(container.scrollTop / meta.itemH),
            Math.max(0, meta.items.length - meta.visible),
        );
        const endIdx = Math.min(
            startIdx + meta.visible + 2, meta.items.length,
        );
        viewport.style.transform =
            "translateY(" + (startIdx * meta.itemH) + "px)";
        render(html`${meta.items.slice(startIdx, endIdx).map((item) => {
            return html`<div class="gui-vlist-item" style=${"height:" + meta.itemH + "px;box-sizing:border-box"}
                role="listitem"
                >${typeof item === "object" && item.type ? renderWidget(item) : String(item)}</div>`;
        })}`, viewport);
    }

    function flushPendingVlists() {
        let guard = 0;
        // A virtual-list item may itself be a virtual list, so draining one
        // batch can enqueue more; loop until none remain (the guard caps
        // pathological self-nesting).
        while (__gui_pending_vlists.length > 0 && guard < 64) {
            guard += 1;
            const batch = __gui_pending_vlists;
            __gui_pending_vlists = [];
            for (const { vlId, items, itemH, visible } of batch) {
                const container = document.querySelector(
                    '[data-vlist-id="' + vlId + '"]',
                );
                if (!container || !container.isConnected) {
                    continue;
                }
                // Refresh live metadata every render so the once-bound scroll
                // handler always windows over the current data.
                container.__vlMeta = { items, itemH, visible };
                renderVlistWindow(container);
                if (!container.__vlScrollBound) {
                    container.__vlScrollBound = true;
                    let ticking = false;
                    container.addEventListener("scroll", () => {
                        if (ticking) {
                            return;
                        }
                        ticking = true;
                        requestAnimationFrame(() => {
                            ticking = false;
                            // Re-window this list, then flush deferred work its
                            // newly-visible items enqueued (charts, ARIA, nested
                            // lists) — otherwise they would never be applied.
                            renderVlistWindow(container);
                            flushDeferred();
                        });
                    });
                }
            }
        }
    }

    // Apply post-render deferred work in dependency order: virtual-list windows
    // first (rendering their items enqueues charts, accessible wrappers, ARIA,
    // and possibly nested lists), then those queues.
    function flushDeferred() {
        flushPendingVlists();
        flushPendingCharts();
        flushPendingAccessible();
        flushPendingAttrs();
    }

    // ── Accessibility audit (dev-mode only) ─────────────────────
    // Warn when an interactive control renders with no discernible
    // accessible name — the icon-only or glyph-only button a screen
    // reader would announce as nothing. This is the accessibility
    // sibling of the theme-contrast check: it is gated on
    // window.__gui_devtools and surfaced through console.warn, so
    // release builds pay nothing and no runtime behaviour changes.
    // Each element is warned at most once so re-renders never flood
    // the console.
    const __gui_a11y_warned = new WeakSet();

    // True when `el` carries a name a screen reader can announce: an
    // explicit ARIA/title name, visible text that is more than a bare
    // symbol glyph, or a labelled image/icon descendant. Depends only
    // on getAttribute/textContent/querySelectorAll so it stays unit
    // testable outside a real DOM.
    function __gui_has_accessible_name(el) {
        const label = el.getAttribute("aria-label");
        if (label && label.trim() !== "") {
            return true;
        }
        const labelledBy = el.getAttribute("aria-labelledby");
        if (labelledBy && typeof document !== "undefined" &&
            typeof document.getElementById === "function") {
            const labelledByNamed = labelledBy.trim().split(/\s+/).some((id) => {
                const ref = id && document.getElementById(id);
                return ref && (ref.textContent || "").trim() !== "";
            });
            if (labelledByNamed) {
                return true;
            }
        }
        const title = el.getAttribute("title");
        if (title && title.trim() !== "") {
            return true;
        }
        // Visible text is a name unless it is only one or two
        // punctuation/symbol characters ("+", "×", "⋮", "☰", "🔍"),
        // which cannot be announced as a meaningful label — exactly the
        // icon-substitute glyphs that need an explicit aria_label.
        const text = (el.textContent || "").trim();
        if (text !== "" && !/^[\p{P}\p{S}]{1,2}$/u.test(text)) {
            return true;
        }
        // A labelled <img>/<svg> descendant supplies the name for an
        // otherwise text-less icon button.
        const descendants = el.querySelectorAll
            ? Array.from(el.querySelectorAll("img,svg"))
            : [];
        return descendants.some((node) => {
            const alt = node.getAttribute("alt");
            if (alt && alt.trim() !== "") {
                return true;
            }
            const aria = node.getAttribute("aria-label");
            if (aria && aria.trim() !== "") {
                return true;
            }
            const titleEl = node.querySelector &&
                node.querySelector("title");
            return !!(titleEl && (titleEl.textContent || "").trim() !== "");
        });
    }

    // Scan the just-rendered tree for interactive controls that lack an
    // accessible name and warn once per offending element.
    function __gui_audit_accessible_names(root) {
        if (!window.__gui_devtools || !root || !root.querySelectorAll) {
            return;
        }
        root.querySelectorAll(
            'button,[role="button"],a[href],[role="link"]',
        ).forEach((el) => {
            if (__gui_a11y_warned.has(el) || __gui_has_accessible_name(el)) {
                return;
            }
            __gui_a11y_warned.add(el);
            console.warn(
                "[GraphicalUi] Interactive control has no accessible name; " +
                "screen readers will skip it. Give it visible text or an " +
                '`aria_label` style key, e.g. ' +
                'button(icon("plus"), on_click, { "aria_label": "Add" }).',
                el,
            );
        });
    }

    // ── Widget renderer factories ───────────────
    // Fold near-identical renderers behind shared templates, mirroring the C++
    // register_simple_widget pattern. Each table entry stays a thin delegating
    // arrow so intent (and renderer coverage) is explicit at the call site.

    // Toggle and switch share one on/off track; only the base class differs
    // (switch adds gui-switch to expose the separate catalog widget).
    function toggleLike(w, style, extraCls, aria, baseCls) {
        const change = boundHandler(w._callback_id, makeToggleHandler);
        return html`<label class=${mergeClass(baseCls, extraCls)} style=${style}
            id=${aria.id || nothing}>
            <input type="checkbox" class="gui-toggle-input" role="switch"
                .checked=${!!w.checked}
                aria-checked=${w.checked ? "true" : "false"}
                @change=${change}>
            <div class="gui-toggle-track" data-on=${!!w.checked} aria-hidden="true">
                <div class="gui-toggle-thumb"></div>
            </div>
            <span>${w.label || ""}</span>
        </label>`;
    }

    // Single-child pass-through <div> wrapper. Renders w.child unchanged inside a
    // class-tagged div. debug/inspect use a base class; the aria wrappers
    // (aria_live/aria_describedby) instead lead with one ARIA attribute and carry
    // only the caller's class. Absent attributes render as `nothing`, so one
    // template stays byte-for-byte for every variant.
    function childWrapper(w, style, extraCls, aria, baseCls, opts) {
        const o = opts || {};
        const cls = baseCls ? mergeClass(baseCls, extraCls) : (extraCls || nothing);
        return html`<div aria-live=${o.ariaLive ? (w.level || "polite") : nothing}
            aria-describedby=${o.ariaDescribedby ? (w.desc_id || nothing) : nothing}
            class=${cls} style=${style}
            id=${aria.id || nothing}
            >${w.child ? renderWidget(w.child) : nothing}</div>`;
    }

    // Shared modal scaffolding for the dialog and confirm renderers: emits the
    // .gui-dialog-overlay (with backdrop-dismiss when closeId is set) wrapping
    // the .gui-dialog shell the focus trap locates by class, plus its
    // tabindex="-1" / aria-modal / data-close-id contract. Callers pass only
    // their body and the few per-widget attributes that differ (widget class,
    // role, labelled/described ids). This markup must stay byte-compatible with
    // syncDialogFocus and the keydown trap above.
    function dialogShell(opts) {
        const dialogStyle = composeStyle("position:relative", opts.style);
        const closeId = opts.closeId;
        const onBackdrop = closeId
            ? (e) => {
                if (e.target === e.currentTarget) {
                    makeClickHandler(closeId)();
                }
            }
            : undefined;
        return html`<div class=${mergeClass("gui-dialog-overlay", opts.extraCls)}
            @click=${onBackdrop}>
            <div class=${opts.widgetCls} style=${dialogStyle}
                id=${opts.id || nothing} role=${opts.role}
                aria-modal="true" tabindex="-1"
                aria-labelledby=${opts.labelledById || nothing}
                aria-describedby=${opts.describedById || nothing}
                data-close-id=${closeId || nothing}>
                ${opts.body}
            </div>
        </div>`;
    }

    // Table-driven <input> renderer shared by the date/time/color pickers, the
    // number input, and the file input. They share the type/class/style/id shape
    // and differ only by input type, class, value/handler, and a couple of
    // optional attributes (min/max for numeric, accept for file) — all described
    // by the SIMPLE_INPUTS config, mirroring the C++ register_simple_widget table.
    function simpleInput(w, style, extraCls, aria, cfg) {
        const handler = boundHandler(w._callback_id, cfg.handler);

        // A file input's value is read-only in the browser, so it binds `accept`
        // instead of `.value`; keep it on its own template to stay byte-for-byte.
        if (cfg.file) {
            return html`<input type=${cfg.type} class=${mergeClass(cfg.cls, extraCls)} style=${style}
                id=${aria.id || nothing}
                accept=${w.accept || nothing}
                @change=${handler}
                >`;
        }

        return html`<input type=${cfg.type} class=${mergeClass(cfg.cls, extraCls)} style=${style}
            id=${aria.id || nothing}
            min=${cfg.numeric ? (w.min != null ? w.min : nothing) : nothing}
            max=${cfg.numeric ? (w.max != null ? w.max : nothing) : nothing}
            .value=${cfg.value(w)}
            @change=${cfg.event === "change" ? handler : undefined}
            @input=${cfg.event === "input" ? handler : undefined}
            >`;
    }

    const SIMPLE_INPUTS = {
        date_picker: { type: "date", cls: "gui-date-picker", event: "change",
            handler: makeCallbackResultHandler, value: (w) => w.value || "" },
        time_picker: { type: "time", cls: "gui-time-picker", event: "change",
            handler: makeCallbackResultHandler, value: (w) => w.value || "" },
        color_picker: { type: "color", cls: "gui-color-picker", event: "input",
            handler: makeCallbackResultHandler, value: (w) => w.value || "#000000" },
        number_input: { type: "number", cls: "gui-number-input", event: "input",
            handler: makeSlideHandler, numeric: true,
            value: (w) => String(w.value != null ? w.value : "") },
        file_input: { type: "file", cls: "gui-file-input", file: true,
            handler: makeFileInputHandler },
    };

    // ── Disclosure controllers (menu / popover / combobox) ──
    // Keyboard and focus controllers for the disclosure widgets, lifted out of
    // the render entries so each renderer only builds its template. Built on the
    // shared setPopupOpen / getFocusable / FOCUSABLE_SEL primitives.

    // Next roving-focus index for an arrow / Home / End keydown, or -1 when the
    // key is not a navigation key for the given orientation. `orientation`
    // selects which arrow keys advance focus: "vertical" (ArrowUp/Down, the
    // default, used by the menu), "horizontal" (ArrowLeft/Right), or "both"
    // (all four, used by the tab bar). Home jumps to the first item, End to the
    // last, regardless of orientation.
    function rovingFocusIndex(key, currentIdx, count, orientation) {
        if (count === 0) {
            return -1;
        }
        if (key === "Home") {
            return 0;
        }
        if (key === "End") {
            return count - 1;
        }
        const mode = orientation || "vertical";
        const vertical = mode === "vertical" || mode === "both";
        const horizontal = mode === "horizontal" || mode === "both";
        const forward =
            (vertical && key === "ArrowDown") ||
            (horizontal && key === "ArrowRight");
        const backward =
            (vertical && key === "ArrowUp") ||
            (horizontal && key === "ArrowLeft");
        if (forward) {
            return (currentIdx + 1) % count;
        }
        if (backward) {
            return (currentIdx - 1 + count) % count;
        }
        return -1;
    }

    // Menu -------------------------------------------------
    function menuFocusFirstItem(root) {
        const first = root.querySelector(".gui-menu-item");
        if (first) {
            requestAnimationFrame(() => first.focus());
        }
    }

    function onMenuTriggerClick(e) {
        const root = e.currentTarget.closest(".gui-menu");
        const open = root.getAttribute("data-open") === "true";
        setPopupOpen(root, !open);
        if (!open) {
            menuFocusFirstItem(root);
        }
    }

    function onMenuTriggerKeydown(e) {
        if (e.key === "ArrowDown" || e.key === "Enter" || e.key === " ") {
            e.preventDefault();
            const root = e.currentTarget.closest(".gui-menu");
            setPopupOpen(root, true);
            menuFocusFirstItem(root);
        }
    }

    function onMenuKeydown(e) {
        const root = e.currentTarget.closest(".gui-menu");
        const itemEls = Array.from(root.querySelectorAll(".gui-menu-item"));
        const idx = itemEls.indexOf(document.activeElement);
        const next = rovingFocusIndex(e.key, idx, itemEls.length);
        if (next >= 0) {
            e.preventDefault();
            itemEls[next].focus();
            return;
        }
        if (e.key === "Escape" || e.key === "Tab") {
            setPopupOpen(root, false);
            const trigger = root.querySelector(".gui-menu-trigger");
            if (e.key === "Escape" && trigger) {
                e.preventDefault();
                trigger.focus();
            }
        }
    }

    function makeMenuItemClick(selectId, item) {
        return (e) => {
            const root = e.currentTarget.closest(".gui-menu");
            if (selectId) {
                emit({ type: "select", id: selectId, value: item });
            }
            setPopupOpen(root, false);
            const trigger = root.querySelector(".gui-menu-trigger");
            if (trigger) {
                trigger.focus();
            }
        };
    }

    // Popover ----------------------------------------------
    function onPopoverTriggerClick(e) {
        const root = e.currentTarget.closest(".gui-popover");
        const open = root.getAttribute("data-open") === "true";
        setPopupOpen(root, !open);
        if (!open) {
            const panel = root.querySelector(".gui-popover-panel");
            const focusable = panel ? panel.querySelector(FOCUSABLE_SEL) : null;
            if (focusable) {
                requestAnimationFrame(() => focusable.focus());
            }
        }
    }

    // Combobox ---------------------------------------------
    // Builds the event handlers for one combobox instance; they close over the
    // widget's callback ids and option list.
    function makeComboboxController(w) {
        const options = w.options || [];
        const changeId = w._callback_id;
        const selectId = w._select_id;
        const rootOf = (el) => el.closest(".gui-combobox");
        const setActive = (root, idx) => {
            const opts = Array.from(
                root.querySelectorAll(".gui-combobox-option"),
            );
            if (opts.length === 0) {
                return;
            }
            const n = ((idx % opts.length) + opts.length) %
                opts.length;
            opts.forEach((o, i) =>
                o.setAttribute(
                    "data-active", i === n ? "true" : "false",
                ));
            root.setAttribute("data-active-index", String(n));
            const input =
                root.querySelector(".gui-combobox-input");
            if (input && opts[n].id) {
                input.setAttribute(
                    "aria-activedescendant", opts[n].id,
                );
            }
            opts[n].scrollIntoView({ block: "nearest" });
        };
        const selectValue = (root, value) => {
            if (selectId) {
                emit({ type: "select", id: selectId, value: value });
            } else if (changeId) {
                emit({ type: "change", id: changeId, value: value });
            }
            setPopupOpen(root, false);
        };
        const onInput = (e) => {
            const root = rootOf(e.target);
            setPopupOpen(root, true);
            root.setAttribute("data-active-index", "-1");
            e.target.removeAttribute("aria-activedescendant");
            if (changeId) {
                emit({
                    type: "change",
                    id: changeId,
                    value: e.target.value,
                });
            }
        };
        const onFocus = (e) => {
            if (options.length) {
                setPopupOpen(rootOf(e.target), true);
            }
        };
        const onKeydown = (e) => {
            const root = rootOf(e.target);
            const idx = parseInt(
                root.getAttribute("data-active-index") || "-1", 10,
            );
            if (e.key === "ArrowDown") {
                e.preventDefault();
                setPopupOpen(root, true);
                setActive(root, idx + 1);
            } else if (e.key === "ArrowUp") {
                e.preventDefault();
                setPopupOpen(root, true);
                setActive(root, idx - 1);
            } else if (e.key === "Enter") {
                const opts = root.querySelectorAll(
                    ".gui-combobox-option",
                );
                if (idx >= 0 && opts[idx]) {
                    e.preventDefault();
                    selectValue(
                        root, opts[idx].getAttribute("data-value"),
                    );
                }
            } else if (e.key === "Escape") {
                setPopupOpen(root, false);
            }
        };
        const onOptionMousedown = (value) => (e) => {
            e.preventDefault();
            selectValue(rootOf(e.target), value);
        };
        return { onInput, onFocus, onKeydown, onOptionMousedown };
    }

    // ── Widget renderer lookup table ───────
    // Each entry maps a widget type to a render function
    // taking (w, style, extraCls, aria, evtHandlers).
    const WIDGET_RENDERERS = {};

    // __GUI_WIDGET_RENDERER_FRAGMENTS__
    // Populated by external/gui-framework/renderers/{basic,layout,advanced,
    // interaction}.js, spliced in here so the module-private helpers above stay
    // in scope. Mirrors the C++ graphicalui_widgets_*.cpp category split.

    // Register chart types — all delegate to renderChart.
    const CHART_TYPES = [
        "vertical_bar_chart", "horizontal_bar_chart",
        "line_chart", "area_chart", "pie_chart",
        "donut_chart", "scatter_plot",
    ];
    for (const ct of CHART_TYPES) {
        WIDGET_RENDERERS[ct] = (w, style) => {
            return renderChart(w, style);
        };
    }

    // ── Widget renderer ────────────────────────
    // Simplified to a lookup table dispatch.
    function renderWidget(w) {
        if (!w || typeof w !== "object") {
            return html`${String(w || "")}`;
        }

        const type = w.type || "label";
        const ss = buildStyleStr(w.style);
        const style = ss.style;
        const extraCls = ss.cls;
        const aria = buildAriaAttrs(w);
        const evtHandlers = buildEventHandlers(w);

        // Author ARIA attributes (aria-label etc., from `aria_*` style keys)
        // beyond id/role cannot be bound in the static templates; register them
        // for post-render application, giving the element a synthetic id to
        // locate it when the author supplied none.
        let ariaExtra = null;
        for (const k in aria) {
            if (k !== "id" && k !== "role") {
                (ariaExtra || (ariaExtra = {}))[k] = aria[k];
            }
        }
        if (ariaExtra) {
            if (!aria.id) {
                aria.id = "__gui_attr_" + (++__gui_attr_id);
            }
            __gui_pending_attrs.push({ id: aria.id, attrs: ariaExtra });
        }

        const renderer = WIDGET_RENDERERS[type];
        if (renderer) {
            return renderer(w, style, extraCls, aria, evtHandlers);
        }
        return html`<span>[unknown widget: ${type}]</span>`;
    }

    // ── Main render entry point ────────────────
    // Render de-duplication lives in the C++ host (graphicalui_events.cpp),
    // which already holds the serialized tree JSON and skips webview_eval on
    // unchanged frames — so no per-frame JSON.stringify / change-detection is
    // needed here.  __gui_pending_render only coalesces bursts within a frame.
    let __gui_pending_render = null;

    // Shared render routine used by both the
    // synchronous first render and the coalesced path.
    // ── Error toast (last-good-frame recovery) ──────────
    // A dismissible banner surfaced when a render/update error occurs. It is an
    // overlay appended to <body>, so the last successfully rendered frame stays
    // visible in gui-root underneath instead of the window blanking out.
    let __gui_error_toast_el = null;

    function __gui_clear_error_toast() {
        if (__gui_error_toast_el && __gui_error_toast_el.parentNode) {
            __gui_error_toast_el.parentNode.removeChild(__gui_error_toast_el);
        }
        __gui_error_toast_el = null;
    }

    window.__gui_clear_error_toast = __gui_clear_error_toast;

    /**
     * Shows (or updates) a dismissible error toast overlay without touching
     * gui-root. Message text is applied via textContent, so it cannot inject
     * markup.
     */
    window.__gui_error_toast = (message) => {
        try {
            if (!__gui_error_toast_el) {
                const el = document.createElement("div");
                el.id = "gui-error-toast";
                el.setAttribute("role", "alert");
                el.setAttribute("aria-live", "assertive");
                el.style.cssText = [
                    "position:fixed",
                    "left:16px",
                    "right:16px",
                    "bottom:16px",
                    "z-index:2147483647",
                    "display:flex",
                    "align-items:flex-start",
                    "gap:12px",
                    "padding:12px 16px",
                    "background:var(--gui-error, #dc3545)",
                    "color:#fff",
                    "font-family:var(--gui-font, system-ui, sans-serif)",
                    "font-size:14px",
                    "line-height:1.4",
                    "border-radius:var(--gui-radius, 6px)",
                    "box-shadow:0 4px 12px rgba(0,0,0,0.3)",
                    "max-height:40vh",
                    "overflow:auto",
                ].join(";");

                const text = document.createElement("pre");
                text.style.cssText =
                    "margin:0;flex:1;white-space:pre-wrap;word-break:break-word;font-family:inherit";
                el.appendChild(text);

                const close = document.createElement("button");
                close.setAttribute("aria-label", "Dismiss error");
                close.textContent = "\u00d7";
                close.style.cssText = [
                    "flex:none",
                    "border:none",
                    "background:transparent",
                    "color:#fff",
                    "font-size:20px",
                    "line-height:1",
                    "cursor:pointer",
                    "padding:0 4px",
                ].join(";");
                close.addEventListener("click", __gui_clear_error_toast);
                el.appendChild(close);

                document.body.appendChild(el);
                __gui_error_toast_el = el;
            }
            __gui_error_toast_el.firstChild.textContent = String(message);
        } catch (e) {
            try {
                console.error("GUI error:", message);
            } catch (_) {
                /* console unavailable */
            }
        }
    };

    function performRender(widgetTree) {
        // Reset counters per render pass.
        __gui_chart_id = 0;
        __gui_anim_id = 0;
        __gui_vlist_id = 0;
        __gui_acc_id = 0;
        __gui_attr_id = 0;
        __gui_dialog_seq = 0;
        __gui_popup_seq = 0;

        pseudoStyles.beginPass();
        __gui_pending_charts = [];
        __gui_pending_accessible = [];
        __gui_pending_attrs = [];
        __gui_pending_vlists = [];

        const tree = (typeof widgetTree === "string")
            ? JSON.parse(widgetTree)
            : widgetTree;
        const root =
            document.getElementById("gui-root") ||
            document.body;
        render(renderWidget(tree), root);
        syncDialogFocus(root);

        // A successful frame means we have recovered from any prior error, so
        // dismiss the error toast (last-good-frame is now up to date).
        __gui_clear_error_toast();

        pseudoStyles.flush();
        // Flush post-render deferred work (vlist windows, charts, accessible
        // wrappers, per-widget ARIA) in dependency order.
        requestAnimationFrame(() => {
            flushDeferred();
            // Audit runs after author aria_* attributes are applied
            // (flushPendingAttrs, inside flushDeferred), so labelled
            // controls are never falsely flagged.
            __gui_audit_accessible_names(root);
        });
    }

    function __gui_flush_render() {
        if (__gui_pending_render === null) {
            return;
        }
        const widgetTree = __gui_pending_render;
        __gui_pending_render = null;
        performRender(widgetTree);
    }

    // First call is synchronous to avoid a blank flash;
    // subsequent calls coalesce per animation frame.
    let __gui_first_render = true;

    /**
     * Renders a widget tree to the GUI root element.
     * First call is synchronous to avoid blank flash;
     * subsequent calls coalesce per animation frame.
     */
    window.__gui_render = (widgetTree) => {
        if (__gui_first_render) {
            __gui_first_render = false;
            __gui_pending_render = null;
            performRender(widgetTree);
            return;
        }
        __gui_pending_render = widgetTree;
        requestAnimationFrame(__gui_flush_render);
    };

    // ── Theme application ──────────────────────

    /**
     * Applies a theme object to the document, mapping
     * theme keys to CSS custom properties.
     */
    window.__gui_apply_theme = (theme) => {
        const map = {
            font: "--gui-font",
            background: "--gui-bg",
            text_color: "--gui-fg",
            accent: "--gui-primary",
            accent_hover: "--gui-primary-hover",
            border: "--gui-border",
            input_background: "--gui-input-bg",
            input_border: "--gui-input-border",
            input_focus: "--gui-input-focus",
            text_muted: "--gui-text-muted",
            radius: "--gui-radius",
            shadow: "--gui-shadow",
            gap: "--gui-gap",
            disabled_background: "--gui-disabled-bg",
            disabled_text: "--gui-disabled-fg",
            success: "--gui-success",
            warning: "--gui-warning",
            error: "--gui-error",
        };
        let isDark = window.matchMedia &&
            window.matchMedia(
                "(prefers-color-scheme: dark)",
            ).matches;
        if (theme.mode === "dark") {
            isDark = true;
        } else if (theme.mode === "light") {
            isDark = false;
        }
        const root = document.documentElement;
        // Global motion kill-switch: `animations: false` stills every
        // transition/animation via the data-gui-motion="off" CSS block.
        // Any other value (true or absent) leaves motion on, so the whole
        // theme re-applies idempotently.
        if (theme.animations === false) {
            root.setAttribute("data-gui-motion", "off");
        } else {
            root.removeAttribute("data-gui-motion");
        }
        for (const k in theme) {
            if (k === "mode") {
                continue;
            }
            if (
                k.length > 7 &&
                k.startsWith("custom_")
            ) {
                const customProp = "--gui-custom-" +
                    toKebabCase(k.substring(7));
                if (
                    typeof theme[k] === "object" &&
                    theme[k] !== null
                ) {
                    const mv = isDark
                        ? theme[k].dark
                        : theme[k].light;
                    if (mv) {
                        root.style.setProperty(
                            customProp, mv,
                        );
                    }
                } else {
                    root.style.setProperty(
                        customProp, theme[k],
                    );
                }
                continue;
            }
            const prop = map[k];
            if (!prop) {
                continue;
            }
            if (
                typeof theme[k] === "object" &&
                theme[k] !== null
            ) {
                const modeVal = isDark
                    ? theme[k].dark
                    : theme[k].light;
                if (modeVal) {
                    root.style.setProperty(prop, modeVal);
                }
            } else {
                root.style.setProperty(prop, theme[k]);
            }
        }
        window.__gui_theme = theme;
    };

    if (window.matchMedia) {
        window.matchMedia(
            "(prefers-color-scheme: dark)",
        ).addEventListener("change", () => {
            if (window.__gui_theme) {
                window.__gui_apply_theme(
                    window.__gui_theme,
                );
            }
        });
    }

    /**
     * Sets the theme mode (dark, light, or auto) and
     * re-applies the current theme.
     */
    window.__gui_set_theme_mode = (mode) => {
        const root = document.documentElement;
        root.removeAttribute("data-theme");
        if (mode === "dark" || mode === "light") {
            root.setAttribute("data-theme", mode);
            root.style.setProperty("color-scheme", mode);
        } else {
            root.style.removeProperty("color-scheme");
        }
        if (window.__gui_theme) {
            const t = Object.assign(
                {}, window.__gui_theme,
            );
            if (mode !== "auto") {
                t.mode = mode;
            } else {
                delete t.mode;
            }
            window.__gui_apply_theme(t);
        }
    };

    /** Returns the current window width in pixels. */
    window.__gui_get_width = () => __gui_window_width;
})();
