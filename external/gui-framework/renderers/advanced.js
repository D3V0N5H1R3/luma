/* GraphicalUi widget renderers — advanced / composite widgets.
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

        virtual_list: (w, style, extraCls, aria) => {
            const vlItems = w.items || [];
            const vlItemH = w.item_height || 40;
            const vlVisible = w.visible_count || 10;
            const vlTotalH = vlItems.length * vlItemH;
            const vlContainerH = vlVisible * vlItemH;
            const vlId = "__vl_" + (++__gui_vlist_id);
            // Defer viewport population to flushPendingVlists: the scroll handler
            // renders the visible window into .gui-vlist-viewport, so the parent
            // template must not also own that node's children — two lit-html
            // render roots writing one node corrupts part bookkeeping.
            __gui_pending_vlists.push({
                vlId,
                items: vlItems,
                itemH: vlItemH,
                visible: vlVisible,
            });
            return html`<div class=${mergeClass("gui-virtual-list", extraCls)}
                style=${"overflow-y:auto;height:" + vlContainerH + "px;" + style}
                id=${aria.id || nothing} role=${aria.role || "list"}
                data-vlist-id=${vlId}>
                <div style=${"height:" + vlTotalH + "px;position:relative"}>
                    <div class="gui-vlist-viewport" style=${"position:absolute;width:100%;transform:translateY(0px)"}></div>
                </div>
            </div>`;
        },

        keyed: (w, style) => {
            const keyedId = w._key
                ? "__keyed_" + w._key
                : undefined;
            return html`<div id=${keyedId || nothing} style=${style}
                >${w.child ? renderWidget(w.child) : nothing}</div>`;
        },

        badge: (w, style, extraCls, aria) => {
            return html`<span class=${mergeClass("gui-badge", extraCls)} data-variant=${w.variant || "default"} style=${style}
                id=${aria.id || nothing}>${w.text || w.label || ""}</span>`;
        },

        error_boundary: (w, style, extraCls) => {
            if (w._error) {
                return html`<div class=${mergeClass("gui-error-boundary", extraCls)} style=${style} role="alert">
                    <div class="gui-error-boundary-message">${w._error}</div>
                    ${w._retry_id ? html`<button class="gui-button" @click=${makeClickHandler(w._retry_id)}>Retry</button>` : nothing}
                </div>`;
            }
            return w.child
                ? renderWidget(w.child)
                : nothing;
        },

        // Uses a post-render callback to apply
        // dynamic ARIA attributes while keeping lit-html in
        // charge of the child subtree.
        accessible: (w, style, extraCls) => {
            const aAttrs = w.attributes || {};
            const accId = "__acc_" + (++__gui_acc_id);
            __gui_pending_accessible.push({ accId, aAttrs });
            return html`<div class=${mergeClass("gui-accessible", extraCls)}
                data-acc-id=${accId}
                style=${style}
                >${w.child ? renderWidget(w.child) : nothing}</div>`;
        },

        aria_live: (w, style, extraCls, aria) => {
            return childWrapper(w, style, extraCls, aria, "", { ariaLive: true });
        },

        aria_describedby: (w, style, extraCls, aria) => {
            return childWrapper(w, style, extraCls, aria, "", { ariaDescribedby: true });
        },

        navigation_link: (w, style) => {
            return html`<button style=${style}
                @click=${boundHandler(w._callback_id, makeClickHandler)}
                >${w.text || ""}</button>`;
        },

        // Field error: inline validation message.
        field_error: (w, style, extraCls, aria) => {
            return html`<span class=${mergeClass("gui-field-error", extraCls)} style=${style}
                id=${aria.id || nothing} role="alert">
                <span class="gui-alert-icon" aria-hidden="true">${severityIcon("error")}</span>
                <span>${w.message || ""}</span></span>`;
        },

        // Avatar: circular image, or the name's initials when no image
        // URL is supplied.
        avatar: (w, style, extraCls, aria) => {
            const initials = String(w.name || "")
                .split(/\s+/)
                .filter(Boolean)
                .slice(0, 2)
                .map((part) => part[0].toUpperCase())
                .join("");
            return html`<span class=${mergeClass("gui-avatar", extraCls)} style=${style}
                id=${aria.id || nothing} title=${w.name || nothing}>
                ${w.url
                    ? html`<img class="gui-avatar-img" src=${sanitizeUrl(w.url, imgSchemes())} alt=${w.name || ""}>`
                    : html`<span class="gui-avatar-initials">${initials || "?"}</span>`}
            </span>`;
        },

        // Skeleton: shimmering placeholder block for loading states.
        skeleton: (w, style, extraCls, aria) => {
            const skStyle = composeStyle(
                w.width != null ? "width:" + w.width + "px" : "width:100%",
                w.height != null ? "height:" + w.height + "px" : "height:1rem",
                style,
            );
            return html`<span class=${mergeClass("gui-skeleton", extraCls)} style=${skStyle}
                id=${aria.id || nothing} aria-hidden="true"></span>`;
        },

        // Accordion: a stack of collapsible sections. Each section is a
        // dictionary with a title/label and string or widget content.
        accordion: (w, style, extraCls, aria) => {
            return html`<div class=${mergeClass("gui-accordion", extraCls)} style=${style}
                id=${aria.id || nothing}>${(w.sections || []).map((sec) => {
                const section = sec || {};
                const title = section.title || section.label || "";
                const open = !!(section.open || section.expanded);
                const body = section.content != null ? section.content : section.body;
                const children = section.children;
                return html`<details class="gui-accordion-item" ?open=${open}>
                    <summary class="gui-accordion-summary">${title}</summary>
                    <div class="gui-accordion-body">${
                        Array.isArray(children)
                            ? children.map(renderWidget)
                            : (body && typeof body === "object" && body.type
                                ? renderWidget(body)
                                : (body == null ? nothing : String(body)))
                    }</div>
                </details>`;
            })}</div>`;
        },

        // Breadcrumb: navigation trail; non-final items are clickable
        // when an on_navigate callback is supplied.
        breadcrumb: (w, style, extraCls, aria) => {
            const items = w.items || [];
            return html`<nav class=${mergeClass("gui-breadcrumb", extraCls)} style=${style}
                id=${aria.id || nothing} aria-label="breadcrumb">${items.map((item, i) => {
                const label = typeof item === "object"
                    ? (item.label || item.text || "")
                    : String(item);
                const last = i === items.length - 1;
                const sep = i > 0
                    ? html`<span class="gui-breadcrumb-sep" aria-hidden="true">/</span>`
                    : nothing;
                const node = (w._callback_id && !last)
                    ? html`<button class="gui-breadcrumb-item" data-link="true"
                        @click=${makeSelectHandler(w._callback_id, label)}>${label}</button>`
                    : html`<span class="gui-breadcrumb-item" data-current=${last}>${label}</span>`;
                return html`${sep}${node}`;
            })}</nav>`;
        },

        // Toast: transient notification; reuses the alert severity
        // palette.
        toast: (w, style, extraCls, aria) => {
            const severity = w.severity || "info";
            return html`<div class=${mergeClass("gui-alert gui-toast", extraCls)} data-severity=${severity} style=${style}
                id=${aria.id || nothing} role="status" aria-live="polite">
                <span class="gui-alert-icon" aria-hidden="true">${severityIcon(severity)}</span>
                <span class="gui-alert-message">${w.message || ""}</span>
                ${(w._action_id && w.action_label)
                    ? html`<button class="gui-toast-action" type="button"
                        @click=${makeClickHandler(w._action_id)}>${w.action_label}</button>`
                    : nothing}
            </div>`;
        },

        // Toast region: a fixed-position stacking area for transient toasts.
        // It is a labelled landmark, not a live region — each child toast keeps
        // its own role="status"/aria-live so insertions announce once.
        toast_region: (w, style, extraCls, aria) => {
            const position = w.position || "bottom-right";
            return html`<div class=${mergeClass("gui-toast-region", extraCls)}
                data-position=${position} style=${style}
                id=${aria.id || nothing} role="region" aria-label="Notifications"
                >${(w.children || []).map(renderWidget)}</div>`;
        },

        // Empty state: a friendly placeholder for blank lists/panels — icon,
        // optional title, message, and an optional call-to-action button. It is
        // a static placeholder (no role="status"): announcing it as a live
        // status update would be wrong, and it contains a focusable action.
        empty_state: (w, style, extraCls, aria) => {
            const iconName = w.icon != null ? w.icon : "inbox";
            const ico = mkIcon(iconName, 48);
            return html`<div class=${mergeClass("gui-empty-state", extraCls)} style=${style}
                id=${aria.id || nothing}>
                ${ico
                    ? html`<span class="gui-empty-state-icon" aria-hidden="true">${ico}</span>`
                    : nothing}
                ${w.title
                    ? html`<div class="gui-empty-state-title">${w.title}</div>`
                    : nothing}
                <div class="gui-empty-state-message">${w.message || ""}</div>
                ${(w._action_id && w.action_label)
                    ? html`<button class="gui-button gui-empty-state-action" type="button"
                        data-variant="primary"
                        @click=${makeClickHandler(w._action_id)}>${w.action_label}</button>`
                    : nothing}
            </div>`;
        },

        // Number input: numeric field. The host coerces the "slide"
        // event payload to a number for the on_change callback.
        number_input: (w, style, extraCls, aria) => {
            return simpleInput(w, style, extraCls, aria, SIMPLE_INPUTS.number_input);
        },

        // Search input: text field with an optional clear button.
        search_input: (w, style, extraCls, aria) => {
            return html`<div class=${mergeClass("gui-search-input", extraCls)} style=${style}
                id=${aria.id || nothing}>
                <input type="search" class="gui-search-input-field"
                    .value=${w.value || ""} placeholder=${w.placeholder || "Search"}
                    @input=${boundHandler(w._callback_id, makeChangeHandler)}
                    >
                ${w._clear_id
                    ? html`<button class="gui-search-input-clear" aria-label="Clear"
                        @click=${makeClickHandler(w._clear_id)}>&times;</button>`
                    : nothing}
            </div>`;
        },

        // Form: groups children and fires on_submit when submitted
        // (e.g. by pressing Enter in a field).
        form: (w, style, extraCls, aria) => {
            const submitHandler = (e) => {
                e.preventDefault();
                if (w._callback_id) {
                    emit({ type: "click", id: w._callback_id });
                }
            };
            return html`<form class=${mergeClass("gui-form", extraCls)} style=${style}
                id=${aria.id || nothing} @submit=${submitHandler}
                >${(w.children || []).map(renderWidget)}</form>`;
        },

        // Wizard: numbered step header plus the active step's content.
        wizard: (w, style, extraCls, aria) => {
            const steps = w.steps || [];
            const active = w.active_step != null ? w.active_step : 0;
            const activeStep = steps[active];
            return html`<div class=${mergeClass("gui-wizard", extraCls)} style=${style}
                id=${aria.id || nothing}>
                <div class="gui-wizard-steps">${steps.map((_, i) => {
                    return html`<button class="gui-wizard-step"
                        data-active=${i === active} data-done=${i < active}
                        @click=${w._callback_id ? makeSelectHandler(w._callback_id, i) : undefined}
                        >${i + 1}</button>`;
                })}</div>
                <div class="gui-wizard-content">${
                    activeStep && typeof activeStep === "object" && activeStep.type
                        ? renderWidget(activeStep)
                        : nothing
                }</div>
            </div>`;
        },

        // Paginator: previous/next plus a button per page.
        paginator: (w, style, extraCls, aria) => {
            const cur = w.current_page != null ? w.current_page : 1;
            const total = w.total_pages != null ? w.total_pages : 1;
            const cb = w._callback_id;
            const pages = [];
            for (let pi = 1; pi <= total; pi++) {
                pages.push(pi);
            }
            return html`<div class=${mergeClass("gui-paginator", extraCls)} style=${style}
                id=${aria.id || nothing} role="navigation" aria-label="pagination">
                <button class="gui-paginator-btn" ?disabled=${cur <= 1}
                    @click=${cb && cur > 1 ? makeSelectHandler(cb, cur - 1) : undefined}
                    aria-label="Previous">‹</button>
                ${pages.map((pi) => {
                    return html`<button class="gui-paginator-btn" data-active=${pi === cur}
                        @click=${cb ? makeSelectHandler(cb, pi) : undefined}>${pi}</button>`;
                })}
                <button class="gui-paginator-btn" ?disabled=${cur >= total}
                    @click=${cb && cur < total ? makeSelectHandler(cb, cur + 1) : undefined}
                    aria-label="Next">›</button>
            </div>`;
        },

        // Infinite scroll: scrollable item list with a load-more
        // trigger that fires on_load_more.
        infinite_scroll: (w, style, extraCls, aria) => {
            const items = w.items || [];
            const itemH = w.item_height || 40;
            const scStyle = composeStyle("overflow:auto", style);
            return html`<div class=${mergeClass("gui-infinite-scroll", extraCls)} style=${scStyle}
                id=${aria.id || nothing}>
                ${items.map((item) => {
                    return html`<div class="gui-infinite-scroll-item" style=${"min-height:" + itemH + "px"}
                        >${typeof item === "object" && item.type ? renderWidget(item) : String(item)}</div>`;
                })}
                ${w._callback_id
                    ? html`<button class="gui-infinite-scroll-more"
                        @click=${makeClickHandler(w._callback_id)}>Load more</button>`
                    : nothing}
            </div>`;
        },

        // Draggable: makes its child draggable and carries a string
        // payload via the native drag-and-drop dataTransfer.
        draggable: (w, style, extraCls, aria) => {
            const dragStart = (e) => {
                if (e.dataTransfer) {
                    e.dataTransfer.setData(
                        "text/plain",
                        w.data != null ? String(w.data) : "",
                    );
                    e.dataTransfer.effectAllowed = "copy";
                }
            };
            return html`<div class=${mergeClass("gui-draggable", extraCls)} style=${style}
                id=${aria.id || nothing} draggable="true" @dragstart=${dragStart}
                >${w.child ? renderWidget(w.child) : nothing}</div>`;
        },

        // Drop target: accepts a dragged payload and forwards the
        // dropped string to on_drop.
        drop_target: (w, style, extraCls, aria) => {
            const overHandler = (e) => {
                e.preventDefault();
                if (e.dataTransfer) {
                    e.dataTransfer.dropEffect = "copy";
                }
            };
            const dropHandler = (e) => {
                e.preventDefault();
                if (w._callback_id) {
                    const data = e.dataTransfer
                        ? e.dataTransfer.getData("text/plain")
                        : "";
                    emit({
                        type: "callback_result",
                        id: w._callback_id,
                        value: data,
                    });
                }
            };
            return html`<div class=${mergeClass("gui-drop-target", extraCls)} style=${style}
                id=${aria.id || nothing} @dragover=${overHandler} @drop=${dropHandler}
                >${w.child ? renderWidget(w.child) : nothing}</div>`;
        },

        // Inspect: developer pass-through that renders its child
        // unchanged.
        inspect: (w, style, extraCls, aria) => {
            return childWrapper(w, style, extraCls, aria, "gui-inspect");
        },
    });
