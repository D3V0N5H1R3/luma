/* GraphicalUi widget renderers — disclosure & roving-focus widgets.
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

        // Menu: a button that discloses a list of actions. The trigger and
        // every item are keyboard-operable (arrows / Home / End / Esc) with
        // roving focus, following the WAI-ARIA menu-button pattern.
        menu: (w, style, extraCls, aria) => {
            const items = w.items || [];
            const selectId = w._callback_id;
            return html`<div class=${mergeClass("gui-menu", extraCls)} data-gui-popup data-open="false"
                style=${style} id=${aria.id || nothing}>
                <button class="gui-menu-trigger" type="button"
                    aria-haspopup="menu" aria-expanded="false"
                    @click=${onMenuTriggerClick} @keydown=${onMenuTriggerKeydown}>
                    <span>${w.label || "Menu"}</span>
                </button>
                <div class="gui-menu-popup" role="menu" @keydown=${onMenuKeydown}>${items.map((item) => {
                    const value = String(item);
                    return html`<button class="gui-menu-item" type="button"
                        role="menuitem" tabindex="-1"
                        @click=${makeMenuItemClick(selectId, value)}>${value}</button>`;
                })}</div>
            </div>`;
        },

        // Popover: a button that discloses a floating panel of arbitrary
        // content. Opens on click, closes on outside-click / Esc, and moves
        // focus to the first focusable element inside the panel.
        popover: (w, style, extraCls, aria) => {
            return html`<div class=${mergeClass("gui-popover", extraCls)} data-gui-popup data-open="false"
                style=${style} id=${aria.id || nothing}>
                <button class="gui-popover-trigger" type="button"
                    aria-haspopup="dialog" aria-expanded="false"
                    @click=${onPopoverTriggerClick}>${w.label || "Open"}</button>
                <div class="gui-popover-panel" role="dialog">${w.child ? renderWidget(w.child) : nothing}</div>
            </div>`;
        },

        // Combobox: a text input paired with a filterable listbox. Typing
        // emits "change" (the host filters and re-renders the options);
        // choosing an option emits "select". Follows the ARIA combobox
        // (aria-activedescendant) pattern so focus stays in the input.
        combobox: (w, style, extraCls, aria) => {
            const options = w.options || [];
            const listId = "__gui_cbx_" + (++__gui_popup_seq);
            const ctl = makeComboboxController(w);
            return html`<div class=${mergeClass("gui-combobox", extraCls)} data-gui-popup data-open="false"
                data-active-index="-1" style=${style} id=${aria.id || nothing}>
                <input class="gui-combobox-input" type="text"
                    role="combobox" aria-expanded="false"
                    aria-haspopup="listbox" aria-autocomplete="list"
                    aria-controls=${listId}
                    .value=${w.value || ""}
                    placeholder=${w.placeholder || ""}
                    @input=${ctl.onInput} @focus=${ctl.onFocus}
                    @keydown=${ctl.onKeydown}>
                <ul class="gui-combobox-listbox" role="listbox" id=${listId}>${options.length === 0
                    ? html`<li class="gui-combobox-empty">No options</li>`
                    : options.map((opt, i) => {
                        const value = String(opt);
                        const optId = listId + "_opt" + i;
                        return html`<li class="gui-combobox-option" role="option"
                            id=${optId} data-value=${value} data-active="false"
                            @mousedown=${ctl.onOptionMousedown(value)}>${value}</li>`;
                    })}</ul>
            </div>`;
        },
    });
