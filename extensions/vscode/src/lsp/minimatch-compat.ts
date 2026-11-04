// Loaded with `require` on purpose: esbuild resolves an ES `import` to
// minimatch's ESM build but vscode-languageclient's `require` to its CommonJS
// build, and the patch below must reach the class the client instantiates.
// eslint-disable-next-line @typescript-eslint/no-require-imports
import minimatch = require("minimatch");

type CompatMinimatch = minimatch.Minimatch & { makeRe?: () => boolean };

/**
 * Restores `Minimatch#makeRe()`, which minimatch 11 removed.
 *
 * package.json overrides minimatch to 11.x because every earlier release is
 * open to a pattern-amplification denial of service (GHSA-g3hv-75wg-m249).
 * vscode-languageclient 11.0.0 still targets minimatch 10 and calls `makeRe()`
 * only as a validity check before `match()` — for the relative-pattern
 * document selectors from ./selectors.ts and for file-operation registrations
 * — so without the method those calls throw a TypeError.
 *
 * minimatch 10's `makeRe()` returned `false` exactly when the pattern compiled
 * to an empty set (a `#` comment or an empty pattern) and a RegExp otherwise;
 * the client only tests the result for truthiness, so a boolean suffices.
 * Remove this once vscode-languageclient supports minimatch 11.
 */
export function installMinimatchCompat(): void {
    const prototype: CompatMinimatch = minimatch.Minimatch.prototype;
    if (typeof prototype.makeRe === "function") {
        return;
    }
    Object.defineProperty(prototype, "makeRe", {
        value: function makeRe(this: minimatch.Minimatch): boolean {
            return this.set.length > 0;
        },
        configurable: true,
        writable: true,
    });
}
