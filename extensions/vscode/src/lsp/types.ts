import * as vscode from "vscode";

// ─── LSP types used by extension commands ─────────────────────────

export interface LspPosition {
    line: number;
    character: number;
}

export interface LspLocation {
    uri: string;
    range: {
        start: LspPosition;
        end: LspPosition;
    };
}

// Only two type guards exist and each checks different fields, so a generic
// createTypeGuard<T> factory would add abstraction without reducing code.
// If more guards with identical structure are added, consider:
//   function createTypeGuard<T>(check: (v: unknown) => boolean): (v: unknown) => v is T
/**
 * Type guard for LspPosition. Validates that a value has both `line` and `character`
 * fields of type number, matching the LSP Position interface.
 */
export function isLspPosition(value: unknown): value is LspPosition {
    return (
        typeof value === "object" &&
        value !== null &&
        "line" in value &&
        "character" in value &&
        typeof (value as LspPosition).line === "number" &&
        typeof (value as LspPosition).character === "number"
    );
}

/**
 * Type guard for LspLocation. Validates that a value has both `uri` and `range`
 * fields, where `range` contains `start` and `end` as LspPositions.
 */
export function isLspLocation(value: unknown): value is LspLocation {
    if (
        typeof value !== "object" ||
        value === null ||
        !("uri" in value) ||
        !("range" in value) ||
        typeof (value as LspLocation).uri !== "string"
    ) {
        return false;
    }
    const range = (value as LspLocation).range;
    return (
        typeof range === "object" &&
        range !== null &&
        isLspPosition(range.start) &&
        isLspPosition(range.end)
    );
}

/**
 * Converts an array of raw LSP locations into VS Code Location objects.
 */
export function toVscodeLocations(locations: unknown[]): vscode.Location[] {
    return locations.filter(isLspLocation).map((l) => {
        return new vscode.Location(
            vscode.Uri.parse(l.uri),
            new vscode.Range(
                l.range.start.line,
                l.range.start.character,
                l.range.end.line,
                l.range.end.character,
            ),
        );
    });
}
