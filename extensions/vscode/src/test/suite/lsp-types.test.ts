import * as assert from "node:assert";

import { isLspPosition, isLspLocation, toVscodeLocations } from "../../lsp/types";

suite("isLspPosition", () => {
    test("accepts a value with numeric line and character", () => {
        assert.strictEqual(isLspPosition({ line: 0, character: 0 }), true);
        assert.strictEqual(isLspPosition({ line: 10, character: 4 }), true);
    });

    test("rejects non-numeric fields", () => {
        assert.strictEqual(isLspPosition({ line: "0", character: 0 }), false);
        assert.strictEqual(isLspPosition({ line: 0, character: "0" }), false);
    });

    test("rejects when a field is missing", () => {
        assert.strictEqual(isLspPosition({ line: 0 }), false);
        assert.strictEqual(isLspPosition({ character: 0 }), false);
    });

    test("rejects non-object values", () => {
        assert.strictEqual(isLspPosition(null), false);
        assert.strictEqual(isLspPosition(undefined), false);
        assert.strictEqual(isLspPosition(42), false);
        assert.strictEqual(isLspPosition("0,0"), false);
    });
});

suite("isLspLocation", () => {
    const validLocation = {
        uri: "file:///a.luma",
        range: {
            start: { line: 0, character: 0 },
            end: { line: 1, character: 2 },
        },
    };

    test("accepts a fully-formed location", () => {
        assert.strictEqual(isLspLocation(validLocation), true);
    });

    test("rejects when uri is not a string", () => {
        assert.strictEqual(isLspLocation({ ...validLocation, uri: 5 }), false);
    });

    test("rejects when range is missing", () => {
        assert.strictEqual(isLspLocation({ uri: "file:///a.luma" }), false);
    });

    test("rejects when a range endpoint is not a position", () => {
        assert.strictEqual(
            isLspLocation({
                uri: "file:///a.luma",
                range: { start: { line: 0, character: 0 }, end: { line: 1 } },
            }),
            false,
        );
    });

    test("rejects non-object values", () => {
        assert.strictEqual(isLspLocation(null), false);
        assert.strictEqual(isLspLocation("file:///a.luma"), false);
    });
});

suite("toVscodeLocations", () => {
    test("maps valid locations and drops invalid ones", () => {
        const raw = [
            {
                uri: "file:///a.luma",
                range: { start: { line: 1, character: 2 }, end: { line: 3, character: 4 } },
            },
            "not a location",
            { uri: 5, range: null },
            {
                uri: "file:///b.luma",
                range: { start: { line: 5, character: 6 }, end: { line: 7, character: 8 } },
            },
        ];

        const locations = toVscodeLocations(raw);

        assert.strictEqual(locations.length, 2);
        assert.strictEqual(locations[0].uri.toString(), "file:///a.luma");
        assert.strictEqual(locations[0].range.start.line, 1);
        assert.strictEqual(locations[0].range.start.character, 2);
        assert.strictEqual(locations[0].range.end.line, 3);
        assert.strictEqual(locations[0].range.end.character, 4);
        assert.strictEqual(locations[1].uri.toString(), "file:///b.luma");
        assert.strictEqual(locations[1].range.end.character, 8);
    });

    test("returns an empty array when nothing is a valid location", () => {
        assert.deepStrictEqual(toVscodeLocations(["x", 1, null, {}]), []);
    });
});
