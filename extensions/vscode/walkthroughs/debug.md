## Debug a Program

The extension bundles the Luma debug adapter (`luma_dap`), downloaded and kept up to date automatically — the same way as the language server. No setup is required to start debugging.

### Set a breakpoint and launch

1. Click in the gutter to the left of a line number to set a **breakpoint** (a red dot).
2. With a `.luma` file open, press `F5` to start debugging. The extension launches the current file with a default configuration — no `launch.json` needed.
3. Execution pauses at each breakpoint. Use the debug toolbar to **Continue** (`F5`), **Step Over** (`F10`), **Step Into** (`F11`), and **Step Out** (`Shift+F11`).

While paused, inspect the **Variables** and **Call Stack** views, hover over a variable in the editor to see its value, and add expressions to the **Watch** view.

### Customise the launch

To debug with arguments or a fixed program, add a `luma` configuration to `.vscode/launch.json`:

```json
{
    "type": "luma",
    "request": "launch",
    "name": "Debug Current File",
    "program": "${file}",
    "stopOnEntry": false,
    "args": [],
    "cwd": "${workspaceFolder}"
}
```

| Attribute     | Default              | Description                                                      |
| ------------- | -------------------- | ---------------------------------------------------------------- |
| `program`     | `${file}`            | Path to the Luma program to debug.                               |
| `stopOnEntry` | `false`              | Pause on the first executable line.                              |
| `args`        | `[]`                 | Arguments passed to the program (via `Process.get_arguments()`). |
| `cwd`         | `${workspaceFolder}` | Working directory for the debugged program.                      |
| `timeTravel`  | `false`              | Record execution history to enable Step Back / Reverse.          |

### Visualise structured values

During a debug session, run **Luma: Visualize Variable** (or right-click a variable in the debug panel) to render structured runtime values — arrays, records, and trees — graphically in the **Luma Visualizer** panel.
