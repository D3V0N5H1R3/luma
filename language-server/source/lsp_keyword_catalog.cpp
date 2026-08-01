#include "lsp_keyword_catalog.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace luma::lsp {

// SYNC: This list must match the keywords defined in
// core/analysis/lexer/lexer.cpp (Lexer::keyword_type). If you add or remove
// keywords there, update this list accordingly. The size is checked at
// compile time; the lsp_test_unit "keyword catalog" test additionally lexes
// each entry to confirm the lexer still treats it as a keyword, guarding
// against silent drift between this list and the lexer.
inline constexpr std::array<std::string_view, 47> k_reserved_keyword_names = {
    // Keywords
    "await",
    "borrow",
    "break",
    "case",
    "catch",
    "choice",
    "continue",
    "downcast",
    "else",
    "failure",
    "false",
    "finally",
    "for",
    "function",
    "if",
    "in",
    "include",
    "interface",
    "internal",
    "is",
    "match",
    "mutable",
    "namespace",
    "none",
    "record",
    "return",
    "some",
    "spawn",
    "success",
    "task_scope",
    "true",
    "trusted_downcast",
    "try",
    "type",
    "unique",
    "use",
    "while",
    "with",
    // Built-in type keywords (container/handle types were demoted to ordinary
    // identifiers in R02, so they are no longer reserved).
    "array",
    "boolean",
    "decimal",
    "dictionary",
    "integer",
    "number",
    "optional",
    "result",
    "string",
};

static_assert(k_reserved_keyword_names.size() == 47,
              "Reserved keyword count drifted — sync with Lexer::keyword_type "
              "in core/analysis/lexer/lexer.cpp");

bool is_reserved_keyword_name(std::string_view name) {
    // Mirrors the lexer's keyword map (lexer.cpp keyword_type()).
    static const std::unordered_set<std::string_view> reserved(k_reserved_keyword_names.begin(),
                                                               k_reserved_keyword_names.end());

    return reserved.contains(name);
}

std::span<const std::string_view> reserved_keyword_names() {
    return k_reserved_keyword_names;
}

std::vector<std::pair<std::string, std::string>> get_type_keywords() {
    return {
        {"boolean", "primitive type"},
        {"integer", "primitive type"},
        {"number", "primitive type"},
        {"decimal", "exact base-10 number"},
        {"string", "primitive type"},
        {"void", "return type"},
        {"array", "generic container"},
        {"dictionary", "generic container"},
        {"optional", "generic wrapper"},
        {"result", "generic wrapper"},
        {"channel", "concurrency"},
        {"task", "concurrency"},
        {"reference", "generic wrapper"},
        {"set", "ordered collection"},
        {"stack", "LIFO collection"},
        {"queue", "FIFO collection"},
        {"key_value_store", "persistent store"},
        {"socket", "network I/O"},
        {"widget", "graphical UI"},
        {"xml", "structured data"},
    };
}

const std::vector<KeywordInfo>& keyword_catalog() {
    // clang-format off
    static const std::vector<KeywordInfo> catalog = {
        // ── Concurrency ──
        {.name = "await",
         .hover_doc = "```luma\nawait\n```\n\nWait for a `task<T>` to complete and obtain its result.",
         .snippet = "await $0",
         .detail = "Wait for a task",
         .context = KeywordContext::Always},
        {.name = "spawn",
         .hover_doc = "```luma\nspawn\n```\n\nSpawn a concurrent task: `spawn function_call()` returns "
                      "a `task<T>`.",
         .snippet = "spawn $0",
         .detail = "Spawn a concurrent task",
         .context = KeywordContext::Always},
        {.name = "task_scope",
         .hover_doc = "```luma\ntask_scope\n```\n\nStructured concurrency block: `task_scope { spawn ... }` "
                      "awaits all child tasks and returns an `array<T>` of results "
                      "in spawn order. Cancels remaining siblings on first failure.",
         .snippet = "task_scope {\n\t$0\n}",
         .detail = "Structured concurrency block",
         .context = KeywordContext::Always},
        // ── Control flow ──
        {.name = "break",
         .hover_doc = "```luma\nbreak\n```\n\nExit the enclosing `for` or `while` loop immediately.",
         .snippet = "",
         .detail = "Exit the enclosing loop",
         .context = KeywordContext::Function},
        {.name = "case",
         .hover_doc = "```luma\ncase\n```\n\nA pattern arm inside a `match` expression.\n\n"
                      "Supports booleans (`case true`), integers (`case 1`), "
                      "strings (`case \"quit\"`), comparisons (`case >= 90`), "
                      "choice variants (`case Color.Red`), `some(x)`, and `none`.",
         .snippet = "",
         .detail = "Pattern arm in a match expression",
         .context = KeywordContext::Always},
        {.name = "continue",
         .hover_doc = "```luma\ncontinue\n```\n\nSkip the rest of the current loop iteration and proceed "
                      "with the next one.",
         .snippet = "",
         .detail = "Skip to next loop iteration",
         .context = KeywordContext::Function},
        {.name = "for",
         .hover_doc = "```luma\nfor\n```\n\nIterate over a collection or range: "
                      "`for item in collection { ... }`.",
         .snippet = "for ${1:item} in ${2:collection} {\n\t$0\n}",
         .detail = "Iterate over a collection",
         .context = KeywordContext::Always},
        {.name = "for/kv",
         .hover_doc = "",
         .snippet = "for ${1:key}, ${2:value} in ${3:dictionary} {\n\t$0\n}",
         .detail = "Iterate with key and value",
         .context = KeywordContext::Always},
        {.name = "for/range",
         .hover_doc = "",
         .snippet = "for ${1:i} in ${2:0}..${3:10} {\n\t$0\n}",
         .detail = "Iterate over a numeric range",
         .context = KeywordContext::Always},
        {.name = "if",
         .hover_doc = "```luma\nif\n```\n\nConditional expression. Evaluates the `then` branch when "
                      "the condition is `true`, otherwise evaluates the optional `else` branch.",
         .snippet = "if ${1:condition} {\n\t$0\n}",
         .detail = "Conditional expression",
         .context = KeywordContext::Always},
        {.name = "if/else",
         .hover_doc = "",
         .snippet = "if ${1:condition} {\n\t$0\n} else {\n\t\n}",
         .detail = "Conditional expression with else branch",
         .context = KeywordContext::Always},
        {.name = "in",
         .hover_doc = "```luma\nin\n```\n\nSpecifies the collection in a `for` loop.",
         .snippet = "",
         .detail = "Collection specifier in for loop",
         .context = KeywordContext::Always},
        {.name = "match",
         .hover_doc = "```luma\nmatch\n```\n\nPattern-matching expression. Tests a value against "
                      "one or more `case` patterns.",
         .snippet = "match ${1:value} {\n\tcase ${2:pattern} {\n\t\t$0\n\t}\n}",
         .detail = "Pattern-matching expression",
         .context = KeywordContext::Always},
        {.name = "match/optional",
         .hover_doc = "",
         .snippet = "match ${1:value} {\n\tcase some(${2:v}) {\n\t\t$0\n\t}\n\tcase none {\n\t\t\n\t}\n}",
         .detail = "Match on optional type",
         .context = KeywordContext::Always},
        {.name = "match/result",
         .hover_doc = "",
         .snippet = "match ${1:value} {\n\tcase success(${2:s}) {\n\t\t$0\n\t}\n\tcase "
                    "failure(${3:err}) {\n\t\t\n\t}\n}",
         .detail = "Match on result type",
         .context = KeywordContext::Always},
        {.name = "return",
         .hover_doc = "```luma\nreturn\n```\n\nReturn a value from the enclosing function.",
         .snippet = "return $0",
         .detail = "Return a value",
         .context = KeywordContext::Function},
        {.name = "while",
         .hover_doc = "```luma\nwhile\n```\n\nRepeat a block while a condition remains `true`.",
         .snippet = "while ${1:condition} {\n\t$0\n}",
         .detail = "Loop while condition is true",
         .context = KeywordContext::Always},
        // ── Declarations ──
        {.name = "choice",
         .hover_doc = "```luma\nchoice\n```\n\nDeclare a sum type (discriminated union / ADT): "
                      "`choice Name { Variant(type) }`.",
         .snippet = "choice ${1:Name} {\n\t${2:Variant}\n}",
         .detail = "Declare a sum type (ADT)",
         .context = KeywordContext::Declaration},
        {.name = "function",
         .hover_doc = "```luma\nfunction\n```\n\nDeclare a named function: "
                      "`function name(param: type) -> return_type { body }`.",
         .snippet = "function ${1:void} ${2:name}(${3:}) {\n\t$0\n}",
         .detail = "Declare a function",
         .context = KeywordContext::Declaration},
        {.name = "interface",
         .hover_doc = "```luma\ninterface\n```\n\nDeclare an interface (structural contract for records): "
                      "`interface Name { function_signature }`.",
         .snippet = "interface ${1:Name} {\n\t$0\n}",
         .detail = "Declare an interface",
         .context = KeywordContext::Declaration},
        {.name = "internal",
         .hover_doc = "```luma\ninternal\n```\n\nRestrict visibility of a declaration to the current file.",
         .snippet = "",
         .detail = "Restrict visibility to current file",
         .context = KeywordContext::Declaration},
        {.name = "mutable",
         .hover_doc = "```luma\nmutable\n```\n\nMark a variable as mutable. "
                      "Without `mutable`, variables are immutable by default.",
         .snippet = "mutable ${1:type} ${2:name} = $0",
         .detail = "Mutable variable declaration",
         .context = KeywordContext::Always},
        {.name = "namespace",
         .hover_doc = "```luma\nnamespace\n```\n\nGroup related declarations under a qualified name.",
         .snippet = "namespace ${1:Name} {\n\t$0\n}",
         .detail = "Group declarations",
         .context = KeywordContext::Declaration},
        {.name = "record",
         .hover_doc = "```luma\nrecord\n```\n\nDeclare a product type (struct-like): "
                      "`record Name { field: type }`.",
         .snippet = "record ${1:Name} {\n\t${2:string} ${3:field}\n}",
         .detail = "Declare a product type",
         .context = KeywordContext::Declaration},
        {.name = "type",
         .hover_doc = "```luma\ntype\n```\n\nDeclare a type alias: `type Name = existing_type`.",
         .snippet = "type ${1:Name} = ${2:type}",
         .detail = "Declare a type alias",
         .context = KeywordContext::Declaration},
        // ── Error handling ──
        {.name = "catch",
         .hover_doc = "```luma\ncatch\n```\n\nHandle an error thrown by a `try` block.",
         .snippet = "catch(${1:error}) {\n\t$0\n}",
         .detail = "Catch error handler",
         .context = KeywordContext::AfterBrace},
        {.name = "failure",
         .hover_doc = "```luma\nfailure\n```\n\nWrap an error message in a `result<T>` as a failure "
                      "variant: `failure(\"message\")`.",
         .snippet = "failure(\"$0\")",
         .detail = "Wrap a message in failure()",
         .context = KeywordContext::Function},
        {.name = "finally",
         .hover_doc = "```luma\nfinally\n```\n\nBlock that always executes after a `try`/`catch`, "
                      "regardless of whether an error was thrown.",
         .snippet = "finally {\n\t$0\n}",
         .detail = "Finally block",
         .context = KeywordContext::AfterBrace},
        {.name = "success",
         .hover_doc = "```luma\nsuccess\n```\n\nWrap a value in a `result<T>` as a success variant: "
                      "`success(value)`.",
         .snippet = "success($0)",
         .detail = "Wrap a value in success()",
         .context = KeywordContext::Function},
        {.name = "try",
         .hover_doc = "```luma\ntry\n```\n\nExecute a block with error handling. Pairs with "
                      "`catch` and optionally `finally`.",
         .snippet = "try {\n\t$0\n} catch(${1:error}) {\n\t\n}",
         .detail = "Execute with error handling",
         .context = KeywordContext::Always},
        {.name = "try/catch/finally",
         .hover_doc = "",
         .snippet = "try {\n\t$0\n} catch(${1:error}) {\n\t\n} finally {\n\t\n}",
         .detail = "Error handling with cleanup",
         .context = KeywordContext::Always},
        // ── Module system ──
        {.name = "include",
         .hover_doc = "```luma\ninclude\n```\n\nInclude another Luma source file: "
                      "`include \"path/to/file.luma\"`.",
         .snippet = "include \"$0\"",
         .detail = "Include a source file",
         .context = KeywordContext::Declaration},
        {.name = "use",
         .hover_doc = "```luma\nuse\n```\n\nImport names from a namespace into the current scope.",
         .snippet = "",
         .detail = "Import names from a namespace",
         .context = KeywordContext::Declaration},
        // ── Resource management ──
        {.name = "borrow",
         .hover_doc = "```luma\nborrow\n```\n\nBorrow a reference to a value without transferring ownership.",
         .snippet = "",
         .detail = "Borrow a reference",
         .context = KeywordContext::Always},
        {.name = "unique",
         .hover_doc = "```luma\nunique\n```\n\nDeclare a uniquely owned value. Ownership transfers on "
                      "assignment.",
         .snippet = "",
         .detail = "Declare unique ownership",
         .context = KeywordContext::Always},
        {.name = "with",
         .hover_doc = "```luma\nwith\n```\n\nRecord update expression. Returns a copy of the record "
                      "with the specified fields replaced: `point with { x = 10 }`.",
         .snippet = "",
         .detail = "Record update expression",
         .context = KeywordContext::Always},
        // ── Type inspection ──
        {.name = "downcast",
         .hover_doc = "```luma\ndowncast\n```\n\nSafe downcast to a more specific type. Returns `result<T>`.",
         .snippet = "",
         .detail = "Safe downcast to a more specific type",
         .context = KeywordContext::Always},
        {.name = "is",
         .hover_doc = "```luma\nis\n```\n\nRuntime type check: `value is TypeName` returns `boolean`.",
         .snippet = "",
         .detail = "Runtime type check",
         .context = KeywordContext::Always},
        {.name = "some",
         .hover_doc = "```luma\nsome\n```\n\nWrap a value in an `optional<T>`: `some(value)`.",
         .snippet = "some($0)",
         .detail = "Wrap a value in some()",
         .context = KeywordContext::Function},
        {.name = "trusted_downcast",
         .hover_doc = "```luma\ntrusted_downcast\n```\n\nUnchecked downcast. Use only when the type is "
                      "guaranteed at the call site; panics at runtime if the type does not match.",
         .snippet = "",
         .detail = "Unchecked downcast",
         .context = KeywordContext::Always},
        // ── Common patterns / snippets ──
        {.name = "@main",
         .hover_doc = "",
         .snippet = "@main\nfunction void main() {\n\t$0\n}",
         .detail = "Entry point annotation",
         .context = KeywordContext::Declaration},
        {.name = "@test",
         .hover_doc = "",
         .snippet = "@test\nfunction void ${1:test_name}() {\n\t$0\n}",
         .detail = "Test annotation",
         .context = KeywordContext::Declaration},
        {.name = "assert",
         .hover_doc = "",
         .snippet = "assert($0)",
         .detail = "Assert a condition",
         .context = KeywordContext::Function},
        {.name = "channel",
         .hover_doc = "",
         .snippet = "channel<${1:type}> ${2:ch} = Channel.new()\nChannel.send(${2:ch}, "
                    "${3:value})\n${1:type} ${4:received} = Channel.receive(${2:ch})",
         .detail = "Create and use a channel",
         .context = KeywordContext::Function},
        {.name = "lambda",
         .hover_doc = "",
         .snippet = "(${1:params}) -> $0",
         .detail = "Lambda expression",
         .context = KeywordContext::Always},
        {.name = "pipe",
         .hover_doc = "",
         .snippet = "${1:value}\n\t|> ${2:Function}($0)",
         .detail = "Pipe chain",
         .context = KeywordContext::Function},
        {.name = "print",
         .hover_doc = "",
         .snippet = "print(\"$0\")",
         .detail = "Print to stdout",
         .context = KeywordContext::Function},
        {.name = "var",
         .hover_doc = "",
         .snippet = "${1:type} ${2:name} = $0",
         .detail = "Immutable variable declaration",
         .context = KeywordContext::Always},
        // ── Context-dependent keywords (after '}') ──
        {.name = "else",
         .hover_doc = "```luma\nelse\n```\n\nAlternative branch of an `if` expression.",
         .snippet = "else {\n\t$0\n}",
         .detail = "Else branch",
         .context = KeywordContext::AfterBrace},
    };
    // clang-format on

    return catalog;
}

} // namespace luma::lsp
