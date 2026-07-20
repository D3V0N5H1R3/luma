#!/usr/bin/env python3
"""Generate the built-in type list from builtin-types.json.

The VS Code extension recognises Luma's built-in type keywords as a Set used by
the "make mutable" quick fix (src/lsp/code-actions.ts). This generator emits that
set so the list has a single source of truth (builtin-types.json) instead of a
hand-maintained copy in src/utils/constants.ts. The tree-sitter based Zed
extension recognises the same types through its native grammar and is not
generated from this file.

Outputs:
    --vscode → extensions/vscode/src/generated/builtin-types.ts
    --all    → same as --vscode

Usage:
    python generate-builtin-types.py --all
"""

from __future__ import annotations

import codegen_common as cc

ROOT_DIR = cc.resolve_root()
TYPES_PATH = cc.SCRIPT_DIR / "builtin-types.json"


def generate_vscode(data: dict) -> str:
    """Generate the TypeScript built-in type Set module."""
    lines = [
        *cc.banner("//", "builtin-types.json", "generate-builtin-types.py", "vscode"),
        "",
        "/** Built-in type keywords recognised in variable declarations. */",
        "export const LUMA_BUILTIN_TYPE_SET = new Set([",
    ]
    for type_name in data["types"]:
        lines.append(f'    "{type_name}",')
    lines.append("]);")
    lines.append("")
    return "\n".join(lines)


GENERATED_OUTPUTS = [
    ROOT_DIR / "vscode" / "src" / "generated" / "builtin-types.ts",
]


def main() -> None:
    parser = cc.editor_arg_parser("Generate built-in type list", editors=("vscode",))
    args = parser.parse_args()

    cc.require_editors(parser, args.vscode or args.all)

    data = cc.load_json(TYPES_PATH)
    cc.write_file(GENERATED_OUTPUTS[0], generate_vscode(data))

    print("Built-in type generation complete.")


if __name__ == "__main__":
    main()
