// Binary download for VS Code (TypeScript) — a barrel module.
//
// One of three per-editor implementations of the shared download protocol.
// The cross-editor rationale (why there is no shared CLI downloader), the
// list of shared configuration files, and the per-editor differences all
// live in the single source of truth: extensions/shared/download-spec.md.
//
// ─── Module layout ────────────────────────────────────────────────
//
// This module is split into focused submodules under ./binary/:
//   - types.ts    — release/asset/config types and GitHub release parsing
//   - platform.ts — platform asset naming and bundled-binary path resolution
//   - archive.ts  — archive extraction (.zip / .tar.gz)
//   - resolve.ts  — download and the 4-step resolution order
//   - update.ts   — update checking and atomic replacement
//
// Unit tests live in src/test/suite/binary-download.test.ts (parsing, configs,
// platform mapping) and src/test/suite/checksum.test.ts (checksum parsing).
// The network/filesystem-heavy paths (downloadBinary, extractArchive) are
// exercised via the @vscode/test-electron integration suite.

export type { GithubAsset, GithubRelease, BinaryConfig } from "./binary/types";
export { LSP_CONFIG, DAP_CONFIG, parseGithubRelease } from "./binary/types";
export { getPlatformAssetName, getBinaryFilename, getBundledBinaryPath } from "./binary/platform";
export { extractArchive } from "./binary/archive";
export { downloadBinary, resolveBinaryCommand } from "./binary/resolve";
export { checkForBinaryUpdate } from "./binary/update";
export type { UpdateMode } from "./binary/update";
