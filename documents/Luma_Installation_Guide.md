# Luma — Installation Guide

> Install pre-built Luma binaries and set up editor integration on Windows, macOS, or Linux.

---

## Table of Contents

1. [Overview](#1--overview)
2. [Download](#2--download)
3. [Verify Checksums](#3--verify-checksums)
4. [Windows](#4--windows)
5. [macOS](#5--macos)
6. [Linux](#6--linux)
7. [Editor Setup — Visual Studio Code](#7--editor-setup--visual-studio-code)
8. [Editor Setup — Zed](#8--editor-setup--zed)
9. [Verify the Installation](#9--verify-the-installation)
10. [Building from Source](#10--building-from-source)
11. [Troubleshooting](#11--troubleshooting)
12. [See Also](#see-also)

---

## 1 — Overview

A Luma installation consists of three binaries and an editor extension:

| Binary     | Purpose                                            |
| ---------- | -------------------------------------------------- |
| `luma`     | The interpreter — runs, tests, and type-checks programs. |
| `luma_lsp` | Language server — provides diagnostics, completions, hover, go-to-definition, and more. |
| `luma_dap` | Debug adapter — provides breakpoints, stepping, variable inspection, and watch expressions. |

The binaries are standalone executables with no runtime dependencies. Download them from a GitHub release, place them in a directory on your `PATH`, and point your editor at them.

---

## 2 — Download

Download the archives and the checksum file from the [latest GitHub release](https://github.com/d3v0n5h1r3/luma/releases/latest). Pick the assets for your platform:

| Platform        | Interpreter                 | Language Server                  | Debug Adapter                  |
| --------------- | --------------------------- | -------------------------------- | ------------------------------ |
| Windows x86\_64 | `luma-windows-x86_64.zip`   | `luma_lsp-windows-x86_64.zip`   | `luma_dap-windows-x86_64.zip`  |
| macOS aarch64   | `luma-macos-aarch64.tar.gz` | `luma_lsp-macos-aarch64.tar.gz` | `luma_dap-macos-aarch64.tar.gz`|
| Linux x86\_64   | `luma-linux-x86_64.tar.gz`  | `luma_lsp-linux-x86_64.tar.gz`  | `luma_dap-linux-x86_64.tar.gz` |
| Linux aarch64   | `luma-linux-aarch64.tar.gz` | `luma_lsp-linux-aarch64.tar.gz` | `luma_dap-linux-aarch64.tar.gz`|

Also download:

- **`SHA256SUMS`** — checksum file for verifying archive integrity.
- **`luma-language-0.5.0.vsix`** — VS Code extension (if using VS Code).

---

## 3 — Verify Checksums

Always verify downloaded archives before extracting them.

### Windows (PowerShell)

```powershell
Get-Content SHA256SUMS | ForEach-Object {
    $parts = $_ -split '\s+'
    $expected = $parts[0]
    $file = $parts[1] -replace '^\*', ''
    if (Test-Path $file) {
        $actual = (Get-FileHash $file -Algorithm SHA256).Hash
        if ($actual -eq $expected) { Write-Host "OK   $file" }
        else { Write-Host "FAIL $file" -ForegroundColor Red }
    }
}
```

### macOS / Linux

```bash
sha256sum --check SHA256SUMS --ignore-missing
```

Every line should show `OK`. If any line shows `FAIL`, re-download the corresponding file.

---

## 4 — Windows

### 4.1 — Extract the Archives

Create an installation directory (for example, `C:\Program Files\Luma`) and extract the three ZIP archives into it:

```powershell
New-Item -ItemType Directory -Path "C:\Program Files\Luma" -Force

Expand-Archive luma-windows-x86_64.zip     -DestinationPath "C:\Program Files\Luma" -Force
Expand-Archive luma_lsp-windows-x86_64.zip -DestinationPath "C:\Program Files\Luma" -Force
Expand-Archive luma_dap-windows-x86_64.zip -DestinationPath "C:\Program Files\Luma" -Force
```

You should now have three executables in `C:\Program Files\Luma\`:

- `luma.exe`
- `luma_lsp.exe`
- `luma_dap.exe`

### 4.2 — Add to PATH

Add the installation directory to your user `PATH` so that the binaries are available in every terminal and in VS Code:

```powershell
$current = [Environment]::GetEnvironmentVariable("Path", "User")
[Environment]::SetEnvironmentVariable("Path", "$current;C:\Program Files\Luma", "User")
```

Restart any open terminals (and VS Code) for the change to take effect. Verify with:

```powershell
luma --version
```

---

## 5 — macOS

### 5.1 — Extract the Archives

Create an installation directory and extract the three tarballs:

```bash
sudo mkdir -p /usr/local/bin

# Extract each archive (each contains a single binary)
sudo tar xzf luma-macos-aarch64.tar.gz     -C /usr/local/bin
sudo tar xzf luma_lsp-macos-aarch64.tar.gz -C /usr/local/bin
sudo tar xzf luma_dap-macos-aarch64.tar.gz -C /usr/local/bin
```

> **Note:** Pre-built macOS binaries are provided for Apple Silicon (aarch64) only. On an Intel Mac, [build from source](#10--building-from-source) or run the aarch64 binaries under Rosetta 2.

You should now have three binaries in `/usr/local/bin/`:

- `luma`
- `luma_lsp`
- `luma_dap`

### 5.2 — Remove the Quarantine Attribute

macOS Gatekeeper quarantines downloaded binaries. Remove the attribute so the binaries can run:

```bash
xattr -d com.apple.quarantine /usr/local/bin/luma /usr/local/bin/luma_lsp /usr/local/bin/luma_dap
```

### 5.3 — Verify

`/usr/local/bin` is on the default `PATH` for most shells. Verify with:

```bash
luma --version
```

If you chose a different directory, add it to your shell profile (`~/.zshrc` or `~/.bash_profile`):

```bash
export PATH="$PATH:/your/install/directory"
```

Then reload your shell (`source ~/.zshrc`) or open a new terminal.

---

## 6 — Linux

### 6.1 — Extract the Archives

Create an installation directory and extract the three tarballs:

```bash
sudo mkdir -p /usr/local/bin

# Extract each archive (each contains a single binary)
sudo tar xzf luma-linux-x86_64.tar.gz     -C /usr/local/bin
sudo tar xzf luma_lsp-linux-x86_64.tar.gz -C /usr/local/bin
sudo tar xzf luma_dap-linux-x86_64.tar.gz -C /usr/local/bin
```

> **Note:** Replace `x86_64` with `aarch64` if you are on an ARM64 system (for example, a Raspberry Pi running a 64-bit OS).

You should now have three binaries in `/usr/local/bin/`:

- `luma`
- `luma_lsp`
- `luma_dap`

### 6.2 — Ensure the Binaries Are Executable

The tarballs preserve file permissions, but if needed:

```bash
sudo chmod +x /usr/local/bin/luma /usr/local/bin/luma_lsp /usr/local/bin/luma_dap
```

### 6.3 — Verify

`/usr/local/bin` is on the default `PATH` for most distributions. Verify with:

```bash
luma --version
```

If you chose a different directory (for example, `~/.local/bin` for a user-local install), ensure it is on your `PATH`:

```bash
export PATH="$PATH:$HOME/.local/bin"
```

Add the line to `~/.bashrc`, `~/.zshrc`, or `~/.profile` to make it permanent.

---

## 7 — Editor Setup — Visual Studio Code

### 7.1 — Install the Extension

Install the extension from the `.vsix` file:

```bash
code --install-extension luma-language-0.5.0.vsix
```

Alternatively, in VS Code: open the Command Palette (`Ctrl+Shift+P`), select **Extensions: Install from VSIX…**, and choose the `.vsix` file.

### 7.2 — Configure Binary Paths

If you added the binaries to your `PATH` (steps above), the extension finds them automatically — no additional configuration is needed.

To use explicit paths instead, open VS Code Settings (`Ctrl+,` / `Cmd+,`) and set:

| Setting        | Description                                                 |
| -------------- | ----------------------------------------------------------- |
| `luma.path`    | Absolute path to the `luma` binary (interpreter).           |
| `luma.lsp.path`| Absolute path to the `luma_lsp` binary (language server).   |
| `luma.dap.path`| Absolute path to the `luma_dap` binary (debug adapter).     |

Or add these entries directly to your `settings.json` (`Ctrl+Shift+P` → **Preferences: Open User Settings (JSON)**):

**Windows example:**

```json
{
    "luma.path": "C:\\Program Files\\Luma\\luma.exe",
    "luma.lsp.path": "C:\\Program Files\\Luma\\luma_lsp.exe",
    "luma.dap.path": "C:\\Program Files\\Luma\\luma_dap.exe"
}
```

**macOS / Linux example:**

```json
{
    "luma.path": "/usr/local/bin/luma",
    "luma.lsp.path": "/usr/local/bin/luma_lsp",
    "luma.dap.path": "/usr/local/bin/luma_dap"
}
```

### 7.3 — Reload VS Code

Restart VS Code, or run **Developer: Reload Window** from the Command Palette.

### 7.4 — Auto-Update

By default, the extension checks for new `luma_lsp` and `luma_dap` releases on activation. To disable this (for example, to pin a specific version), set `luma.lsp.autoUpdate` to `false` in your settings.

---

## 8 — Editor Setup — Zed

The Luma extension for Zed is available from the Zed extension gallery:

1. Open the extensions panel (`zed: extensions` command).
2. Search for **Luma** and install it.

The extension automatically downloads `luma_lsp` and `luma_dap` from GitHub Releases on first activation. If you prefer to use local binaries, place them on your `PATH`.

---

## 9 — Verify the Installation

Create a file called `hello.luma` with the following content:

```luma
@main
function void main() {
    print("Hello from Luma!")
}
```

### Run from the Terminal

```bash
luma hello.luma
```

Expected output:

```text
Hello from Luma!
```

### Run from VS Code

1. Open `hello.luma` in VS Code.
2. Press `Ctrl+Alt+R` (or `Cmd+Alt+R` on macOS), or use the Command Palette → **Luma: Run Current File**.
3. The output `Hello from Luma!` appears in the terminal panel.

### Check Editor Features

- **Syntax highlighting** — keywords, types, strings, and annotations should be coloured.
- **Hover** — hover over `print` to see its type signature.
- **Diagnostics** — introduce a typo (for example, change `print` to `prnt`) and a red squiggly should appear.

### Debug

1. Set a breakpoint on the `print` line by clicking the editor gutter.
2. Press `F5` (or **Run → Start Debugging**).
3. The debugger should launch and pause at the breakpoint.

---

## 10 — Building from Source

If pre-built binaries are not available for your platform, or you prefer to build from source, see the [project README](../README.md#installation) and [CONTRIBUTING.md](../CONTRIBUTING.md) for the full build instructions. In brief:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default          # optional: run the test suite
```

This produces `luma`, `luma_lsp`, and `luma_dap` in the `build/` directory (or `build\Release\` on Windows with the Visual Studio generator).

---

## 11 — Troubleshooting

| Problem | Cause | Fix |
| --- | --- | --- |
| `luma: command not found` (Linux/macOS) or `'luma' is not recognized` (Windows). | Installation directory is not on `PATH`. | Add it to `PATH` (see platform sections above) and restart your terminal. |
| `luma --version` works in a terminal, but VS Code does not detect it. | VS Code was started before the `PATH` change. | Restart VS Code so it picks up the updated `PATH`. |
| Windows: `Windows protected your PC` (SmartScreen popup). | The binaries are unsigned and downloaded from the internet. | Click **More info** → **Run anyway**. |
| Windows: antivirus quarantines a binary. | Some antivirus engines flag unsigned executables as false positives. | Add the installation directory (e.g. `C:\Program Files\Luma`) as an exclusion in your antivirus settings, then re-extract the binary. |
| macOS: `"luma" cannot be opened because the developer cannot be verified.` | Gatekeeper quarantine on downloaded binaries. | Run `xattr -d com.apple.quarantine /usr/local/bin/luma` (and for `luma_lsp`, `luma_dap`). See [§5.2](#52--remove-the-quarantine-attribute). |
| macOS: `"luma" is damaged and can't be opened.` | Quarantine attribute still present after moving or copying the binary. | Run `xattr -cr /usr/local/bin/luma /usr/local/bin/luma_lsp /usr/local/bin/luma_dap` to remove all extended attributes. |
| Linux: `Permission denied` when running `luma`. | The binary is not executable. | Run `chmod +x /usr/local/bin/luma` (and for `luma_lsp`, `luma_dap`). |
| VS Code extension installed but no diagnostics or hover. | Language server binary not found. | Set `luma.lsp.path` in VS Code settings to the absolute path of `luma_lsp`. Check **Output → Luma Language Server** for details. |
| VS Code shows "Restricted mode — LSP disabled". | The workspace is not trusted. | Open the Command Palette → **Workspaces: Manage Workspace Trust** and trust the folder. |
| VS Code debugging does not start. | Debug adapter binary not found. | Set `luma.dap.path` in VS Code settings to the absolute path of `luma_dap`. |
| `no @main function found` when running a `.luma` file. | The source file has no `@main`-annotated function. | Add `@main` on the line immediately before your entry-point function. |
| Checksum verification fails. | Corrupted or incomplete download. | Re-download the failing archive from the release page. |
| Behind a corporate proxy — auto-download fails. | The extension cannot reach GitHub to download binaries. | Download the binaries manually (see [§2](#2--download)), then set `luma.lsp.path` and `luma.dap.path` in VS Code settings. |

---

## See Also

- [User Manual](Luma_User_Manual.md) — complete language reference
- [Tutorial](Luma_Tutorial.md) — a step-by-step introduction to Luma for beginners
- [REPL Guide](Luma_REPL_Guide.md) — interactive exploration of the language
- [Standard Library Reference](Luma_Standard_Library_Reference.md) — all built-in modules and functions
- [Debugger](Luma_Debugger.md) — debug adapter design and usage
- [Language Server](Luma_Language_Server.md) — language server features and architecture
- [Syntax Highlighting](Luma_Syntax_Highlighting.md) — editor extension design and grammars
- [Contributing](../CONTRIBUTING.md) — building from source and the contribution workflow
