import type * as vscode from "vscode";
import type { DocumentSelector } from "vscode-languageclient/node";

/**
 * Document selector for a per-folder language client in a multi-root
 * workspace.
 *
 * The folder is matched with an LSP relative pattern rather than a glob built
 * from `fsPath`: a Windows path's backslashes, or glob characters such as `[`
 * and `{` in a folder name, would otherwise turn the glob into one that never
 * matches. Untitled documents belong to no folder, so the first folder's
 * client serves them; every other client leaves them alone.
 */
export function folderDocumentSelector(folder: vscode.WorkspaceFolder): DocumentSelector {
    const selector: DocumentSelector = [
        {
            scheme: "file",
            language: "luma",
            pattern: { baseUri: folder.uri.toString(), pattern: "**/*" },
        },
    ];
    if (folder.index === 0) {
        selector.push({ scheme: "untitled", language: "luma" });
    }
    return selector;
}
