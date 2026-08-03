# GraphicalUi Framework Tests

Unit tests for the first-party browser framework in the parent directory
(`gui-renderer.js`, `gui-charts.js`, `gui-subscriptions.js`). They run under
Node's built-in test runner with **no third-party dependencies and no browser**,
matching the zero-dependency `node --test` convention used elsewhere in the repo.

## Running

From the repository root:

```bash
node --test external/gui-framework/tests/*.test.mjs
```

Requires Node.js 20 or newer. The `ci-gui-framework.yml` workflow runs the same
command on every push and pull request that touches `external/gui-framework/`.

## Why a sandbox harness

The framework files are browser IIFEs: they attach functions to `window.*` and
close over module-private helpers (`sanitizeUrl`, `buildInlineStyle`,
`makeCoalescedEmitter`, …) that are never exported. They also read the DOM,
lit-html, and uPlot at load time, so they cannot simply be `import`ed under Node.

`gui-test-harness.mjs` solves this without touching the shipping source:

- **Fake environment** — `createEnvironment()` builds a minimal, controllable
  `window`/`document`, a fake lit-html, manually-driven fake timers
  (`requestAnimationFrame`/`setTimeout`/`setInterval`/`performance.now`), a
  `matchMedia` whose match state a test can flip, an emit recorder, and a
  listener registry. Test controls (`flushRaf`, `runDueTimers`, `tickIntervals`,
  `dispatch`, `setNow`, `advance`, `listenerCount`, `setMediaMatches`) let the
  tests drive coalescing and lifecycle behaviour deterministically.
- **Capture epilogue** — `loadFramework(fileName, { capture, globals })` reads
  the source, splices a capture call in front of the file's final `})();` (found
  via `lastIndexOf`), and runs it in a `node:vm` context. This exposes the named
  module-private bindings to the test **without modifying the file on disk**, so
  the compressed blob embedded into
  `core/runtime/stdlib/io/graphicalui_assets.hpp` stays byte-for-byte identical
  and no header regeneration is needed.
- **`plain(value)`** — deep-clones a value produced inside the sandbox into the
  test realm. Objects created in the vm carry that realm's prototypes, so
  `assert.deepStrictEqual` against a test-realm literal fails on a prototype
  mismatch even when the structure matches; round-tripping the (JSON-serialisable)
  return values through `plain()` normalises the realm.

This directory is ignored by both the asset generator
(`scripts/generate_gui_assets.mjs`) and the dev-asset loader
(`graphicalui_serialization.cpp`), which read explicit framework filenames — so
co-locating the tests here is safe.

## What is covered

| Suite                         | Focus                                                                                              |
| ----------------------------- | -------------------------------------------------------------------------------------------------- |
| `gui-renderer.test.mjs`       | Security-critical sanitisers (`sanitizeUrl`, `sanitizeCssValue`, `isSafeCssName`, `isSafePseudoSelector`), style/class/aria helpers, roving-focus index, and the theme/inject-CSS window API. |
| `gui-charts.test.mjs`         | Series construction, `withAlpha`, number formatting, chart-type labels, and the `describeChart` text alternative (WCAG 1.1.1). |
| `gui-subscriptions.test.mjs`  | The frame-coalescing emitter (immediate / latest-wins / throttle) and every subscription's setup, emit payload, and clean detach on removal. |

## Adding tests

Import the harness, `loadFramework` the file under test naming the internals to
`capture`, drive the sandbox with the environment's test controls, and wrap
sandbox return values in `plain()` before structural assertions:

```js
import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { loadFramework, plain } from "./gui-test-harness.mjs";

const { internals, window, env } = loadFramework("gui-renderer.js", {
    capture: ["sanitizeUrl", "GUI_LINK_SCHEMES"],
});

describe("sanitizeUrl", () => {
    it("blocks the javascript: scheme", () => {
        // sanitizeUrl takes the URL and the allow-list of permitted schemes.
        assert.equal(
            internals.sanitizeUrl("javascript:alert(1)", internals.GUI_LINK_SCHEMES),
            "",
        );
    });
});
```

New `*.test.mjs` files are picked up automatically by the CI command above.
