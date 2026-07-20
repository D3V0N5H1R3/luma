import * as vscode from "vscode";
import { LUMA_BUILTIN_TYPE_SET, LUMA_TYPE_PATTERN } from "../utils/constants";
import { escapeRegex } from "../utils/util";

// ─── Precompiled diagnostic patterns ─────────────────────────────
//
// Exported so the unit tests bind to the real patterns rather than
// re-declaring copies (which would pass even if these drifted). None carry the
// global flag, so sharing them across call sites and tests is stateless-safe.

/** Matches diagnostics about assigning to or mutating an immutable binding. */
export const MUTABLE_DIAGNOSTIC_PATTERN = /cannot (?:assign|mutate)|immutable/;
/** Matches a declaration line that is already marked `mutable`. */
export const ALREADY_MUTABLE_PATTERN = /^\s*mutable\s/;
/** Matches diagnostics about an unknown identifier, module, or function. */
export const UNKNOWN_IDENT_DIAGNOSTIC_PATTERN =
    /unknown (?:identifier|module|function)|not defined/;
/** Captures the offending name from an "unknown identifier `x`" diagnostic. */
export const IDENT_NAME_PATTERN =
    /unknown (?:identifier|module|function) [`']([a-zA-Z_][a-zA-Z0-9_]*)[`']/;

// ─── Shared helper ───────────────────────────────────────────────

function pushFix(
    actions: vscode.CodeAction[],
    diag: vscode.Diagnostic,
    title: string,
    edit: vscode.WorkspaceEdit,
    preferred = false,
): void {
    const fix = new vscode.CodeAction(title, vscode.CodeActionKind.QuickFix);
    fix.diagnostics = [diag];
    fix.isPreferred = preferred;
    fix.edit = edit;
    actions.push(fix);
}

/**
 * Registers a quick fix that inserts `include "<include_path>"` at the top of the
 * document. Shared by the workspace-match and snake_case-fallback branches.
 */
function addIncludeFix(
    actions: vscode.CodeAction[],
    diag: vscode.Diagnostic,
    document: vscode.TextDocument,
    include_path: string,
): void {
    const edit = new vscode.WorkspaceEdit();
    edit.insert(document.uri, new vscode.Position(0, 0), `include "${include_path}"\n`);
    pushFix(actions, diag, `Add include "${include_path}"`, edit);
}

// ─── Mutable keyword fixer ───────────────────────────────────────

/** Suggests inserting the `mutable` keyword for immutability diagnostics. */
export class MutableKeywordFixer implements vscode.CodeActionProvider {
    provideCodeActions(
        document: vscode.TextDocument,
        _range: vscode.Range,
        context: vscode.CodeActionContext,
    ): vscode.CodeAction[] {
        const actions: vscode.CodeAction[] = [];

        for (const diag of context.diagnostics) {
            this.checkMutableFix(document, diag, actions);
        }

        return actions;
    }

    private checkMutableFix(
        document: vscode.TextDocument,
        diag: vscode.Diagnostic,
        actions: vscode.CodeAction[],
    ): void {
        const msg = diag.message;
        if (!MUTABLE_DIAGNOSTIC_PATTERN.test(msg)) {
            return;
        }
        const diag_line = diag.range.start.line;
        const line_text = document.lineAt(diag_line).text;
        if (ALREADY_MUTABLE_PATTERN.test(line_text)) {
            return;
        }

        // Try the diagnostic line first, then the line above for multi-line declarations.
        let target_line = diag_line;
        let type_match = LUMA_TYPE_PATTERN.exec(line_text);
        if (!type_match && diag_line > 0) {
            const prev_text = document.lineAt(diag_line - 1).text;
            if (ALREADY_MUTABLE_PATTERN.test(prev_text)) {
                return;
            }
            type_match = LUMA_TYPE_PATTERN.exec(prev_text);
            if (type_match) {
                target_line = diag_line - 1;
            }
        }
        if (!type_match) {
            return;
        }
        const word = type_match[2];
        // Accept built-in types or PascalCase user-defined types.
        if (!LUMA_BUILTIN_TYPE_SET.has(word) && !/^[A-Z]/.test(word)) {
            return;
        }
        const target_text = document.lineAt(target_line).text;
        const edit = new vscode.WorkspaceEdit();
        const type_start = target_text.indexOf(word);
        edit.insert(document.uri, new vscode.Position(target_line, type_start), "mutable ");
        pushFix(actions, diag, "Make variable mutable", edit, true);
    }
}

// ─── Include path fixer ──────────────────────────────────────────

/** Suggests adding `include` statements for unknown identifiers. */
export class IncludePathFixer implements vscode.CodeActionProvider {
    async provideCodeActions(
        document: vscode.TextDocument,
        _range: vscode.Range,
        context: vscode.CodeActionContext,
    ): Promise<vscode.CodeAction[]> {
        const actions: vscode.CodeAction[] = [];

        for (const diag of context.diagnostics) {
            await this.checkIncludeFix(document, diag, actions);
        }

        return actions;
    }

    private async checkIncludeFix(
        document: vscode.TextDocument,
        diag: vscode.Diagnostic,
        actions: vscode.CodeAction[],
    ): Promise<void> {
        const msg = diag.message;
        if (!UNKNOWN_IDENT_DIAGNOSTIC_PATTERN.test(msg)) {
            return;
        }
        const ident_match = IDENT_NAME_PATTERN.exec(msg);
        if (!ident_match) {
            return;
        }
        const name = ident_match[1];
        // Search workspace for .luma files that define this symbol.
        const files = await vscode.workspace.findFiles("**/*.luma", null, 50);
        const matches = await findIncludeMatches(
            name,
            document.uri,
            files,
            (file) => vscode.workspace.fs.readFile(file),
            (file) => vscode.workspace.asRelativePath(file, false),
        );

        if (matches.length > 0) {
            for (const { relative } of matches) {
                addIncludeFix(actions, diag, document, relative);
            }
        } else {
            // Fallback: offer a snake_case filename heuristic.
            const snake = name.replaceAll(/([a-z])([A-Z])/g, "$1_$2").toLowerCase();
            addIncludeFix(actions, diag, document, `${snake}.luma`);
        }
    }
}

/**
 * Finds workspace `.luma` files (excluding the current document) whose contents
 * mention `name` as a whole word, returning `{ relative, uri }` for each.
 *
 * The word-boundary RegExp is compiled once (it is invariant across the file
 * loop), and the candidate files are read concurrently via `Promise.all` so the
 * total latency is the slowest read rather than the sum of all reads. Input
 * order is preserved so the produced quick-fix actions stay stable.
 */
export async function findIncludeMatches(
    name: string,
    current_uri: vscode.Uri,
    files: readonly vscode.Uri[],
    read_file: (file: vscode.Uri) => Thenable<Uint8Array>,
    to_relative: (file: vscode.Uri) => string,
): Promise<{ relative: string; uri: vscode.Uri }[]> {
    const word_boundary = new RegExp(String.raw`\b${escapeRegex(name)}\b`);
    const candidates = files.filter((file) => file.fsPath !== current_uri.fsPath);
    const results = await Promise.all(
        candidates.map(async (file) => {
            const bytes = await read_file(file);
            const text = Buffer.from(bytes).toString("utf-8");
            return word_boundary.test(text) ? { relative: to_relative(file), uri: file } : null;
        }),
    );
    return results.filter(
        (match): match is { relative: string; uri: vscode.Uri } => match !== null,
    );
}
