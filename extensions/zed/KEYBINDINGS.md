<!-- AUTO-GENERATED from extensions/shared/keybindings.json -->
<!-- Do not edit manually. Run: python generate-keybindings.py --zed -->

## Suggested Keybindings

Zed extensions cannot register keybindings automatically. Add the bindings
below to your `keymap.json` (`zed: open keymap` command); the
`extension == luma` context scopes them to Luma files.

### Run and test

Luma `@main` and `@test` functions get inline **Run** buttons from Zed's task
runner. Drive them from the keyboard with Zed's built-in task actions:

```json
[
  {
    "context": "Editor && extension == luma",
    "bindings": {
      // Spawn a task for the current file (pick the @main or @test runnable)
      "ctrl-alt-r": "task::Spawn",
      // Re-run the most recently spawned task
      "ctrl-alt-shift-r": "task::Rerun"
    }
  }
]
```

### Language server actions

Go to definition, find references, rename, format, and the other `lsp` actions
below are Zed's built-in `editor::` commands and already have default
keybindings. Rebind any of them under the same `Editor && extension == luma`
context to use Luma-specific keys.

### Available Actions

Logical Luma actions and the Zed subsystem that provides each: `lsp` and
`diagnostics` actions are built-in `editor::` commands, `runner` actions use
the task runner (`task::Spawn` / `task::Rerun`), and `debug` actions use Zed's
debugger.

| Action | Description | Category |
| ------ | ----------- | -------- |
| `goToDefinition` | Go to definition | lsp |
| `findReferences` | Find references | lsp |
| `hover` | Hover info | lsp |
| `rename` | Rename symbol | lsp |
| `codeAction` | Code action | lsp |
| `format` | Format buffer | lsp |
| `signatureHelp` | Signature help | lsp |
| `typeDefinition` | Type definition | lsp |
| `toggleInlayHints` | Toggle inlay hints | lsp |
| `showDiagnostic` | Show diagnostic at cursor | diagnostics |
| `previousDiagnostic` | Previous diagnostic | diagnostics |
| `nextDiagnostic` | Next diagnostic | diagnostics |
| `diagnosticsToLoclist` | Send diagnostics to location list | diagnostics |
| `runFile` | Run current Luma file | runner |
| `runTests` | Run tests in current file | runner |
| `runNearestTest` | Run nearest test | runner |
| `toggleBreakpoint` | Toggle breakpoint | debug |
| `conditionalBreakpoint` | Set conditional breakpoint | debug |
| `debugContinue` | Debug: continue | debug |
| `debugStepOver` | Debug: step over | debug |
| `debugStepInto` | Debug: step into | debug |
| `debugTerminate` | Debug: terminate | debug |
