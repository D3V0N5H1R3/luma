import * as crypto from "node:crypto";

/** Generates the full HTML content for the Luma Playground webview. */
export function generatePlaygroundHtml(): string {
    const nonce = crypto.randomBytes(16).toString("base64");
    return `<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
<style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
        font-family: var(--vscode-font-family);
        color: var(--vscode-foreground);
        background: var(--vscode-editor-background);
        height: 100vh;
        display: flex;
        flex-direction: column;
        padding: 8px;
    }
    .toolbar {
        display: flex;
        gap: 8px;
        margin-bottom: 8px;
        align-items: center;
    }
    button {
        background: var(--vscode-button-background);
        color: var(--vscode-button-foreground);
        border: none;
        padding: 4px 12px;
        cursor: pointer;
        border-radius: 2px;
        font-size: 12px;
    }
    button:hover { background: var(--vscode-button-hoverBackground); }
    .editor-container {
        flex: 1;
        display: flex;
        flex-direction: column;
        gap: 8px;
        min-height: 0;
    }
    textarea {
        flex: 1;
        background: var(--vscode-input-background);
        color: var(--vscode-input-foreground);
        border: 1px solid var(--vscode-input-border);
        font-family: var(--vscode-editor-font-family);
        font-size: var(--vscode-editor-font-size);
        padding: 8px;
        resize: none;
        tab-size: 4;
    }
    textarea:focus { outline: 1px solid var(--vscode-focusBorder); }
    .output {
        flex: 1;
        background: var(--vscode-terminal-background, var(--vscode-editor-background));
        color: var(--vscode-terminal-foreground, var(--vscode-foreground));
        border: 1px solid var(--vscode-input-border);
        font-family: var(--vscode-editor-font-family);
        font-size: var(--vscode-editor-font-size);
        padding: 8px;
        overflow: auto;
        white-space: pre-wrap;
        word-break: break-word;
    }
    .label { font-size: 11px; opacity: 0.7; margin-bottom: 4px; }
</style>
</head>
<body>
    <div class="toolbar">
        <button id="run-btn">▶ Run (Ctrl+Enter)</button>
        <button id="clear-btn">Clear</button>
    </div>
    <div class="editor-container">
        <div style="flex:1; display:flex; flex-direction:column;">
            <div class="label">Code</div>
            <textarea id="code" placeholder="# Write Luma code here...
@main
function main() {
    print(&quot;Hello!&quot;)
}"></textarea>
        </div>
        <div style="flex:1; display:flex; flex-direction:column;">
            <div class="label">Output</div>
            <div class="output" id="output"></div>
        </div>
    </div>

    <script nonce="${nonce}">
        const vscode = acquireVsCodeApi();
        const codeEl = document.getElementById('code');
        const outputEl = document.getElementById('output');

        document.getElementById('run-btn').addEventListener('click', run);
        document.getElementById('clear-btn').addEventListener('click', () => {
            outputEl.textContent = '';
        });

        codeEl.addEventListener('keydown', (e) => {
            if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
                e.preventDefault();
                run();
            }
            // Tab inserts spaces
            if (e.key === 'Tab') {
                e.preventDefault();
                const start = codeEl.selectionStart;
                const end = codeEl.selectionEnd;
                codeEl.value = codeEl.value.substring(0, start) + '    ' + codeEl.value.substring(end);
                codeEl.selectionStart = codeEl.selectionEnd = start + 4;
            }
        });

        function run() {
            const code = codeEl.value;
            outputEl.textContent = 'Running...';
            vscode.postMessage({ type: 'run', code });
        }

        window.addEventListener('message', (e) => {
            if (e.data.type === 'output') {
                outputEl.textContent = e.data.text || '(no output)';
            }
        });

        // Restore state
        const state = vscode.getState();
        if (state && state.code) {
            codeEl.value = state.code;
        }
        codeEl.addEventListener('input', () => {
            vscode.setState({ code: codeEl.value });
        });
    </script>
</body>
</html>`;
}
