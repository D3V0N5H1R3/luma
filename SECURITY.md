# Security Policy

Luma is in active alpha development. We take the security of the interpreter, its tooling, and the standard library seriously, and we are grateful to the researchers who help keep Luma safe.

---

## Table of Contents

1. [Supported Versions](#supported-versions)
2. [Reporting a Vulnerability](#reporting-a-vulnerability)
3. [Disclosure Policy](#disclosure-policy)
4. [Security Model](#security-model)
5. [Scope](#scope)
6. [Known Limitations](#known-limitations)

---

## Supported Versions

Luma has not yet reached a stable 1.0 release. Security fixes are applied only to the latest published minor release; earlier versions receive no updates. When a fix ships, please upgrade promptly.

| Version | Supported |
| ------- | --------- |
| 0.10.x   | ✓         |
| < 0.10   | ✗         |

---

## Reporting a Vulnerability

If you discover a security vulnerability in Luma, **please do not open a public GitHub issue**, post it in discussions, or disclose it publicly before it has been resolved.

Instead, report it privately through GitHub's [private vulnerability reporting](https://github.com/d3v0n5h1r3/luma/security/advisories/new): open the repository's **Security** tab and click **Report a vulnerability**. If you cannot use that channel, email the maintainers at the address listed on the [GitHub repository](https://github.com/d3v0n5h1r3/luma).

Please include as much of the following as you can:

1. A description of the vulnerability and its potential impact.
2. Steps to reproduce (a minimal Luma program or C++ test case if applicable).
3. The affected Luma version (`luma --version`), operating system, and compiler.
4. Any proof-of-concept code, logs, or stack traces.
5. Your assessment of the severity and whether the issue is being actively exploited.

Report each distinct vulnerability separately so that each can be tracked and fixed independently.

---

## Disclosure Policy

We follow a coordinated disclosure process and aim to meet the following targets:

| Stage                                          | Target          |
| ---------------------------------------------- | --------------- |
| Acknowledge your report                        | Within 72 hours |
| Confirm or decline, with an initial assessment | Within 7 days   |
| Release a fix for a confirmed vulnerability    | Within 14 days  |

These are best-effort targets for a volunteer-maintained alpha project; complex issues may take longer, and we will keep you updated on progress.

When a fix is released, we publish a [GitHub Security Advisory](https://github.com/d3v0n5h1r3/luma/security/advisories) describing the issue and the affected versions. With your permission we credit you for the discovery — let us know if you prefer to remain anonymous.

**Safe harbour.** We will not pursue or support legal action against anyone who reports a vulnerability in good faith, who avoids privacy violations and disruption of service, and who does not access or modify data beyond what is necessary to demonstrate the issue. Please give us a reasonable opportunity to resolve the problem before disclosing it publicly.

---

## Security Model

Luma is designed to execute untrusted source files in **sandbox mode** (`--box` / `-b`). The sandbox is Luma's primary trust boundary: it disables every standard library module that touches the operating system, together with the individual file-I/O functions in otherwise-safe modules, so that a sandboxed program can perform only pure computation and terminal output.

A defect that lets sandboxed code reach the filesystem, network, or other processes is therefore the most serious class of vulnerability in Luma. Outside the sandbox, the interpreter still validates inputs at every OS boundary — rejecting path traversal, CRLF header injection, and server-side request forgery, and enforcing resource limits.

For the authoritative description of these boundaries, see [Sandbox Mode][sandbox] and [Security Boundaries][boundaries] in the architecture documentation.

[sandbox]: documents/Luma_Software_Architecture.md#115-sandbox-mode
[boundaries]: documents/Luma_Software_Architecture.md#114-security-boundaries

---

## Scope

The following are in scope:

- The `luma` interpreter binary (all phases: lexer, parser, include resolver, type checker, linter, compiler, VM).
- The `luma_dap` debugger binary (DAP protocol handling, debug session management).
- The `luma_lsp` language server binary.
- The standard library (`core/runtime/stdlib/`), especially modules with OS access:
  `Console`, `Csv`, `FileSystem`, `Http`, `KeyValueStore`, `Process`, `Socket`, `Xml`.
- Individually sandboxed functions in otherwise-safe modules:
  `Compression.gzip_file`, `Compression.gunzip_file`, `Hash.sha256_file`, `Hash.sha512_file`, `Log.set_output`.
- Sandbox mode (`--box`) bypass — any code path that allows sandboxed functions to access the filesystem, network, or processes must be reported.

The following are **out of scope**:

- Bugs in example programs (`examples/`) that do not affect the interpreter itself.
- Editor extensions (`extensions/`) unless they expose the LSP server insecurely.

---

## Known Limitations

### Zed Extension Binary Download

The Zed extension downloads LSP and DAP binaries from GitHub releases over
HTTPS but **does not verify checksums**. This is because the Zed WASM
extension sandbox does not provide cryptographic primitives or raw HTTP
access needed for checksum verification.

HTTPS provides transport-layer integrity, which mitigates most tampering
risks. The VS Code extension performs SHA-256 checksum verification after
download.
