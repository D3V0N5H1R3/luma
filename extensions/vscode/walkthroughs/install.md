## Install the Language Server

The Luma extension provides full language support through the **`luma_lsp`** language server.

### Automatic download

On first activation the extension downloads the correct `luma_lsp` binary for your platform from GitHub Releases — no manual steps needed. It keeps the binary up to date on later activations; turn this off with the `luma.lsp.autoUpdate` setting if you prefer to manage it yourself.

If a `luma_lsp` binary is already on your `PATH`, the extension uses that instead of downloading one.

### Manual configuration

If automatic download is unavailable (for example, offline or behind a proxy), build the server from source with the project's CMake presets:

```bash
cmake --preset default
cmake --build --preset default --target luma_lsp
```

This produces the `luma_lsp` binary in the `build` directory. Set its path in VS Code settings:

```json
"luma.lsp.path": "/absolute/path/to/luma_lsp"
```

The path also accepts the `${workspaceFolder}` variable, so you can point at a binary stored inside your project.
