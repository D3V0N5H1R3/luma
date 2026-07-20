/* GraphicalUi subscription management (timers, keyboard, resize, focus, mouse).
 *
 * SPDX-License-Identifier: MIT
 */
(function() {
    "use strict";

    window.__gui_subs = {};

    // Reuse the shared event emitter defined by the renderer.
    const emit = window.__gui_emit;

    /** Remove a previous subscription for the same id before re-registering. */
    function ensureRemoved(id) {
        if (window.__gui_subs[id]) {
            window.__gui_remove_sub(id);
        }
    }

    // Generic subscription registration. `attach` wires up the
    // listeners and returns a cleanup function; the result is stored so that
    // __gui_remove_sub can detach it later.
    function register(id, type, attach) {
        ensureRemoved(id);
        const cleanup = attach();
        window.__gui_subs[id] = { type, cleanup };
    }

    // Coalesce high-frequency events (mousemove, scroll) onto
    // animation frames while honouring a minimum emit interval. Returns a
    // function that accepts the latest event payload.
    function makeCoalescedEmitter(minInterval) {
        let pendingData = null;
        let lastEmitTime = 0;
        return (data) => {
            const wasNull = (pendingData === null);
            pendingData = data;
            if (!wasNull) {
                return;
            }
            requestAnimationFrame(() => {
                if (pendingData === null) {
                    return;
                }
                const now = performance.now();
                const elapsed = now - lastEmitTime;
                if (elapsed >= minInterval) {
                    emit(pendingData);
                    pendingData = null;
                    lastEmitTime = now;
                } else {
                    // Re-schedule after the remaining interval.
                    const d = pendingData;
                    pendingData = null;
                    setTimeout(() => {
                        lastEmitTime = performance.now();
                        emit(d);
                    }, minInterval - elapsed);
                }
            });
        };
    }

    /** Register a repeating timer subscription. */
    window.__gui_setup_timer = (id, interval) => {
        register(id, "timer", () => {
            const timer = setInterval(() => {
                emit({ type: "subscription", id });
            }, interval);
            return () => clearInterval(timer);
        });
    };

    /** Register a keyboard event subscription with an optional key filter. */
    window.__gui_setup_keyboard = (id, filter) => {
        register(id, "keyboard", () => {
            const handler = (e) => {
                let key = "";
                if (e.ctrlKey) { key += "Ctrl+"; }
                if (e.shiftKey) { key += "Shift+"; }
                if (e.altKey) { key += "Alt+"; }
                key += e.key;
                if (filter === "*" || key === filter || e.key === filter) {
                    e.preventDefault();
                    emit({ type: "keyboard", id, value: e.key });
                }
            };
            document.addEventListener("keydown", handler);
            return () => document.removeEventListener("keydown", handler);
        });
    };

    /** Register a window resize subscription (debounced via rAF). */
    window.__gui_setup_resize = (id) => {
        register(id, "resize", () => {
            let pending = false;
            const handler = () => {
                if (pending) { return; }
                pending = true;
                requestAnimationFrame(() => {
                    pending = false;
                    emit({
                        type: "resize",
                        id,
                        value: window.innerWidth + "," + window.innerHeight,
                    });
                });
            };
            window.addEventListener("resize", handler);
            return () => window.removeEventListener("resize", handler);
        });
    };

    /** Register focus/blur change subscription. */
    window.__gui_setup_focus = (id) => {
        register(id, "focus", () => {
            const focusHandler = () => {
                emit({ type: "focus_change", id, value: true });
            };
            const blurHandler = () => {
                emit({ type: "focus_change", id, value: false });
            };
            window.addEventListener("focus", focusHandler);
            window.addEventListener("blur", blurHandler);
            return () => {
                window.removeEventListener("focus", focusHandler);
                window.removeEventListener("blur", blurHandler);
            };
        });
    };

    /** Register a mouse/scroll event subscription with throttling. */
    window.__gui_setup_mouse = (id, eventType, throttleMs) => {
        register(id, "mouse", () => {
            const evtMap = {
                click: "click",
                move: "mousemove",
                down: "mousedown",
                up: "mouseup",
                scroll: "scroll",
            };
            const domEvt = evtMap[eventType] || eventType;
            const target = (domEvt === "scroll") ? window : document;
            const coalesce = (domEvt === "mousemove" || domEvt === "scroll");
            const minInterval =
                (typeof throttleMs === "number" && throttleMs > 0)
                    ? throttleMs
                    : 16;
            const pushCoalesced = coalesce
                ? makeCoalescedEmitter(minInterval)
                : null;
            const handler = (e) => {
                const data = {
                    type: "mouse_event",
                    id,
                    event: eventType,
                };
                if (e.clientX !== undefined) {
                    data.x = e.clientX;
                    data.y = e.clientY;
                }
                if (e.button !== undefined) {
                    data.button =
                        ["left", "middle", "right"][e.button] || "left";
                }
                if (domEvt === "scroll") {
                    data.x = window.scrollX;
                    data.y = window.scrollY;
                }
                data.ctrl = !!e.ctrlKey;
                data.shift = !!e.shiftKey;
                data.alt = !!e.altKey;
                if (pushCoalesced) {
                    pushCoalesced(data);
                } else {
                    emit(data);
                }
            };
            target.addEventListener(domEvt, handler);
            return () => target.removeEventListener(domEvt, handler);
        });
    };

    /** Register a document visibility change subscription. */
    window.__gui_setup_visibility = (id) => {
        register(id, "visibility", () => {
            const handler = () => {
                emit({
                    type: "visibility_change",
                    id,
                    value: !document.hidden,
                });
            };
            document.addEventListener("visibilitychange", handler);
            return () => document.removeEventListener("visibilitychange", handler);
        });
    };

    /** Internal helper for online/offline connectivity subscriptions. */
    function setupConnectivity(id, eventName, value) {
        register(id, eventName, () => {
            const handler = () => {
                emit({ type: eventName, id, value });
            };
            window.addEventListener(eventName, handler);
            return () => window.removeEventListener(eventName, handler);
        });
    }

    /** Register a network online event subscription. */
    window.__gui_setup_online = (id) => {
        setupConnectivity(id, "online", true);
    };

    /** Register a network offline event subscription. */
    window.__gui_setup_offline = (id) => {
        setupConnectivity(id, "offline", false);
    };

    /** Register a CSS media query change subscription. */
    window.__gui_setup_media_query = (id, query) => {
        register(id, "media_query", () => {
            const mql = window.matchMedia(query);
            const handler = (e) => {
                emit({ type: "media_query", id, value: e.matches });
            };
            mql.addEventListener("change", handler);
            // Emit initial state.
            emit({ type: "media_query", id, value: mql.matches });
            return () => mql.removeEventListener("change", handler);
        });
    };

    /** Remove a subscription by id and detach its event listeners. */
    window.__gui_remove_sub = (id) => {
        const sub = window.__gui_subs[id];
        if (!sub) { return; }
        if (sub.cleanup) { sub.cleanup(); }
        delete window.__gui_subs[id];
    };
})();
