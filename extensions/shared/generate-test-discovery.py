#!/usr/bin/env python3
"""Generate test-discovery patterns from test-discovery-pattern.json.

The VS Code extension discovers @test/@main annotations with regular
expressions, so it can share the canonical patterns directly. The tree-sitter
based Zed extension implements the same contract with native queries and is not
generated from this file.

Outputs:
    --vscode → extensions/vscode/src/generated/test-discovery.ts
    --all    → same as --vscode

Usage:
    python generate-test-discovery.py --all
"""

from __future__ import annotations

import codegen_common as cc

ROOT_DIR = cc.resolve_root()
PATTERN_PATH = cc.SCRIPT_DIR / "test-discovery-pattern.json"


def _regex_literal(spec: dict) -> str:
    """Render a ``{regex, flags}`` spec as a TypeScript RegExp literal."""
    return f"/{spec['regex']}/{spec['flags']}"


def generate_vscode(data: dict) -> str:
    """Generate TypeScript factory functions returning fresh RegExp instances."""
    lines = [
        *cc.banner("//", "test-discovery-pattern.json", "generate-test-discovery.py", "vscode"),
        "",
        f"/** {data['test_function']['description']} */",
        "export function testFunctionPattern(): RegExp {",
        f"    return {_regex_literal(data['test_function'])};",
        "}",
        "",
        f"/** {data['test_annotation']['description']} */",
        "export function testAnnotationPattern(): RegExp {",
        f"    return {_regex_literal(data['test_annotation'])};",
        "}",
        "",
        f"/** {data['main_annotation']['description']} */",
        "export function mainAnnotationPattern(): RegExp {",
        f"    return {_regex_literal(data['main_annotation'])};",
        "}",
        "",
    ]
    return "\n".join(lines)


GENERATED_OUTPUTS = [
    ROOT_DIR / "vscode" / "src" / "generated" / "test-discovery.ts",
]


def main() -> None:
    parser = cc.editor_arg_parser("Generate test-discovery patterns", editors=("vscode",))
    args = parser.parse_args()

    cc.require_editors(parser, args.vscode or args.all)

    data = cc.load_json(PATTERN_PATH)
    cc.write_file(
        ROOT_DIR / "vscode" / "src" / "generated" / "test-discovery.ts", generate_vscode(data)
    )

    print("Test-discovery generation complete.")


if __name__ == "__main__":
    main()
