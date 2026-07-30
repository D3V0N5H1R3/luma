/* Unit tests for gui-subscriptions.js.
 *
 * Covers the frame-coalescing emitter (the trickiest logic — immediate emit,
 * latest-wins coalescing, and interval throttling) and the setup/remove
 * lifecycle of every subscription kind: that each registers exactly one
 * listener, emits the right payload, and detaches cleanly on removal.
 *
 * SPDX-License-Identifier: MIT
 */

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { loadFramework, plain } from "./gui-test-harness.mjs";

/** Load a fresh sandbox so each test gets isolated emit/subscription state. */
function fresh() {
    return loadFramework("gui-subscriptions.js", {
        capture: ["makeCoalescedEmitter"],
    });
}

describe("makeCoalescedEmitter", () => {
    it("defers the first payload to an animation frame, then emits it", () => {
        const { internals, env } = fresh();
        env.setNow(1000);
        const push = internals.makeCoalescedEmitter(16);

        push({ v: 1 });
        assert.equal(env.rafPending(), 1);
        assert.equal(env.emitCalls.length, 0);

        env.flushRaf();
        assert.equal(env.emitCalls.length, 1);
        assert.deepEqual(plain(env.emitCalls[0]), { v: 1 });
    });

    it("coalesces multiple payloads within a frame down to the latest", () => {
        const { internals, env } = fresh();
        env.setNow(1000);
        const push = internals.makeCoalescedEmitter(16);

        push({ v: 1 });
        push({ v: 2 });
        push({ v: 3 });
        assert.equal(env.rafPending(), 1);

        env.flushRaf();
        assert.equal(env.emitCalls.length, 1);
        assert.deepEqual(plain(env.emitCalls[0]), { v: 3 });
    });

    it("throttles a follow-up payload via setTimeout when inside the interval", () => {
        const { internals, env } = fresh();
        const push = internals.makeCoalescedEmitter(100);

        // First payload emits immediately on the frame (elapsed >= interval).
        env.setNow(1000);
        push({ v: 1 });
        env.flushRaf();
        assert.equal(env.emitCalls.length, 1);

        // Second payload arrives 50ms later — inside the 100ms window, so the
        // frame schedules a timer instead of emitting.
        env.setNow(1050);
        push({ v: 2 });
        env.flushRaf();
        assert.equal(env.emitCalls.length, 1);

        // The timer becomes due at 1050 + (100 - 50) = 1100.
        env.setNow(1100);
        env.runDueTimers();
        assert.equal(env.emitCalls.length, 2);
        assert.deepEqual(plain(env.emitCalls[1]), { v: 2 });
    });
});

describe("__gui_setup_timer", () => {
    it("registers a repeating timer that emits a subscription tick", () => {
        const { window, env } = fresh();
        window.__gui_setup_timer("t1", 1000);

        assert.equal(window.__gui_subs.t1.type, "timer");
        assert.equal(env.intervals.size, 1);

        env.tickIntervals();
        assert.deepEqual(plain(env.emitCalls[0]), { type: "subscription", id: "t1" });
    });

    it("replaces an existing subscription with the same id", () => {
        const { window, env } = fresh();
        window.__gui_setup_timer("t1", 1000);
        window.__gui_setup_timer("t1", 500);

        assert.equal(env.intervals.size, 1);
    });

    it("clears the interval on removal", () => {
        const { window, env } = fresh();
        window.__gui_setup_timer("t1", 1000);
        window.__gui_remove_sub("t1");

        assert.equal(env.intervals.size, 0);
        assert.equal(window.__gui_subs.t1, undefined);
    });
});

describe("__gui_setup_keyboard", () => {
    it("emits only for the matching key and prevents its default", () => {
        const { window, env } = fresh();
        window.__gui_setup_keyboard("k1", "a");
        assert.equal(env.listenerCount("document", "keydown"), 1);

        let prevented = 0;
        env.dispatch("document", "keydown", {
            key: "a",
            preventDefault() { prevented += 1; },
        });
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "keyboard",
            id: "k1",
            value: "a",
            ctrl: false,
            shift: false,
            alt: false,
            meta: false,
        });
        assert.equal(prevented, 1);

        env.dispatch("document", "keydown", { key: "b", preventDefault() {} });
        assert.equal(env.emitCalls.length, 1);
    });

    it("matches a modifier combination filter", () => {
        const { window, env } = fresh();
        window.__gui_setup_keyboard("k2", "Ctrl+s");

        env.dispatch("document", "keydown", {
            key: "s",
            ctrlKey: true,
            preventDefault() {},
        });
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "keyboard",
            id: "k2",
            value: "s",
            ctrl: true,
            shift: false,
            alt: false,
            meta: false,
        });
    });

    it("matches any key with the wildcard filter and detaches on removal", () => {
        const { window, env } = fresh();
        window.__gui_setup_keyboard("k3", "*");

        env.dispatch("document", "keydown", { key: "z", preventDefault() {} });
        assert.equal(env.emitCalls.length, 1);

        window.__gui_remove_sub("k3");
        assert.equal(env.listenerCount("document", "keydown"), 0);
    });
});

describe("__gui_setup_mouse", () => {
    it("emits click events immediately with position and modifiers", () => {
        const { window, env } = fresh();
        window.__gui_setup_mouse("m1", "click", 0);
        assert.equal(env.listenerCount("document", "click"), 1);

        env.dispatch("document", "click", {
            clientX: 10,
            clientY: 20,
            button: 0,
            ctrlKey: false,
            shiftKey: true,
            altKey: false,
        });
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "mouse_event",
            id: "m1",
            event: "click",
            x: 10,
            y: 20,
            button: "left",
            ctrl: false,
            shift: true,
            alt: false,
        });

        window.__gui_remove_sub("m1");
        assert.equal(env.listenerCount("document", "click"), 0);
    });

    it("coalesces mousemove events onto a frame, keeping the latest position", () => {
        const { window, env } = fresh();
        window.__gui_setup_mouse("m2", "move", 50);
        assert.equal(env.listenerCount("document", "mousemove"), 1);

        env.setNow(1000);
        env.dispatch("document", "mousemove", { clientX: 1, clientY: 1 });
        env.dispatch("document", "mousemove", { clientX: 2, clientY: 2 });
        assert.equal(env.rafPending(), 1);

        env.flushRaf();
        assert.equal(env.emitCalls.length, 1);
        assert.equal(env.emitCalls[0].x, 2);
        assert.equal(env.emitCalls[0].y, 2);
    });

    it("binds scroll subscriptions to the window target", () => {
        const { window, env } = fresh();
        window.__gui_setup_mouse("m3", "scroll", 0);
        assert.equal(env.listenerCount("document", "scroll"), 0);
        assert.equal(env.listenerCount("window", "scroll"), 1);
    });
});

describe("__gui_setup_drag", () => {
    it("emits a non-move phase immediately with position and phase name", () => {
        const { window, env } = fresh();
        window.__gui_setup_drag("d1", "*");
        assert.equal(env.listenerCount("document", "dragstart"), 1);
        assert.equal(env.listenerCount("document", "dragover"), 1);

        env.dispatch("document", "dragstart", { clientX: 5, clientY: 8 });
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "drag_event",
            id: "d1",
            event: "start",
            x: 5,
            y: 8,
            data: "",
        });

        window.__gui_remove_sub("d1");
        assert.equal(env.listenerCount("document", "dragstart"), 0);
        assert.equal(env.listenerCount("document", "dragover"), 0);
    });

    it("reads the dropped text/plain data and prevents the drop default", () => {
        const { window, env } = fresh();
        window.__gui_setup_drag("d2", "*");

        let prevented = 0;
        env.dispatch("document", "drop", {
            clientX: 12,
            clientY: 3,
            preventDefault() { prevented += 1; },
            dataTransfer: { getData: (t) => (t === "text/plain" ? "card-9" : "") },
        });
        assert.equal(prevented, 1);
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "drag_event",
            id: "d2",
            event: "drop",
            x: 12,
            y: 3,
            data: "card-9",
        });
    });

    it("filters to a single phase and ignores the others", () => {
        const { window, env } = fresh();
        window.__gui_setup_drag("d3", "end");

        env.dispatch("document", "dragstart", { clientX: 1, clientY: 1 });
        assert.equal(env.emitCalls.length, 0);

        env.dispatch("document", "dragend", { clientX: 2, clientY: 4 });
        assert.equal(env.emitCalls.length, 1);
        assert.equal(env.emitCalls[0].event, "end");
    });

    it("coalesces the high-frequency move phase onto a frame", () => {
        const { window, env } = fresh();
        window.__gui_setup_drag("d4", "*");

        env.setNow(1000);
        env.dispatch("document", "drag", { clientX: 1, clientY: 1 });
        env.dispatch("document", "drag", { clientX: 7, clientY: 9 });
        assert.equal(env.emitCalls.length, 0);
        assert.equal(env.rafPending(), 1);

        env.flushRaf();
        assert.equal(env.emitCalls.length, 1);
        assert.equal(env.emitCalls[0].event, "move");
        assert.equal(env.emitCalls[0].x, 7);
        assert.equal(env.emitCalls[0].y, 9);
    });
});

describe("__gui_setup_storage", () => {
    it("forwards a matching key change with old and new values", () => {
        const { window, env } = fresh();
        window.__gui_setup_storage("s1", "theme");
        assert.equal(env.listenerCount("window", "storage"), 1);

        env.dispatch("window", "storage", {
            key: "theme",
            oldValue: "light",
            newValue: "dark",
        });
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "storage_change",
            id: "s1",
            key: "theme",
            oldValue: "light",
            newValue: "dark",
        });

        window.__gui_remove_sub("s1");
        assert.equal(env.listenerCount("window", "storage"), 0);
    });

    it("omits null old/new values so they map to none", () => {
        const { window, env } = fresh();
        window.__gui_setup_storage("s2", "token");

        env.dispatch("window", "storage", {
            key: "token",
            oldValue: null,
            newValue: "abc",
        });
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "storage_change",
            id: "s2",
            key: "token",
            newValue: "abc",
        });
    });

    it("ignores changes to other keys unless the filter is empty", () => {
        const { window, env } = fresh();
        window.__gui_setup_storage("s3", "theme");

        env.dispatch("window", "storage", { key: "other", oldValue: "1", newValue: "2" });
        assert.equal(env.emitCalls.length, 0);

        // A storage.clear() nulls e.key and is always forwarded.
        env.dispatch("window", "storage", { key: null, oldValue: null, newValue: null });
        assert.equal(env.emitCalls.length, 1);
        assert.equal(env.emitCalls[0].key, "");
    });

    it("matches every key when the filter is empty", () => {
        const { window, env } = fresh();
        window.__gui_setup_storage("s4", "");

        env.dispatch("window", "storage", { key: "anything", oldValue: "a", newValue: "b" });
        assert.equal(env.emitCalls.length, 1);
        assert.equal(env.emitCalls[0].key, "anything");
    });
});

describe("__gui_setup_wheel", () => {
    it("coalesces wheel deltas onto a frame, keeping the latest delta", () => {
        const { window, env } = fresh();
        window.__gui_setup_wheel("w1");
        assert.equal(env.listenerCount("window", "wheel"), 1);

        env.setNow(1000);
        env.dispatch("window", "wheel", { deltaX: 1, deltaY: 2 });
        env.dispatch("window", "wheel", { deltaX: 10, deltaY: 20 });
        assert.equal(env.emitCalls.length, 0);
        assert.equal(env.rafPending(), 1);

        env.flushRaf();
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "wheel_event",
            id: "w1",
            deltaX: 10,
            deltaY: 20,
        });

        window.__gui_remove_sub("w1");
        assert.equal(env.listenerCount("window", "wheel"), 0);
    });
});

describe("__gui_setup_resize", () => {
    it("debounces resize onto a frame and reports the window dimensions", () => {
        const { window, env } = fresh();
        window.__gui_setup_resize("r1");
        assert.equal(env.listenerCount("window", "resize"), 1);

        env.dispatch("window", "resize", {});
        env.dispatch("window", "resize", {});
        assert.equal(env.rafPending(), 1);

        env.flushRaf();
        assert.equal(env.emitCalls.length, 1);
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "resize",
            id: "r1",
            value: "1024,768",
        });
    });
});

describe("__gui_setup_focus", () => {
    it("emits focus and blur changes and detaches both on removal", () => {
        const { window, env } = fresh();
        window.__gui_setup_focus("f1");
        assert.equal(env.listenerCount("window", "focus"), 1);
        assert.equal(env.listenerCount("window", "blur"), 1);

        env.dispatch("window", "focus", {});
        env.dispatch("window", "blur", {});
        assert.deepEqual(plain(env.emitCalls), [
            { type: "focus_change", id: "f1", value: true },
            { type: "focus_change", id: "f1", value: false },
        ]);

        window.__gui_remove_sub("f1");
        assert.equal(env.listenerCount("window", "focus"), 0);
        assert.equal(env.listenerCount("window", "blur"), 0);
    });
});

describe("__gui_setup_visibility", () => {
    it("emits the inverse of document.hidden on visibility change", () => {
        const { window, env } = fresh();
        window.__gui_setup_visibility("v1");
        assert.equal(env.listenerCount("document", "visibilitychange"), 1);

        env.dispatch("document", "visibilitychange", {});
        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "visibility_change",
            id: "v1",
            value: true,
        });
    });
});

describe("__gui_setup_online / __gui_setup_offline", () => {
    it("emits a fixed value for online and offline events", () => {
        const { window, env } = fresh();
        window.__gui_setup_online("on1");
        window.__gui_setup_offline("off1");

        env.dispatch("window", "online", {});
        env.dispatch("window", "offline", {});
        assert.deepEqual(plain(env.emitCalls), [
            { type: "online", id: "on1", value: true },
            { type: "offline", id: "off1", value: false },
        ]);
    });
});

describe("__gui_setup_media_query", () => {
    it("emits the initial match state and subsequent changes", () => {
        const { window, env } = fresh();
        const query = "(max-width: 600px)";
        window.__gui_setup_media_query("mq1", query);

        assert.deepEqual(plain(env.emitCalls[0]), {
            type: "media_query",
            id: "mq1",
            value: false,
        });

        env.setMediaMatches(true);
        env.dispatch("mql:" + query, "change", { matches: true });
        assert.deepEqual(plain(env.emitCalls[1]), {
            type: "media_query",
            id: "mq1",
            value: true,
        });

        window.__gui_remove_sub("mq1");
        assert.equal(env.listenerCount("mql:" + query, "change"), 0);
    });
});

describe("__gui_remove_sub", () => {
    it("is a no-op for an unknown subscription id", () => {
        const { window } = fresh();
        assert.doesNotThrow(() => window.__gui_remove_sub("does-not-exist"));
    });
});
