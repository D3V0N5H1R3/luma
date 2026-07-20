import * as assert from "node:assert";

import { renderArray, renderObject, renderValue } from "../../debugger/visualizer-renderers";

suite("renderArray (structured)", () => {
    test("renders an all-numeric array as a bar chart", () => {
        const html = renderArray([1, 2, 3]);
        assert.ok(html.includes('class="bar"'), "expected a bar chart");
        assert.ok(html.includes("<th>Bar</th>"));
    });

    test("renders a non-numeric array as an indexed table", () => {
        const html = renderArray(["a", "b"]);
        assert.ok(!html.includes('class="bar"'), "should not be a bar chart");
        assert.ok(html.includes("<th>Index</th>"));
        assert.ok(html.includes("<td>a</td>"));
    });

    test("treats numeric strings as non-numeric (no bar chart)", () => {
        const html = renderArray(["1", "2"]);
        assert.ok(!html.includes('class="bar"'));
    });

    test("renders nested values as compact JSON cells", () => {
        const html = renderArray([
            [1, 2],
            [3, 4],
        ]);
        // Cell content is JSON.stringify-ed then HTML-escaped.
        assert.ok(html.includes("[1,2]"));
        assert.ok(html.includes("[3,4]"));
    });

    test("reports an empty array", () => {
        assert.ok(renderArray([]).includes("empty array"));
    });

    test("refuses to render arrays larger than the display limit", () => {
        const big = Array.from({ length: 201 }, (_, i) => i);
        assert.ok(renderArray(big).includes("too large to display"));
    });
});

suite("renderObject (structured)", () => {
    test("renders fields and values from a parsed object", () => {
        const html = renderObject({ name: "x", age: 3 });
        assert.ok(html.includes("<strong>name</strong>"));
        assert.ok(html.includes("<td>x</td>"));
        assert.ok(html.includes("<strong>age</strong>"));
        assert.ok(html.includes("<td>3</td>"));
    });

    test("reports an empty record", () => {
        assert.ok(renderObject({}).includes("empty record"));
    });
});

suite("renderValue", () => {
    test("renders a JSON array string via the structured path", () => {
        const html = renderValue("xs", { result: "[10, 20, 30]", type: "array<integer>" });
        assert.ok(html.includes('class="bar"'));
    });

    test("does not split values that contain commas (JSON object)", () => {
        const html = renderValue("rec", { result: '{"a":"x,y","b":2}', type: "record" });
        // The comma inside the string must stay in a single cell.
        assert.ok(html.includes("<td>x,y</td>"), "value with comma should be one cell");
        assert.ok(html.includes("<strong>a</strong>"));
        assert.ok(html.includes("<strong>b</strong>"));
    });

    test("falls back to a loose record renderer for unquoted keys", () => {
        const html = renderValue("rec", { result: "{name: hello, age: 3}", type: "record" });
        assert.ok(html.includes("<strong>name</strong>"));
        assert.ok(html.includes("<td>hello</td>"));
        assert.ok(html.includes("<strong>age</strong>"));
    });

    test("loose record splitting respects nested braces", () => {
        const html = renderValue("rec", { result: "{point: {x:1, y:2}, n: 3}", type: "record" });
        // Two top-level fields: point and n (the inner comma must not split).
        assert.ok(html.includes("<strong>point</strong>"));
        assert.ok(html.includes("<strong>n</strong>"));
        assert.ok(
            !html.includes("<strong>y</strong>"),
            "nested key must not become a top-level field",
        );
    });

    test("renders a plain scalar with its type label", () => {
        const html = renderValue("n", { result: "42", type: "integer" });
        assert.ok(html.includes('class="scalar"'));
        assert.ok(html.includes("42"));
    });

    test("escapes HTML in scalar values", () => {
        const html = renderValue("s", { result: "<script>", type: "string" });
        assert.ok(html.includes("&lt;script&gt;"));
        assert.ok(!html.includes("<script>"));
    });
});
