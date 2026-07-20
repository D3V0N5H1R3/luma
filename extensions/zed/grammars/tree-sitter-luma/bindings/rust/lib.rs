//! This crate provides Luma language support for the [tree-sitter][] parsing library.
//!
//! Typically, you will use the [LANGUAGE][] constant to add this language to a
//! tree-sitter [Parser][], and then use the parser to parse some code:
//!
//! ```
//! let code = r#"
//! "#;
//! let mut parser = tree_sitter::Parser::new();
//! let language = tree_sitter_luma::LANGUAGE;
//!
//! parser
//!     .set_language(&language.into())
//!     .expect("Error loading Luma parser");
//!
//! let tree = parser.parse(code, None).unwrap();
//!
//! assert!(!tree.root_node().has_error());
//! ```
//!
//! [Parser]: https://docs.rs/tree-sitter/*/tree_sitter/struct.Parser.html
//! [tree-sitter]: https://tree-sitter.github.io/

use tree_sitter_language::LanguageFn;

extern "C" {
    fn tree_sitter_luma() -> *const ();
}

/// The tree-sitter [`LanguageFn`][LanguageFn] for this grammar.
///
/// [LanguageFn]: https://docs.rs/tree-sitter-language/*/tree_sitter_language/struct.LanguageFn.html
pub const LANGUAGE: LanguageFn = unsafe { LanguageFn::from_raw(tree_sitter_luma) };

/// The content of the [`node-types.json`][] file for this grammar.
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers#static-node-types
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");

// NOTE: uncomment these to include any queries that this grammar contains:

// pub const HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");
// pub const INJECTIONS_QUERY: &str = include_str!("../../queries/injections.scm");
// pub const LOCALS_QUERY: &str = include_str!("../../queries/locals.scm");
// pub const TAGS_QUERY: &str = include_str!("../../queries/tags.scm");

#[cfg(test)]
mod tests {
    use streaming_iterator::StreamingIterator;
    use tree_sitter::{Parser, Query};

    fn make_parser() -> Parser {
        let mut parser = Parser::new();
        parser
            .set_language(&super::LANGUAGE.into())
            .expect("Error loading Luma parser");
        parser
    }

    #[test]
    fn test_can_load_grammar() {
        make_parser();
    }

    // ── Parse fixture tests ───────────────────────────────────────

    fn parse_and_check(name: &str, code: &str) {
        let mut parser = make_parser();
        let tree = parser
            .parse(code, None)
            .expect(&format!("Failed to parse {name}"));
        let root = tree.root_node();
        assert!(
            !root.has_error(),
            "Parse errors in {name}: {root_s}",
            root_s = root.to_sexp()
        );
    }

    #[test]
    fn parse_function_declaration() {
        parse_and_check(
            "function",
            r#"
function void greet() {
    print("hello")
}
"#,
        );
    }

    #[test]
    fn parse_record_declaration() {
        parse_and_check(
            "record",
            r#"
record Point {
    number x,
    number y
}
"#,
        );
    }

    #[test]
    fn parse_choice_declaration() {
        parse_and_check(
            "choice",
            r#"
choice Shape {
    Circle(number radius)
    Rectangle(number width, number height)
    Point
}
"#,
        );
    }

    #[test]
    fn parse_interface_declaration() {
        parse_and_check(
            "interface",
            r#"
interface Named {
    string name
}
"#,
        );
    }

    #[test]
    fn parse_namespace_declaration() {
        parse_and_check(
            "namespace",
            r#"
namespace Utils {
    function integer add(integer a, integer b) {
        return a + b
    }
}
"#,
        );
    }

    #[test]
    fn parse_control_flow() {
        parse_and_check(
            "control_flow",
            r#"
function void main() {
    mutable integer count = 0
    if count == 0 {
        count = 1
    } else {
        count = 2
    }
    for item in [1, 2, 3] {
        if item > 2 {
            break
        }
        continue
    }
    while count < 10 {
        count++
    }
    return
}
"#,
        );
    }

    #[test]
    fn parse_match_expression() {
        parse_and_check(
            "match",
            r#"
function string describe(integer x) {
    return match x {
        case 0 { "zero" }
        case 1 { "one" }
        else { "other" }
    }
}
"#,
        );
    }

    #[test]
    fn parse_string_interpolation() {
        parse_and_check(
            "interpolation",
            r#"
function void greet(string name) {
    print("Hello, ${name}!")
}
"#,
        );
    }

    #[test]
    fn parse_triple_quoted_string() {
        parse_and_check(
            "triple_string",
            r#"
string text = """
    multi-line
    string
"""
"#,
        );
    }

    #[test]
    fn parse_lambda_expression() {
        parse_and_check(
            "lambda",
            r#"
function void main() {
    array<integer> doubled = Result.unwrap(Array.map([1, 2, 3], (integer x) -> x * 2))
}
"#,
        );
    }

    #[test]
    fn parse_generics() {
        parse_and_check(
            "generics",
            r#"
function optional<integer> find_first(array<integer> items) {
    return Array.first(items) |> Result.to_optional()
}
"#,
        );
    }

    #[test]
    fn parse_concurrency() {
        parse_and_check(
            "concurrency",
            r#"
function void main() {
    task_scope {
        task<integer> t = spawn compute(42)
        integer result = await t
    }
}
"#,
        );
    }

    #[test]
    fn parse_try_catch() {
        parse_and_check(
            "try_catch",
            r#"
function void main() {
    try {
        integer x = 1 / 0
    } catch(error) {
        print("caught")
    } finally {
        print("done")
    }
}
"#,
        );
    }

    #[test]
    fn parse_annotations() {
        parse_and_check(
            "annotations",
            r#"
@main
function void main() {
    print("hello")
}

@test
function void test_a() {
    assert(true)
}
"#,
        );
    }

    #[test]
    fn parse_type_alias() {
        parse_and_check(
            "type_alias",
            r#"
type StringList = array<string>
"#,
        );
    }

    #[test]
    fn parse_operators() {
        parse_and_check(
            "operators",
            r#"
function void main() {
    integer a = 1 + 2 * 3 - 4 / 2
    boolean b = a > 0 && a < 100 || a == 42
    integer c = a % 7
    string s = "hello" |> String.uppercase()
    integer d = 0xFF
    integer e = 0b1010
}
"#,
        );
    }

    #[test]
    fn parse_dictionary_literal() {
        parse_and_check(
            "dictionary",
            r#"
function void main() {
    dictionary<string> d = {"name": "Alice", "city": "Berlin"}
}
"#,
        );
    }

    #[test]
    fn parse_result_constructors() {
        parse_and_check(
            "result_constructors",
            r#"
function void main() {
    result<integer> ok = success(42)
    result<integer> err = failure("oops")
    optional<integer> opt = some(1)
}
"#,
        );
    }

    // ── Query syntax validation ───────────────────────────────────

    fn load_query(query_source: &str, name: &str) {
        let lang: tree_sitter::Language = super::LANGUAGE.into();
        Query::new(&lang, query_source).unwrap_or_else(|e| {
            panic!("Query parse error in {name}: {e}");
        });
    }

    // The tree-sitter Rust API (0.23–0.24) has stricter "impossible pattern"
    // validation than the CLI for queries that combine anonymous terminal
    // matches with certain node structures.  The Zed highlights query uses
    // patterns like (argument (identifier) ":") which are valid in the editor
    // but rejected by the Rust API.  To avoid false positives,
    // highlights queries are validated via the tree-sitter CLI in
    // extensions/tests/validate_queries.js.  All other queries (folds,
    // outline, brackets, etc.) load fine through the Rust API.

    #[test]
    fn zed_folds_query_parses() {
        let src = include_str!("../../../../languages/luma/folds.scm");
        load_query(src, "zed/folds.scm");
    }

    #[test]
    fn zed_indents_query_parses() {
        let src = include_str!("../../../../languages/luma/indents.scm");
        load_query(src, "zed/indents.scm");
    }

    #[test]
    fn zed_outline_query_parses() {
        let src = include_str!("../../../../languages/luma/outline.scm");
        load_query(src, "zed/outline.scm");
    }

    #[test]
    fn zed_brackets_query_parses() {
        let src = include_str!("../../../../languages/luma/brackets.scm");
        load_query(src, "zed/brackets.scm");
    }

    #[test]
    fn zed_runnables_query_parses() {
        let src = include_str!("../../../../languages/luma/runnables.scm");
        load_query(src, "zed/runnables.scm");
    }

    #[test]
    fn zed_injections_query_parses() {
        let src = include_str!("../../../../languages/luma/injections.scm");
        load_query(src, "zed/injections.scm");
    }

    #[test]
    fn zed_overrides_query_parses() {
        let src = include_str!("../../../../languages/luma/overrides.scm");
        load_query(src, "zed/overrides.scm");
    }

    #[test]
    fn zed_redactions_query_parses() {
        let src = include_str!("../../../../languages/luma/redactions.scm");
        load_query(src, "zed/redactions.scm");
    }

    #[test]
    fn zed_textobjects_query_parses() {
        let src = include_str!("../../../../languages/luma/textobjects.scm");
        load_query(src, "zed/textobjects.scm");
    }

    // ── Highlight capture integration tests ───────────────────────
    //
    // These use a curated subset of the highlights query that avoids
    // patterns the tree-sitter Rust API rejects as "impossible".

    const HIGHLIGHTS_SUBSET: &str = r#"
(comment) @comment
(annotation) @attribute
(string) @string
(triple_string) @string
(string_content) @string
(triple_string_content) @string
(string_escape) @string.escape
(interpolation "${" @string.special "}" @string.special)
(hex_literal) @number
(binary_literal) @number
(float_literal) @number
(integer_literal) @number
(boolean_literal) @constant.builtin
(none_literal) @constant.builtin
(constructor_expression ["success" "failure" "some"] @constructor)
["if" "else" "for" "while" "in" "return" "match" "case" "try" "catch" "finally"] @keyword.control
(break_statement) @keyword.control
(continue_statement) @keyword.control
["function" "record" "choice" "interface" "namespace" "type" "include" "use" "spawn" "await" "task_scope"] @keyword
["mutable" "unique" "borrow" "internal"] @keyword.modifier
"with" @keyword
(builtin_type) @type.builtin
(function_declaration name: (function_name) @function)
(variable_declaration (identifier) @variable)
(parameter (identifier) @variable.parameter)
(type_identifier) @type
(identifier) @variable
"#;

    fn highlights_capture_names(code: &str) -> Vec<String> {
        let mut parser = make_parser();
        let tree = parser.parse(code, None).unwrap();
        let lang: tree_sitter::Language = super::LANGUAGE.into();
        let query = Query::new(&lang, HIGHLIGHTS_SUBSET).unwrap();
        let mut cursor = tree_sitter::QueryCursor::new();
        let mut matches = cursor.matches(&query, tree.root_node(), code.as_bytes());

        let mut names: Vec<String> = Vec::new();
        while let Some(m) = matches.next() {
            for cap in m.captures {
                let name = query.capture_names()[cap.index as usize].to_string();
                if !names.contains(&name) {
                    names.push(name);
                }
            }
        }
        names
    }

    #[test]
    fn highlight_captures_comment() {
        let names = highlights_capture_names("# a comment\n");
        assert!(
            names.contains(&"comment".to_string()),
            "expected 'comment' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_string() {
        let names = highlights_capture_names("string s = \"hello\"\n");
        assert!(
            names.contains(&"string".to_string()),
            "expected 'string' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_number() {
        let names = highlights_capture_names("integer x = 42\n");
        assert!(
            names.contains(&"number".to_string()),
            "expected 'number' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_keyword_control() {
        let names = highlights_capture_names("function void f() {\n  if true { return }\n}\n");
        assert!(
            names.contains(&"keyword.control".to_string()),
            "expected 'keyword.control' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_function_name() {
        let names = highlights_capture_names("function void greet() {}\n");
        assert!(
            names.contains(&"function".to_string()),
            "expected 'function' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_type_builtin() {
        let names = highlights_capture_names("integer count = 0\n");
        assert!(
            names.contains(&"type.builtin".to_string()),
            "expected 'type.builtin' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_attribute() {
        let names = highlights_capture_names("@test\nfunction void test_a() {}\n");
        assert!(
            names.contains(&"attribute".to_string()),
            "expected 'attribute' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_keyword_modifier() {
        let names = highlights_capture_names("mutable integer x = 0\n");
        assert!(
            names.contains(&"keyword.modifier".to_string()),
            "expected 'keyword.modifier' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_constant_builtin() {
        let names = highlights_capture_names("boolean flag = true\n");
        assert!(
            names.contains(&"constant.builtin".to_string()),
            "expected 'constant.builtin' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_variable() {
        let names = highlights_capture_names("integer count = 42\n");
        assert!(
            names.contains(&"variable".to_string()),
            "expected 'variable' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_constructor() {
        let names = highlights_capture_names("result<integer> r = success(42)\n");
        assert!(
            names.contains(&"constructor".to_string()),
            "expected 'constructor' in {names:?}"
        );
    }

    #[test]
    fn highlight_captures_string_escape() {
        let names = highlights_capture_names(r#"string s = "hello\nworld""#);
        assert!(
            names.contains(&"string.escape".to_string()),
            "expected 'string.escape' in {names:?}"
        );
    }

    // ── Outline query integration tests ───────────────────────────

    fn outline_capture_names(code: &str) -> Vec<(String, String)> {
        let mut parser = make_parser();
        let tree = parser.parse(code, None).unwrap();
        let lang: tree_sitter::Language = super::LANGUAGE.into();
        let query_src = include_str!("../../../../languages/luma/outline.scm");
        let query = Query::new(&lang, query_src).unwrap();
        let mut cursor = tree_sitter::QueryCursor::new();
        let mut matches = cursor.matches(&query, tree.root_node(), code.as_bytes());

        let mut results = Vec::new();
        while let Some(m) = matches.next() {
            for cap in m.captures {
                let cap_name = query.capture_names()[cap.index as usize].to_string();
                let text = cap
                    .node
                    .utf8_text(code.as_bytes())
                    .unwrap_or("")
                    .to_string();
                results.push((cap_name, text));
            }
        }
        results
    }

    #[test]
    fn outline_captures_function() {
        let items = outline_capture_names("function void greet() {}\n");
        assert!(
            items
                .iter()
                .any(|(cap, text)| cap == "name" && text == "greet"),
            "expected function 'greet' in outline: {items:?}"
        );
    }

    #[test]
    fn outline_captures_record() {
        let items = outline_capture_names("record Point { number x, number y }\n");
        assert!(
            items
                .iter()
                .any(|(cap, text)| cap == "name" && text == "Point"),
            "expected record 'Point' in outline: {items:?}"
        );
    }

    #[test]
    fn outline_captures_choice() {
        let items = outline_capture_names("choice Color { Red, Green, Blue }\n");
        assert!(
            items
                .iter()
                .any(|(cap, text)| cap == "name" && text == "Color"),
            "expected choice 'Color' in outline: {items:?}"
        );
    }

    #[test]
    fn outline_captures_annotated_function() {
        let items = outline_capture_names("@main\nfunction void main() {}\n");
        assert!(
            items
                .iter()
                .any(|(cap, text)| cap == "name" && text == "main"),
            "expected annotated function 'main' in outline: {items:?}"
        );
        assert!(
            items
                .iter()
                .any(|(cap, text)| cap == "context" && text == "@main"),
            "expected annotation context '@main' in outline: {items:?}"
        );
    }

    // ── Runnables query integration tests ─────────────────────────

    fn runnables_capture_names(code: &str) -> Vec<(String, String)> {
        let mut parser = make_parser();
        let tree = parser.parse(code, None).unwrap();
        let lang: tree_sitter::Language = super::LANGUAGE.into();
        let query_src = include_str!("../../../../languages/luma/runnables.scm");
        let query = Query::new(&lang, query_src).unwrap();
        let mut cursor = tree_sitter::QueryCursor::new();
        let mut matches = cursor.matches(&query, tree.root_node(), code.as_bytes());

        let mut results = Vec::new();
        while let Some(m) = matches.next() {
            for cap in m.captures {
                let cap_name = query.capture_names()[cap.index as usize].to_string();
                let text = cap
                    .node
                    .utf8_text(code.as_bytes())
                    .unwrap_or("")
                    .to_string();
                results.push((cap_name, text));
            }
        }
        results
    }

    #[test]
    fn runnables_detects_main() {
        let items = runnables_capture_names("@main\nfunction void main() {}\n");
        assert!(
            items
                .iter()
                .any(|(cap, text)| cap == "run" && text == "main"),
            "expected runnable 'main' in {items:?}"
        );
    }

    #[test]
    fn runnables_detects_test() {
        let items = runnables_capture_names("@test\nfunction void test_foo() {}\n");
        assert!(
            items
                .iter()
                .any(|(cap, text)| cap == "run" && text == "test_foo"),
            "expected runnable 'test_foo' in {items:?}"
        );
    }

    #[test]
    fn runnables_ignores_unannotated() {
        let items = runnables_capture_names("function void helper() {}\n");
        assert!(
            items.is_empty(),
            "unannotated function should not be runnable: {items:?}"
        );
    }
}
