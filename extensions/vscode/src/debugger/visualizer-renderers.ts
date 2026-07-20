import { escapeHtml } from "../utils/html";

/** Shared CSS variables used across all visualizer HTML views. */
export const VISUALIZER_BASE_CSS = `
    body { font-family: var(--vscode-font-family); color: var(--vscode-foreground); padding: 12px; margin: 0; }
    h3 { margin: 0 0 8px 0; font-size: 13px; }
    .type { opacity: 0.6; font-size: 11px; }
    .value { font-family: var(--vscode-editor-font-family); }
    .scalar { padding: 4px 0; }
    table { border-collapse: collapse; width: 100%; margin-top: 8px; }
    th, td { border: 1px solid var(--vscode-widget-border); padding: 4px 8px; text-align: left; font-size: 12px; }
    th { background: var(--vscode-editor-background); opacity: 0.8; }
    .bar { height: 14px; background: var(--vscode-progressBar-background); border-radius: 2px; }
    .hint { opacity: 0.7; font-style: italic; }
    .error { color: var(--vscode-errorForeground); }
`;

/** Wraps body content in a CSP-restricted HTML document with shared styles. */
export function wrapHtml(body: string, extra_css = ""): string {
    return `<!DOCTYPE html>
<html>
<head>
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline';">
<style>${VISUALIZER_BASE_CSS}${extra_css}</style></head>
<body>
${body}
</body>
</html>`;
}

/** Maximum number of elements to display in array view. */
const MAX_DISPLAY_ELEMENTS = 200;
/** Maximum number of elements for bar chart rendering. */
const MAX_BAR_CHART_ELEMENTS = 50;

/** Renders a debug value as HTML, choosing appropriate format based on type. */
export function renderValue(
    name: string,
    response: { result: string; type?: string; variablesReference?: number },
): string {
    const type_label = response.type ?? "unknown";

    // Prefer structured rendering from parsed JSON. Fall back to best-effort
    // heuristic parsing for debug output that is not strict JSON (e.g. Luma
    // record literals with unquoted keys).
    let parsed: unknown;
    try {
        parsed = JSON.parse(response.result);
    } catch {
        parsed = undefined;
    }

    let content_html: string;
    if (Array.isArray(parsed)) {
        content_html = renderArray(parsed);
    } else if (typeof parsed === "object" && parsed !== null) {
        content_html = renderObject(parsed as Record<string, unknown>);
    } else if (response.result.startsWith("[") && response.result.endsWith("]")) {
        content_html = renderArrayLoose(response.result);
    } else if (response.result.startsWith("{") && response.result.endsWith("}")) {
        content_html = renderObjectLoose(response.result);
    } else {
        content_html = `<div class="scalar"><span class="type">${escapeHtml(type_label)}</span> <span class="value">${escapeHtml(response.result)}</span></div>`;
    }

    return wrapHtml(
        `<h3>${escapeHtml(name)} <span class="type">(${escapeHtml(type_label)})</span></h3>
    ${content_html}`,
    );
}

/** Formats a parsed JSON value as a single display string for a table cell. */
function formatCell(value: unknown): string {
    if (value === null) {
        return "null";
    }
    if (typeof value === "object") {
        return JSON.stringify(value);
    }
    return String(value);
}

/** Renders the array body as a bar chart (numeric) or an indexed table. */
function renderArrayTable(
    cells: readonly string[],
    numbers: readonly number[] | undefined,
): string {
    if (cells.length > MAX_DISPLAY_ELEMENTS) {
        return `<em>Array too large to display (${cells.length} elements, limit is ${MAX_DISPLAY_ELEMENTS})</em>`;
    }

    if (numbers && cells.length <= MAX_BAR_CHART_ELEMENTS) {
        const max = Math.max(...numbers, 1);
        let rows = "";
        for (let i = 0; i < cells.length; i++) {
            const pct = (numbers[i] / max) * 100;
            rows += `<tr><td>${i}</td><td>${escapeHtml(cells[i])}</td><td><div class="bar" style="width:${pct}%"></div></td></tr>`;
        }
        return `<table><tr><th>#</th><th>Value</th><th>Bar</th></tr>${rows}</table>`;
    }

    let rows = "";
    for (let i = 0; i < cells.length; i++) {
        rows += `<tr><td>${i}</td><td>${escapeHtml(cells[i])}</td></tr>`;
    }
    return `<table><tr><th>Index</th><th>Value</th></tr>${rows}</table>`;
}

/** Renders a parsed JSON array as a table or bar chart. */
export function renderArray(elements: readonly unknown[]): string {
    if (elements.length === 0) {
        return "<em>empty array</em>";
    }
    const cells = elements.map(formatCell);
    const all_numeric = elements.every((el) => typeof el === "number" && Number.isFinite(el));
    return renderArrayTable(cells, all_numeric ? (elements as number[]) : undefined);
}

/** Renders the rows of a key/value record table. */
function renderRecordTable(entries: ReadonlyArray<{ key: string; val: string }>): string {
    let rows = "";
    for (const { key, val } of entries) {
        rows += `<tr><td><strong>${escapeHtml(key)}</strong></td><td>${escapeHtml(val)}</td></tr>`;
    }
    return `<table><tr><th>Field</th><th>Value</th></tr>${rows}</table>`;
}

/** Renders a parsed JSON object as a key-value table. */
export function renderObject(obj: Record<string, unknown>): string {
    const keys = Object.keys(obj);
    if (keys.length === 0) {
        return "<em>empty record</em>";
    }
    return renderRecordTable(keys.map((key) => ({ key, val: formatCell(obj[key]) })));
}

interface TopLevelChar {
    ch: string;
    index: number;
    depth: number;
    inQuote: boolean;
}

/**
 * Walks `input` one character at a time while tracking bracket depth ([], {})
 * and quote state ("/'), yielding each character with the depth and quote flag
 * in effect. Callers act only on top-level (`depth === 0`, `!inQuote`)
 * characters. Shared by the loose renderers' splitTopLevel and indexOfTopLevel.
 */
function* scanTopLevel(input: string): Generator<TopLevelChar> {
    let depth = 0;
    let quote: string | undefined;
    for (let index = 0; index < input.length; index++) {
        const ch = input[index];
        if (quote) {
            if (ch === quote) {
                quote = undefined;
            }
            yield { ch, index, depth, inQuote: true };
        } else if (ch === '"' || ch === "'") {
            quote = ch;
            yield { ch, index, depth, inQuote: true };
        } else {
            if (ch === "[" || ch === "{") {
                depth++;
            } else if (ch === "]" || ch === "}") {
                depth--;
            }
            yield { ch, index, depth, inQuote: false };
        }
    }
}

/**
 * Splits `input` on a single-character separator, but only at the top level —
 * separators nested inside brackets ([], {}) or quotes are ignored. Used by the
 * best-effort loose renderers for debug output that is not strict JSON.
 */
function splitTopLevel(input: string, separator: string): string[] {
    const parts: string[] = [];
    let current = "";
    for (const { ch, depth, inQuote } of scanTopLevel(input)) {
        if (!inQuote && depth === 0 && ch === separator) {
            parts.push(current);
            current = "";
        } else {
            current += ch;
        }
    }
    parts.push(current);
    return parts;
}

/** Returns the index of the first top-level `separator`, or -1 if none. */
function indexOfTopLevel(input: string, separator: string): number {
    for (const { ch, index, depth, inQuote } of scanTopLevel(input)) {
        if (!inQuote && depth === 0 && ch === separator) {
            return index;
        }
    }
    return -1;
}

/** Best-effort renderer for array-like strings that are not valid JSON. */
function renderArrayLoose(value: string): string {
    const inner = value.slice(1, -1).trim();
    if (!inner) {
        return "<em>empty array</em>";
    }
    const cells = splitTopLevel(inner, ",").map((s) => s.trim());
    const coerced = cells.map(Number);
    const all_numeric = coerced.every((n) => !Number.isNaN(n));
    return renderArrayTable(cells, all_numeric ? coerced : undefined);
}

/** Best-effort renderer for object-like strings that are not valid JSON. */
function renderObjectLoose(value: string): string {
    const inner = value.slice(1, -1).trim();
    if (!inner) {
        return "<em>empty record</em>";
    }
    const entries = splitTopLevel(inner, ",").map((segment) => {
        const trimmed = segment.trim();
        const colon = indexOfTopLevel(trimmed, ":");
        if (colon === -1) {
            return { key: trimmed, val: "" };
        }
        return {
            key: trimmed.slice(0, colon).trim(),
            val: trimmed.slice(colon + 1).trim(),
        };
    });
    return renderRecordTable(entries);
}

/** Renders an error message. */
export function renderError(expression: string, message: string): string {
    return wrapHtml(
        `<h3>${escapeHtml(expression)}</h3>
    <p class="error">${escapeHtml(message)}</p>`,
    );
}

/** Renders the empty-state hint shown when no variable is selected. */
export function renderEmptyHint(): string {
    return wrapHtml(
        `<p class="hint">Use "Luma: Visualize Variable" command or right-click a variable in the debug panel.</p>`,
    );
}
