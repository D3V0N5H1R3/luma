/**
 * Minimal vscode stub for unit tests that run outside VS Code.
 * Loaded via --require before the test runner so that modules importing
 * "vscode" at the top level get a harmless stub instead of a crash.
 */
"use strict";

class Position {
    constructor(line, character) {
        this.line = line;
        this.character = character;
    }
}

class Range {
    constructor(startLine, startCharacter, endLine, endCharacter) {
        // Support both (Position, Position) and (number, number, number, number).
        if (typeof startLine === "object") {
            this.start = startLine;
            this.end = startCharacter;
        } else {
            this.start = new Position(startLine, startCharacter);
            this.end = new Position(endLine, endCharacter);
        }
    }
}

class Location {
    constructor(uri, range) {
        this.uri = uri;
        this.range = range;
    }
}

const stub = {
    // Marker so tests can detect the unit stub and gate assertions that mutate
    // the vscode namespace (which is read-only in the real electron runtime).
    IS_UNIT_STUB: true,
    workspace: {
        getConfiguration: () => ({ get: (_key, defaultValue) => defaultValue }),
        workspaceFolders: [],
    },
    window: {
        showWarningMessage: () => Promise.resolve(),
        showErrorMessage: () => Promise.resolve(),
        showInformationMessage: () => Promise.resolve(),
        withProgress: (_opts, task) => task({ report: () => {} }),
        createOutputChannel: () => ({
            appendLine: () => {},
            append: () => {},
            show: () => {},
            dispose: () => {},
        }),
    },
    extensions: { getExtension: () => undefined },
    ProgressLocation: { Notification: 1 },
    Uri: {
        file: (p) => ({ fsPath: p, toString: () => p }),
        parse: (p) => ({ fsPath: p, toString: () => p }),
    },
    Position,
    Range,
    Location,
    LanguageStatusSeverity: { Information: 0, Warning: 1, Error: 2 },
    TestMessage: class {
        constructor(message) {
            this.message = message;
        }
    },
    commands: { getCommands: () => Promise.resolve([]) },
};

// Inject into the require cache so `require("vscode")` returns this stub.
require.cache[require.resolve || "vscode"] = undefined;

// Use the internal Module API to intercept the resolution.
const Module = require("node:module");
const originalLoad = Module._load;
Module._load = function (request, parent, isMain) {
    if (request === "vscode") {
        return stub;
    }
    return originalLoad.call(this, request, parent, isMain);
};
