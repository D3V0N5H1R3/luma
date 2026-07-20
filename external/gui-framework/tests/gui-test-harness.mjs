/* Test harness for the GraphicalUi browser framework.
 *
 * The framework files (gui-renderer.js, gui-charts.js, gui-subscriptions.js)
 * are browser IIFEs: they attach functions to `window.*` and close over
 * module-private helpers that are never exported. They also depend on the DOM,
 * lit-html, and uPlot, so they cannot simply be `import`ed under Node.
 *
 * This harness loads a framework file inside a `node:vm` sandbox with a minimal,
 * controllable fake browser environment, and exposes the module-private helpers
 * for unit testing by splicing a capture epilogue in front of the file's final
 * `})();` — the source files themselves are never modified, so the compressed
 * blob embedded into `graphicalui_assets.hpp` stays byte-for-byte identical.
 *
 * The fake environment records emitted events and uses manually-driven fake
 * timers (requestAnimationFrame / setTimeout / setInterval / performance.now) so
 * tests can assert coalescing and throttling behaviour deterministically.
 *
 * SPDX-License-Identifier: MIT
 */

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const HERE = dirname(fileURLToPath(import.meta.url));
const FRAMEWORK_DIR = join(HERE, "..");

/**
 * Deep-clone a value produced inside the vm sandbox into a plain value owned by
 * the test realm. Objects/arrays created in the sandbox carry that realm's
 * prototypes, so `assert.deepStrictEqual` against test-realm literals fails on a
 * prototype mismatch even when the structure is identical. Round-tripping the
 * plain (JSON-serialisable) return values through JSON normalises the realm.
 */
export function plain(value) {
    return value === undefined ? undefined : JSON.parse(JSON.stringify(value));
}

// ── Minimal DOM element stub ──────────────────────────────
// Enough for the load-time listener registrations and the handful of functions
// under test that read/write element attributes and inline style properties.
function makeStyle() {
    const props = new Map();
    return {
        _props: props,
        setProperty(name, value) {
            props.set(name, String(value));
        },
        removeProperty(name) {
            props.delete(name);
        },
        getPropertyValue(name) {
            return props.has(name) ? props.get(name) : "";
        },
    };
}

function makeElement(tagName) {
    const attributes = new Map();
    return {
        tagName,
        style: makeStyle(),
        textContent: "",
        children: [],
        parentNode: null,
        attributes,
        get firstChild() {
            return this.children.length > 0 ? this.children[0] : null;
        },
        setAttribute(name, value) {
            attributes.set(name, String(value));
        },
        getAttribute(name) {
            return attributes.has(name) ? attributes.get(name) : null;
        },
        removeAttribute(name) {
            attributes.delete(name);
        },
        hasAttribute(name) {
            return attributes.has(name);
        },
        appendChild(child) {
            this.children.push(child);
            if (child) {
                child.parentNode = this;
            }
            return child;
        },
        removeChild(child) {
            const idx = this.children.indexOf(child);
            if (idx !== -1) {
                this.children.splice(idx, 1);
            }
            if (child) {
                child.parentNode = null;
            }
            return child;
        },
        remove() {
            this.removed = true;
            if (this.parentNode) {
                this.parentNode.removeChild(this);
            }
        },
        querySelector() {
            return null;
        },
        addEventListener() {},
        removeEventListener() {},
    };
}

/**
 * Build a fresh fake browser environment plus the manual clocks that drive it.
 * Returns the pieces tests need to observe and advance the sandbox, as well as
 * the timer/global functions the framework calls.
 */
export function createEnvironment() {
    const emitCalls = [];
    const rafQueue = [];
    const timerQueue = [];
    const intervals = new Map();
    // "target:type" -> array of handlers, so lifecycle tests can assert
    // that listeners are attached on setup and detached on cleanup.
    const listeners = new Map();
    let clock = 0;
    let nextTimerId = 1;

    const listenerKey = (target, type) => target + ":" + type;

    const addListener = (target) => (type, handler) => {
        const key = listenerKey(target, type);
        const list = listeners.get(key) || [];
        list.push(handler);
        listeners.set(key, list);
    };

    const removeListener = (target) => (type, handler) => {
        const list = listeners.get(listenerKey(target, type));
        if (!list) {
            return;
        }
        const idx = list.indexOf(handler);
        if (idx !== -1) {
            list.splice(idx, 1);
        }
    };

    const documentElement = makeElement("html");
    const body = makeElement("body");
    const head = makeElement("head");

    const document = {
        documentElement,
        body,
        head,
        hidden: false,
        addEventListener: addListener("document"),
        removeEventListener: removeListener("document"),
        createElement: (tag) => makeElement(tag),
        getElementById: () => null,
        querySelector: () => null,
    };

    // matchMedia returns a MediaQueryList whose `matches` a test can flip, with
    // its own listener registry keyed by query string.
    const mediaState = { matches: false };
    const matchMedia = (query) => ({
        media: query,
        get matches() {
            return mediaState.matches;
        },
        addEventListener: addListener("mql:" + query),
        removeEventListener: removeListener("mql:" + query),
    });

    const window = {
        innerWidth: 1024,
        innerHeight: 768,
        scrollX: 0,
        scrollY: 0,
        litHtml: {
            html: (strings, ...values) => ({ kind: "html", strings, values }),
            svg: (strings, ...values) => ({ kind: "svg", strings, values }),
            render: () => {},
            nothing: Symbol("nothing"),
        },
        // The renderer sets this itself; subscriptions read it at load, so seed
        // a recorder in case only the subscriptions file is loaded.
        __gui_emit: (payload) => emitCalls.push(payload),
        __gui_event: (json) => emitCalls.push(JSON.parse(json)),
        matchMedia,
        addEventListener: addListener("window"),
        removeEventListener: removeListener("window"),
    };

    const requestAnimationFrame = (cb) => {
        rafQueue.push(cb);
        return rafQueue.length;
    };
    const setTimeoutFn = (cb, ms) => {
        const id = nextTimerId++;
        timerQueue.push({ id, cb, at: clock + (Number(ms) || 0) });
        return id;
    };
    const clearTimeoutFn = (id) => {
        const idx = timerQueue.findIndex((t) => t.id === id);
        if (idx !== -1) {
            timerQueue.splice(idx, 1);
        }
    };
    const setIntervalFn = (cb, ms) => {
        const id = nextTimerId++;
        intervals.set(id, { cb, ms });
        return id;
    };
    const clearIntervalFn = (id) => {
        intervals.delete(id);
    };
    const performance = { now: () => clock };

    return {
        // Observable state.
        emitCalls,
        listeners,
        intervals,
        window,
        document,

        // Sandbox globals the framework code calls.
        globals: {
            window,
            document,
            requestAnimationFrame,
            setTimeout: setTimeoutFn,
            clearTimeout: clearTimeoutFn,
            setInterval: setIntervalFn,
            clearInterval: clearIntervalFn,
            performance,
            console,
        },

        // ── Test controls ─────────────────────────────
        /** Set the fake wall clock (drives performance.now and timer due-times). */
        setNow(ms) {
            clock = ms;
        },
        /** Advance the fake wall clock by a delta. */
        advance(ms) {
            clock += ms;
        },
        /** Run and drain all queued animation-frame callbacks (FIFO). */
        flushRaf() {
            const pending = rafQueue.splice(0, rafQueue.length);
            for (const cb of pending) {
                cb(clock);
            }
        },
        /** Number of animation-frame callbacks currently queued. */
        rafPending() {
            return rafQueue.length;
        },
        /** Run all setTimeout callbacks whose due-time has been reached. */
        runDueTimers() {
            const due = timerQueue.filter((t) => t.at <= clock);
            for (const t of due) {
                const idx = timerQueue.indexOf(t);
                if (idx !== -1) {
                    timerQueue.splice(idx, 1);
                }
                t.cb();
            }
        },
        /** Fire every registered interval callback once. */
        tickIntervals() {
            for (const { cb } of intervals.values()) {
                cb();
            }
        },
        /** Invoke every handler registered for a target/event pair. */
        dispatch(target, type, event) {
            const list = listeners.get(listenerKey(target, type)) || [];
            for (const handler of list.slice()) {
                handler(event);
            }
        },
        /** Count listeners currently attached for a target/event pair. */
        listenerCount(target, type) {
            const list = listeners.get(listenerKey(target, type));
            return list ? list.length : 0;
        },
        /** Flip the media-query match state shared by matchMedia consumers. */
        setMediaMatches(value) {
            mediaState.matches = value;
        },
    };
}

/**
 * Load a framework file into a fresh sandbox and capture the named
 * module-private internals.
 *
 * @param {string} fileName  File under external/gui-framework/, e.g. "gui-renderer.js".
 * @param {object} [options]
 * @param {string[]} [options.capture]  Module-private bindings to expose to tests.
 * @param {object} [options.globals]  Extra sandbox globals (e.g. { uPlot }).
 * @returns {{ env: object, internals: object, window: object, document: object }}
 */
export function loadFramework(fileName, options = {}) {
    const { capture = [], globals = {} } = options;
    const source = readFileSync(join(FRAMEWORK_DIR, fileName), "utf8");

    // Splice the capture call in front of the outer IIFE's closing `})();`,
    // which is always the last such token in the file. The `typeof` guard keeps
    // a renamed/removed binding from aborting the whole load — the test then
    // sees `undefined` and fails with a clear assertion instead.
    const closeIdx = source.lastIndexOf("})();");
    if (closeIdx === -1) {
        throw new Error(`Could not find IIFE close in ${fileName}`);
    }
    const captureObj = capture
        .map(
            (name) =>
                `${JSON.stringify(name)}: (typeof ${name} !== "undefined" ? ${name} : undefined)`,
        )
        .join(", ");
    const epilogue = `\n;globalThis.__gui_capture({ ${captureObj} });\n`;
    const patched = source.slice(0, closeIdx) + epilogue + source.slice(closeIdx);

    const env = createEnvironment();
    const captured = {};

    const sandbox = {
        ...env.globals,
        __gui_capture: (obj) => Object.assign(captured, obj),
        ...globals,
    };
    // The framework references window/document/uPlot as free globals; make the
    // sandbox its own global object so those resolve.
    sandbox.globalThis = sandbox;

    vm.createContext(sandbox);
    vm.runInContext(patched, sandbox, { filename: fileName });

    return {
        env,
        internals: captured,
        window: env.window,
        document: env.document,
    };
}
