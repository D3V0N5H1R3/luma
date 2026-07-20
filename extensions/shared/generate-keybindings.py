#!/usr/bin/env python3
"""Generate keybinding definitions from keybindings.json.

Produces editor-specific keybinding configurations:
    --vscode → splices contributes.keybindings into extensions/vscode/package.json
    --zed    → keybinding documentation snippet for the Zed README (KEYBINDINGS.md)
    --all    → --vscode and --zed

Usage:
    python generate-keybindings.py --all
    python generate-keybindings.py --vscode
"""

from __future__ import annotations

import codegen_common as cc

ROOT_DIR = cc.resolve_root()
KEYBINDINGS_PATH = cc.SCRIPT_DIR / "keybindings.json"


def _vscode_keybindings(data: dict) -> list[dict]:
    """Build the contributes.keybindings array from the canonical definitions."""
    keybindings: list[dict] = []
    for _category, bindings in data["keybindings"].items():
        for binding in bindings:
            vscode = binding.get("vscode")
            if not vscode:
                continue

            entry: dict = {}
            command = vscode.get("command")
            if command:
                entry["command"] = command
            key = vscode.get("key")
            if key:
                entry["key"] = key
            mac_key = vscode.get("mac")
            if mac_key:
                entry["mac"] = mac_key
            when = vscode.get("when")
            if when:
                entry["when"] = when

            if entry.get("command") or entry.get("key"):
                keybindings.append(entry)

    return keybindings


def generate_vscode(data: dict) -> None:
    """Splice the generated keybindings into package.json."""
    keybindings = _vscode_keybindings(data)

    def mutate(manifest: dict) -> None:
        manifest["contributes"]["keybindings"] = keybindings

    cc.update_package_json(ROOT_DIR, mutate)


def generate_zed(data: dict) -> str:
    """Generate a Zed keymap documentation snippet for the README."""
    # Zed has no extension-defined keybindings (see defaults.zed in
    # keybindings.json): LSP/diagnostics features are built-in `editor::` actions
    # with their own defaults, and running/testing is driven by the task runner.
    # Emit a correct, copy-pasteable example plus a reference table of the logical
    # Luma actions rather than fabricating per-command actions Zed does not expose.
    lines = [
        *cc.banner(
            "<!--", "keybindings.json", "generate-keybindings.py", "zed", comment_close="-->"
        ),
        "",
        "## Suggested Keybindings",
        "",
        "Zed extensions cannot register keybindings automatically. Add the bindings",
        "below to your `keymap.json` (`zed: open keymap` command); the",
        "`extension == luma` context scopes them to Luma files.",
        "",
        "### Run and test",
        "",
        "Luma `@main` and `@test` functions get inline **Run** buttons from Zed's task",
        "runner. Drive them from the keyboard with Zed's built-in task actions:",
        "",
        "```json",
        "[",
        "  {",
        '    "context": "Editor && extension == luma",',
        '    "bindings": {',
        "      // Spawn a task for the current file (pick the @main or @test runnable)",
        '      "ctrl-alt-r": "task::Spawn",',
        "      // Re-run the most recently spawned task",
        '      "ctrl-alt-shift-r": "task::Rerun"',
        "    }",
        "  }",
        "]",
        "```",
        "",
        "### Language server actions",
        "",
        "Go to definition, find references, rename, format, and the other `lsp` actions",
        "below are Zed's built-in `editor::` commands and already have default",
        "keybindings. Rebind any of them under the same `Editor && extension == luma`",
        "context to use Luma-specific keys.",
        "",
        "### Available Actions",
        "",
        "Logical Luma actions and the Zed subsystem that provides each: `lsp` and",
        "`diagnostics` actions are built-in `editor::` commands, `runner` actions use",
        "the task runner (`task::Spawn` / `task::Rerun`), and `debug` actions use Zed's",
        "debugger.",
        "",
        "| Action | Description | Category |",
        "| ------ | ----------- | -------- |",
    ]

    for category, bindings in data["keybindings"].items():
        for binding in bindings:
            action = binding["action"]
            desc = binding.get("description", action)
            lines.append(f"| `{action}` | {desc} | {category} |")

    lines.append("")

    return "\n".join(lines)


GENERATED_OUTPUTS = [
    ROOT_DIR / "vscode" / "package.json",
    ROOT_DIR / "zed" / "KEYBINDINGS.md",
]


def main() -> None:
    parser = cc.editor_arg_parser("Generate keybindings from keybindings.json")
    args = parser.parse_args()

    do_vscode = args.vscode or args.all
    do_zed = args.zed or args.all

    cc.require_editors(parser, do_vscode or do_zed)

    data = cc.load_json(KEYBINDINGS_PATH)

    if do_vscode:
        generate_vscode(data)
    if do_zed:
        cc.write_file(ROOT_DIR / "zed" / "KEYBINDINGS.md", generate_zed(data))

    print("Keybinding generation complete.")


if __name__ == "__main__":
    main()
