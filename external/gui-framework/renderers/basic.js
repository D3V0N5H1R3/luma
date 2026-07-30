/* GraphicalUi widget renderers — basic & input widgets.
 *
 * Not a standalone module: this fragment is concatenated into the
 * gui-renderer.js IIFE (at the // __GUI_WIDGET_RENDERER_FRAGMENTS__ marker) by
 * scripts/generate_gui_assets.mjs and the C++ dev-asset loader. It uses the
 * module-private helpers defined there (html, nothing, renderWidget,
 * mergeClass, boundHandler, the shared factories/controllers, …) and
 * populates the shared WIDGET_RENDERERS table.
 *
 * SPDX-License-Identifier: MIT
 */
    Object.assign(WIDGET_RENDERERS, {

        label: (w, style, extraCls, aria, evtHandlers) => {
            return html`<span class=${mergeClass("gui-label", extraCls)} style=${style}
                id=${aria.id || nothing} role=${aria.role || nothing}
                @click=${evtHandlers.click} @dblclick=${evtHandlers.dblclick}
                @contextmenu=${evtHandlers.contextmenu}
                @mouseenter=${evtHandlers.mouseenter} @mouseleave=${evtHandlers.mouseleave}
                >${w.text || ""}</span>`;
        },

        heading: (w, style, extraCls, aria) => {
            return html`<span class=${mergeClass("gui-heading", extraCls)} data-level=${w.level || 1} style=${style}
                id=${aria.id || nothing} role=${aria.role || nothing}
                >${w.text || ""}</span>`;
        },

        button: (w, style, extraCls, aria) => {
            return html`<button class=${mergeClass("gui-button", extraCls)} style=${style}
                id=${aria.id || nothing} role=${aria.role || nothing}
                data-variant=${w.variant || nothing}
                ?disabled=${w.disabled}
                @click=${boundHandler(w._callback_id, makeClickHandler)}
                >${w.label || "Button"}</button>`;
        },

        // Spinner: an indeterminate busy indicator for "working, unknown
        // duration" states. role="status" + the visually-hidden label announce
        // the activity to assistive technology; the ring stops under
        // prefers-reduced-motion (see the stylesheet).
        spinner: (w, style, extraCls, aria) => {
            const label = w.label != null ? w.label : "Loading…";
            return html`<span class=${mergeClass("gui-spinner", extraCls)} style=${style}
                id=${aria.id || nothing} role="status" aria-live="polite">
                <span class="gui-spinner-ring" aria-hidden="true"></span>
                <span class="gui-visually-hidden">${label}</span>
            </span>`;
        },

        text_input: (w, style, extraCls, aria) => {
            return html`<input type=${w.input_type || "text"} class=${extraCls || nothing} style=${style}
                id=${aria.id || nothing} role=${aria.role || nothing}
                .value=${w.value || ""} placeholder=${w.placeholder || ""}
                @input=${boundHandler(w._callback_id, makeChangeHandler)}
                @change=${w._commit_id ? makeCommitChangeHandler(w._commit_id) : undefined}
                @keydown=${w._commit_id ? makeCommitKeyHandler(w._commit_id, false) : undefined}
                >`;
        },

        text_area: (w, style, extraCls, aria) => {
            return html`<textarea class=${extraCls || nothing} style=${style}
                id=${aria.id || nothing} role=${aria.role || nothing}
                .value=${w.value || ""} placeholder=${w.placeholder || ""}
                rows=${w.rows || 4}
                @input=${boundHandler(w._callback_id, makeChangeHandler)}
                @change=${w._commit_id ? makeCommitChangeHandler(w._commit_id) : undefined}
                @keydown=${w._commit_id ? makeCommitKeyHandler(w._commit_id, true) : undefined}
                ></textarea>`;
        },

        checkbox: (w, style, extraCls, aria) => {
            return html`<label class=${mergeClass("gui-checkbox-wrapper", extraCls)} style=${style}
                id=${aria.id || nothing}>
                <input type="checkbox" .checked=${!!w.checked}
                    @change=${boundHandler(w._callback_id, makeToggleHandler)}
                ><span>${w.label || ""}</span></label>`;
        },

        dropdown: (w, style, extraCls, aria) => {
            return html`<select class=${extraCls || nothing} style=${style}
                id=${aria.id || nothing} role=${aria.role || nothing}
                @change=${boundHandler(w._callback_id, makeDropdownHandler)}
                >${(w.options || []).map((opt) => {
                    return html`<option value=${opt} ?selected=${opt === w.value}>${opt}</option>`;
                })}</select>`;
        },

        slider: (w, style, extraCls, aria) => {
            return html`<input type="range" class=${extraCls || nothing} style=${style}
                id=${aria.id || nothing}
                min=${w.min != null ? w.min : 0}
                max=${w.max != null ? w.max : 100}
                step=${w.step != null ? w.step : 1}
                .value=${String(w.value != null ? w.value : 50)}
                @input=${boundHandler(w._callback_id, makeSlideHandler)}
                >`;
        },

        progress: (w, style, extraCls, aria) => {
            return html`<progress class=${extraCls || nothing} value=${w.value || 0} max=${w.max || 100}
                id=${aria.id || nothing} style=${style}></progress>`;
        },

        image: (w, style, extraCls, aria) => {
            return html`<img class=${mergeClass("gui-image", extraCls)} src=${sanitizeUrl(w.source, imgSchemes())} alt=${w.alt || ""}
                width=${w.width || nothing} height=${w.height || nothing}
                id=${aria.id || nothing}
                style=${style}>`;
        },

        separator: (w, style, extraCls) => {
            return html`<hr class=${mergeClass("gui-separator", extraCls)} style=${style}>`;
        },

        spacer: (w, style, extraCls) => {
            const spacerStyle = composeStyle(
                style,
                w.height
                    ? "height:" + w.height + "px"
                    : (!w.width && !w.flex ? "height:16px" : ""),
                w.width ? "width:" + w.width + "px" : "",
                w.flex ? "flex:" + w.flex + " 1 0%" : "",
            );
            return html`<div class=${mergeClass("gui-spacer", extraCls)} style=${spacerStyle}></div>`;
        },

        // Use extracted handler factories.
        file_input: (w, style, extraCls, aria) => {
            return simpleInput(w, style, extraCls, aria, SIMPLE_INPUTS.file_input);
        },

        // Date / time / color pickers: table-driven <input> variants.
        date_picker: (w, style, extraCls, aria) => {
            return simpleInput(w, style, extraCls, aria, SIMPLE_INPUTS.date_picker);
        },

        time_picker: (w, style, extraCls, aria) => {
            return simpleInput(w, style, extraCls, aria, SIMPLE_INPUTS.time_picker);
        },

        color_picker: (w, style, extraCls, aria) => {
            return simpleInput(w, style, extraCls, aria, SIMPLE_INPUTS.color_picker);
        },
    });
