/* GraphicalUi widget renderers — layout containers, overlays & structural widgets.
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
    // Layout containers all delegate to renderContainer, differing only by the
    // CSS class applied.  This factory turns a class name into the renderer.
    const createContainerRenderer = (className) => (w, style, extraCls, aria) =>
        renderContainer(w, className, style, extraCls, aria);

    Object.assign(WIDGET_RENDERERS, {

        // Field: wraps a single control in a real <label> (implicit association)
        // so inputs gain a persistent, programmatically-linked label instead of
        // relying on placeholder text. Carries validation affordances too:
        // a required marker, help text, and an error message with an icon.
        field: (w, style, extraCls, aria) => {
            const hasError = !!w.error;
            return html`<div class=${mergeClass("gui-field", extraCls)} style=${style}
                id=${aria.id || nothing} data-invalid=${hasError ? "true" : "false"}>
                <label class="gui-field-label">
                    <span class="gui-field-label-text">${w.label || ""}${w.required
                        ? html`<span class="gui-field-required" aria-hidden="true"> *</span>`
                        : nothing}</span>
                    ${w.child ? renderWidget(w.child) : nothing}
                </label>
                ${w.help
                    ? html`<small class="gui-field-help">${w.help}</small>`
                    : nothing}
                ${hasError
                    ? html`<small class="gui-field-error" role="alert">
                        <span class="gui-alert-icon" aria-hidden="true">${severityIcon("error")}</span>
                        <span>${w.error}</span></small>`
                    : nothing}
            </div>`;
        },

        // Layout containers delegate to renderContainer, differing only by class.
        row: createContainerRenderer("gui-row"),
        column: createContainerRenderer("gui-column"),
        wrapped_row: createContainerRenderer("gui-wrapped-row"),
        scroll_row: createContainerRenderer("gui-scroll-row"),
        scroll_column: createContainerRenderer("gui-scroll-column"),

        panel: (w, style, extraCls, aria) => {
            return html`<div class=${mergeClass("gui-panel", extraCls)} style=${style}
                id=${aria.id || nothing} role=${aria.role || nothing}>
                ${w.title ? html`<span class="gui-heading" data-level="3" style="margin-bottom:8px">${w.title}</span>` : nothing}
                ${(w.children || []).map(renderWidget)}
            </div>`;
        },

        list: (w, style, extraCls, aria) => {
            const clickable = !!w._callback_id;
            // A clickable list is a set of buttons (keyboard-operable), so the
            // container drops the list/listitem semantics; a static list keeps
            // them.
            const containerRole = clickable
                ? (aria.role || nothing)
                : (aria.role || "list");
            return html`<div class=${extraCls || nothing} style=${style}
                id=${aria.id || nothing} role=${containerRole}
                >${(w.items || []).map((item, i) => {
                const content = (typeof item === "object" && item.type)
                    ? renderWidget(item)
                    : String(item);
                return clickable
                    ? html`<button class="gui-list-item" type="button" data-clickable="true"
                        @click=${makeSelectHandler(w._callback_id, i)}
                        >${content}</button>`
                    : html`<div class="gui-list-item" role="listitem"
                        >${content}</div>`;
            })}</div>`;
        },

        radio_group: (w, style, extraCls, aria) => {
            return html`<div class=${mergeClass("gui-radio-group", extraCls)} style=${style}
                id=${aria.id || nothing} role="radiogroup">${(w.options || []).map((opt) => {
                return html`<label class="gui-radio-item">
                    <input type="radio" name=${w._callback_id || "_rg"}
                        .checked=${opt === w.selected}
                        @change=${w._callback_id ? makeSelectHandler(w._callback_id, opt) : undefined}
                    ><span>${opt}</span></label>`;
            })}</div>`;
        },

        toggle: (w, style, extraCls, aria) => {
            return toggleLike(w, style, extraCls, aria, "gui-toggle-wrapper");
        },

        tabs: (w, style, extraCls, aria) => {
            const activeIdx =
                w.active != null ? w.active : 0;
            const tabChildren = w.children || [];
            const onTabKeydown = (e) => {
                const bar = e.currentTarget.closest(".gui-tabs-bar");
                const tabEls = Array.from(
                    bar.querySelectorAll(".gui-tab"),
                );
                const i = tabEls.indexOf(e.currentTarget);
                const next = rovingFocusIndex(e.key, i, tabEls.length, "both");
                if (next >= 0) {
                    e.preventDefault();
                    tabEls[next].focus();
                    if (w._callback_id) {
                        emit({
                            type: "select",
                            id: w._callback_id,
                            value: next,
                        });
                    }
                }
            };
            return html`<div class=${extraCls || nothing} style=${style}
                id=${aria.id || nothing}>
                <div class="gui-tabs-bar" role=${aria.role || "tablist"}>${(w.labels || []).map((label, i) => {
                    const selected = i === activeIdx;
                    return html`<button class="gui-tab" type="button" role="tab"
                        data-active=${selected}
                        aria-selected=${selected ? "true" : "false"}
                        tabindex=${selected ? "0" : "-1"}
                        @click=${w._callback_id ? makeSelectHandler(w._callback_id, i) : undefined}
                        @keydown=${onTabKeydown}
                        >${label}</button>`;
                })}</div>
                <div class="gui-tabs-content" role="tabpanel">${tabChildren[activeIdx] ? renderWidget(tabChildren[activeIdx]) : nothing}</div>
            </div>`;
        },

        table: (w, style, extraCls, aria) => {
            const aligns = Array.isArray(w.align) ? w.align : [];
            const cellAlign = (i) => {
                const a = aligns[i];
                return (a === "right" || a === "center" || a === "left")
                    ? "text-align:" + a
                    : nothing;
            };

            // Row selection: `selected` may be a single index or an array of
            // indices. Selected rows are flagged for styling and assistive tech.
            const selected = Array.isArray(w.selected)
                ? w.selected
                : (w.selected != null ? [w.selected] : []);
            const isSelected = (i) => selected.indexOf(i) !== -1;

            // Sorting: when an on_sort callback is bound, headers become
            // buttons that dispatch the clicked column index, and the active
            // column advertises its direction through aria-sort + an arrow.
            const sortId = w._sort_id;
            const sortCol = w.sort_column != null ? w.sort_column : -1;
            const sortDir = w.sort_direction === "desc" ? "desc" : "asc";
            const ariaSort = (i) => {
                if (i !== sortCol) {
                    return sortId ? "none" : nothing;
                }
                return sortDir === "desc" ? "descending" : "ascending";
            };
            const sortArrow = (i) => {
                if (i !== sortCol) {
                    return "";
                }
                return sortDir === "desc" ? " ▼" : " ▲";
            };

            const clickable = !!w._callback_id;
            return html`<table class=${mergeClass("gui-table", extraCls)} style=${style}
                id=${aria.id || nothing}>
                <thead><tr>${(w.headers || []).map((h, i) => {
                    return html`<th style=${cellAlign(i)} aria-sort=${ariaSort(i)}>${sortId
                        ? html`<button type="button" class="gui-table-sort"
                            @click=${makeSelectHandler(sortId, i)}
                            >${h}<span class="gui-table-sort-arrow" aria-hidden="true"
                                >${sortArrow(i)}</span></button>`
                        : h}</th>`;
                })}</tr></thead>
                <tbody>${(w.rows || []).map((row, i) => {
                    return html`<tr data-clickable=${clickable}
                        data-selected=${isSelected(i) ? "true" : nothing}
                        aria-selected=${isSelected(i) ? "true" : nothing}
                        @click=${clickable ? makeSelectHandler(w._callback_id, i) : undefined}
                        >${(row || []).map((cell, ci) => {
                            return html`<td style=${cellAlign(ci)}>${String(cell)}</td>`;
                        })}</tr>`;
                })}</tbody>
            </table>`;
        },

        dialog: (w, style, extraCls, aria) => {
            if (!w.is_open) {
                return nothing;
            }
            const closeHandler = boundHandler(w._close_id, makeClickHandler);
            const titleId = "__gui_dlg_t_" + (++__gui_dialog_seq);
            return dialogShell({
                widgetCls: "gui-dialog",
                style,
                extraCls,
                id: aria.id,
                role: aria.role || "dialog",
                closeId: w._close_id,
                labelledById: w.title ? titleId : undefined,
                body: [
                    w.title ? html`<div class="gui-dialog-title" id=${titleId}>${w.title}</div>` : nothing,
                    w._close_id ? html`<button class="gui-dialog-close" type="button" aria-label="Close" @click=${closeHandler}>&times;</button>` : nothing,
                    (w.children || []).map(renderWidget),
                ],
            });
        },

        // Confirm: a modal confirmation dialog for (destructive) actions. Reuses
        // the .gui-dialog shell so the shared focus trap, Escape-to-cancel, and
        // focus restoration all apply. on_confirm is the primary action;
        // on_cancel (data-close-id) also fires on Escape and backdrop click.
        confirm: (w, style, extraCls, aria) => {
            const confirmHandler = boundHandler(w._callback_id, makeClickHandler);
            const cancelHandler = boundHandler(w._close_id, makeClickHandler);
            const titleId = "__gui_cfm_t_" + (++__gui_dialog_seq);
            const msgId = "__gui_cfm_m_" + __gui_dialog_seq;
            return dialogShell({
                widgetCls: "gui-dialog gui-confirm",
                style,
                extraCls,
                id: aria.id,
                role: "alertdialog",
                closeId: w._close_id,
                labelledById: w.title ? titleId : undefined,
                describedById: w.message ? msgId : undefined,
                body: [
                    w.title ? html`<div class="gui-dialog-title" id=${titleId}>${w.title}</div>` : nothing,
                    w.message ? html`<div class="gui-confirm-message" id=${msgId}>${w.message}</div>` : nothing,
                    html`<div class="gui-confirm-actions">
                        ${w._close_id ? html`<button class="gui-button gui-confirm-cancel" type="button"
                            @click=${cancelHandler}>${w.cancel_label || "Cancel"}</button>` : nothing}
                        <button class="gui-button gui-confirm-confirm" type="button"
                            data-danger=${w.danger ? "true" : "false"}
                            @click=${confirmHandler}>${w.confirm_label || "Confirm"}</button>
                    </div>`,
                ],
            });
        },

        alert: (w, style, extraCls, aria) => {
            const severity = w.severity || "info";
            return html`<div class=${mergeClass("gui-alert", extraCls)} data-severity=${severity} style=${style}
                id=${aria.id || nothing} role="alert">
                <span class="gui-alert-icon" aria-hidden="true">${severityIcon(severity)}</span>
                <span class="gui-alert-message">${w.message || ""}</span>
            </div>`;
        },

        tooltip: (w, style, extraCls, aria) => {
            const tipId = "__gui_tip_" + (++__gui_popup_seq);
            return html`<span class=${mergeClass("gui-tooltip-wrapper", extraCls)} style=${style}
                id=${aria.id || nothing} tabindex="0" aria-describedby=${tipId}>
                ${w.child ? renderWidget(w.child) : nothing}
                <span class="gui-tooltip-text" role="tooltip" id=${tipId}>${w.text || ""}</span>
            </span>`;
        },

        link: (w, style, extraCls, aria) => {
            const safeHref = sanitizeUrl(w.href, GUI_LINK_SCHEMES);
            if (safeHref) {
                return html`<a class=${mergeClass("gui-link", extraCls)} href=${safeHref} target="_blank" rel="noopener" style=${style}
                    id=${aria.id || nothing}
                    >${w.text || ""}</a>`;
            }
            return html`<button class=${mergeClass("gui-link", extraCls)} style=${style}
                id=${aria.id || nothing}
                @click=${boundHandler(w._callback_id, makeClickHandler)}
                >${w.text || ""}</button>`;
        },

        icon: (w, style, extraCls, aria) => {
            const iconName = toKebabCase((w.name || "").toLowerCase());
            const ico = mkIcon(iconName, w.size);
            if (ico) {
                return html`<span class=${mergeClass("gui-icon", extraCls)} style=${style}
                    id=${aria.id || nothing}>${ico}</span>`;
            }
            // Unknown name: render a recognisable fallback glyph rather than the
            // raw text (which looks like a bug). The intended name is preserved
            // as the accessible label/title so it stays discoverable.
            if (window.__gui_devtools) {
                console.warn(
                    "[GraphicalUi] Unknown icon name: " + iconName +
                    " — rendering fallback glyph",
                );
            }
            const fallback = mkIcon("help-circle", w.size);
            return html`<span class=${mergeClass("gui-icon gui-icon-fallback", extraCls)}
                style=${style} id=${aria.id || nothing}
                data-fallback="true" title=${"Unknown icon: " + iconName}
                aria-label=${"Unknown icon: " + iconName}
                >${fallback ? fallback : "□"}</span>`;
        },

        toolbar: (w, style, extraCls, aria) => {
            return html`<div class=${mergeClass("gui-toolbar", extraCls)} style=${style}
                id=${aria.id || nothing} role=${aria.role || "toolbar"}
                >${(w.children || []).map(renderWidget)}</div>`;
        },

        grid: (w, style, extraCls, aria) => {
            const gridCols = w.columns || 2;
            // Drive the column count through a custom property (not an inline
            // grid-template-columns) so the stylesheet can collapse the grid to
            // a single column on narrow viewports — automatic mobile-first reflow
            // without the caller wiring up responsive() subscriptions.
            const gridStyle = composeStyle(
                "--gui-grid-cols:" + gridCols,
                style,
            );
            return html`<div class=${mergeClass("gui-grid", extraCls)} style=${gridStyle}
                id=${aria.id || nothing}
                >${(w.children || []).map(renderWidget)}</div>`;
        },

        nearby: (w, style, extraCls, aria) => {
            const pos = w.position || "above";
            return html`<div class=${mergeClass("gui-nearby-wrapper", extraCls)} style=${style}
                id=${aria.id || nothing}>
                ${w.child ? renderWidget(w.child) : nothing}
                <div class=${"gui-nearby-" + pos}>${w.overlay ? renderWidget(w.overlay) : nothing}</div>
            </div>`;
        },

        debug: (w, style, extraCls, aria) => {
            return childWrapper(w, style, extraCls, aria, "gui-debug");
        },

        transition: (w, style, extraCls) => {
            const tp = w.properties || {};
            const tProp = tp.property || "all";
            const tDur = tp.duration || "0.3s";
            const tEase = tp.easing || "ease";
            const tDelay = tp.delay || "0s";
            // Sanitise the developer-supplied transition value.
            const transValue = sanitizeCssValue(
                "transition",
                tProp + " " + tDur + " " + tEase + " " + tDelay,
            );
            const transStyle = composeStyle(
                transValue ? "transition:" + transValue : "",
                style,
            );
            return html`<div class=${mergeClass("gui-transition", extraCls)} style=${transStyle}
                >${w.child ? renderWidget(w.child) : nothing}</div>`;
        },

        animate: (w, style, extraCls) => {
            const aOpts = w.options || {};
            const aDur = aOpts.duration || "1s";
            const aEase = aOpts.easing || "ease";
            const aDelay = aOpts.delay || "0s";
            const aIter = aOpts.iterations || "1";
            const aDir = aOpts.direction || "normal";
            const aFill = aOpts.fill_mode || "none";

            const animName =
                "gui-anim-" + (++__gui_anim_id);
            const kfArr = w.keyframes || [];
            let kfCss =
                "@keyframes " + animName + "{";
            for (let ki = 0; ki < kfArr.length; ki++) {
                const pct = kfArr.length === 1
                    ? "100%"
                    : (Math.round(
                        (ki / (kfArr.length - 1)) * 100,
                    ) + "%");
                kfCss += pct + "{";
                const kf = kfArr[ki] || {};
                // Sanitise keyframe values before they
                // are injected into a <style> block.
                for (const kp in kf) {
                    const kfProp = toKebabCase(kp);
                    const kfVal = sanitizeCssValue(kfProp, kf[kp]);
                    if (kfVal !== null && isSafeCssName(kfProp)) {
                        kfCss += kfProp + ":" + kfVal + ";";
                    }
                }
                kfCss += "}";
            }
            kfCss += "}";
            window.__gui_inject_css(kfCss);

            const animStyle = composeStyle(
                "animation:" + animName + " " + aDur +
                " " + aEase + " " + aDelay + " " +
                aIter + " " + aDir + " " + aFill,
                style,
            );
            return html`<div class=${mergeClass("gui-animate", extraCls)} style=${animStyle}
                >${w.child ? renderWidget(w.child) : nothing}</div>`;
        },

        // Card: a bordered surface that stacks its children, like a
        // titleless panel.
        card: (w, style, extraCls, aria) => {
            return html`<div class=${mergeClass("gui-card", extraCls)} style=${style}
                id=${aria.id || nothing} role=${aria.role || nothing}
                >${(w.children || []).map(renderWidget)}</div>`;
        },

        // Switch: same on/off track as toggle, exposed as a separate
        // catalog widget with the switch ARIA role.
        switch: (w, style, extraCls, aria) => {
            return toggleLike(w, style, extraCls, aria, "gui-toggle-wrapper gui-switch");
        },
    });
