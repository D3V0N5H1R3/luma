#ifndef LUMA_LSP_ANALYSIS_RESULT_HPP
#define LUMA_LSP_ANALYSIS_RESULT_HPP

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/lexer/token.hpp"
#include "analysis/source/source_location.hpp"
#include "common/string_hash.hpp"
#include "lsp_optional_ref.hpp"
#include "lsp_position_utils.hpp"
#include "lsp_token_index.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// Prefix used to store interface definitions as synthetic record entries.
inline constexpr std::string_view k_interface_record_prefix = "__iface__";

// Structured parameter info (name + type string).
struct ParamInfo {
    std::string name;
    std::string type_string;
};

// Information about a user-defined function extracted from the AST.
struct UserFunctionInfo {
    std::string signature;             // e.g., "function greet(name: string) -> string"
    std::string return_type;           // e.g., "string" — empty if no return type declared
    std::string params_signature;      // e.g., "(name: string)" — for signature help
    std::vector<ParamInfo> parameters; // structured parameter list
    SourceLocation location;           // declaration location (leading `function` keyword)
    // Range of the function NAME token — precomputed once in build_token_index
    // because `location` points at the `function` keyword, not the name. Stored
    // by value so it survives result copies/moves (unlike the raw-pointer-backed
    // line index); the semantic-token classifier reads it O(1) per token rather
    // than rescanning the stream (which is quadratic over the document). The
    // {-1,-1} sentinel means "not computed" and can never match a real token.
    Range name_range{Position{-1, -1}, Position{-1, -1}};
};

// Symbol definition location and type string (for hover / go-to-definition).
struct SymbolDefinition {
    SourceLocation location; // 1-based line/column of the name token
    std::string type_string; // human-readable type, e.g. "record", "integer"
    bool is_mutable{false};
};

// Record definition with field names and type strings.
struct RecordInfo {
    SourceLocation location;
    std::vector<std::pair<std::string, std::string>> fields; // (name, type_string)
};

// Index entry for fast token lookup by position.
struct TokenIndexEntry {
    int line;              // 1-based
    int col_start;         // 1-based, inclusive
    int col_end;           // 1-based, exclusive
    std::size_t token_idx; // index into the token vector
};

// A local variable with its block-level scope range (1-based, inclusive).
struct ScopedLocalVar {
    std::string type_string;
    int scope_start_line; // 1-based inclusive
    int scope_end_line;   // 1-based inclusive
};

// Cached analysis results per document URI.

// ─── Sub-structures for SemanticAnalysis ───

// Top-level symbol definitions, user functions, and record/choice declarations.
struct SymbolTable {
    // All top-level named declarations → definition location + type.
    StringMap<SymbolDefinition> definitions;
    // User-defined functions: qualified name → UserFunctionInfo.
    StringMap<UserFunctionInfo> user_functions;
    // Record definitions (record name → RecordInfo with fields).
    StringMap<RecordInfo> record_definitions;
    // Choice type variants (choice name → list of variant names).
    StringMap<std::vector<std::string>> choice_variants;
    // Doc comments extracted from source: symbol name → comment text.
    StringMap<std::string> doc_comments;
    // Reverse map: short function name → list of qualified names.
    // Built during symbol_phase for O(1) ns-qualified lookup in classify_token.
    StringMap<std::vector<std::string>> function_short_names;
    // Interface implementations: interface name → list of record names.
    StringMap<std::vector<std::string>> interface_implementations;
};

// Local variable declarations and scope information.
struct LocalVariableInfo {
    // Local variable declarations found in function bodies (name → type_string).
    StringMap<std::string> local_variable_types;
    // Block-scoped local variables: function name → (var name → scoped entries).
    // Multiple entries for the same var arise when the same name is declared
    // in sibling blocks (e.g. separate if/else branches). Keying by function
    // first lets block-scope collection touch only one function's locals.
    StringMap<StringMap<std::vector<ScopedLocalVar>>> scoped_locals;
    // Mutable local variables (function_name + ":" + var_name, or just var_name).
    StringSet mutable_locals;
    // Per-function local variables (function name → {var name → type string}).
    StringMap<StringMap<std::string>> function_locals;
};

// Function body ranges, call graph, and enclosing-function lookup data.
struct FunctionStructure {
    // Call graph: caller function name → set of callee function names.
    StringMap<StringSet> call_graph;
    // Function body ranges (function name → token line range [start, end] inclusive, 1-based).
    StringMap<std::pair<int, int>> function_body_ranges;

    // Sorted function ranges for O(log n) enclosing-function lookup.
    // Built from function_body_ranges after symbol collection.
    struct FunctionRange {
        int start_line;
        int end_line;
        std::string name;
    };

    std::vector<FunctionRange> sorted_function_ranges;
};

// Include dependencies and symbol origin tracking.
struct IncludeInfo {
    // Include dependencies for this document (resolved file paths).
    std::vector<std::string> included_paths;
    // Include paths found in the source (for document links).
    std::vector<std::pair<std::string, SourceLocation>> include_literals;
    // Symbol origin: symbol name → source file path (for symbols from includes).
    StringMap<std::string> symbol_origins;
    // File ID → file path mapping (for include origin tracking).
    std::unordered_map<int, std::string> file_id_to_path;
    // File ID counter for included files (starts at 1; 0 = main document).
    int next_file_id{1};
};

// ─── Core semantic analysis outputs ───
struct SemanticAnalysis {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;

    SymbolTable symbols;
    LocalVariableInfo locals;
    FunctionStructure functions;
    IncludeInfo includes;

    // Backward-compatible type alias.
    using FunctionRange = FunctionStructure::FunctionRange;
};

// ─── Caching and indexing metadata ───
struct AnalysisMetadata {
    std::vector<TokenIndexEntry> token_index; // sorted by (line, col_start)
    TokenIndex line_index;                    // per-line O(1) lookup into tokens
    // Identifier location index: lexeme → list of token indices.
    StringMap<std::vector<std::size_t>> identifier_index;
    // Cached AST from parsing (avoids re-parse in handle_document_symbol).
    std::optional<Program> cached_program;
    // Semantic token data (for full responses and delta computation).
    std::vector<int64_t> semantic_token_data;
    // Result ID for semantic tokens delta protocol.
    std::string semantic_token_result_id;
    // Content hash of the source when semantic_token_data was last computed.
    // Used to validate the cache without re-running classification.
    std::size_t source_hash{0};
    // LSP document version when semantic_token_data was last computed.
    int64_t document_version{-1};
    // The exact source text analysed, retained so request/response positions can
    // be translated between the lexer's codepoint columns and LSP's UTF-16
    // columns (see PositionEncoder). Kept per document; one copy per cached
    // analysis.
    std::string source_text;
    // Byte offset of the start of each line in `source_text` (0-based line
    // index). Precomputed during analysis for O(1) line slicing in the encoder.
    std::vector<std::size_t> line_starts;
    // Set when analysis aborted because a newer edit requested cancellation
    // (as opposed to exceeding the deadline). A cancelled result is stale by
    // definition — the caller re-schedules the URI and skips publishing so no
    // misleading "timed out" warning surfaces to the user.
    bool cancelled{false};
};

// ─── Combined result (backward-compatible) ───
struct AnalysisResult {
    SemanticAnalysis semantic;
    AnalysisMetadata metadata;

    // Type alias for backward compatibility.
    using FunctionRange = SemanticAnalysis::FunctionRange;

    // ─── Preferred accessors (use these over direct field access in new code) ───

    [[nodiscard]] const std::vector<Token>& get_tokens() const {
        return semantic.tokens;
    }

    [[nodiscard]] const std::vector<Diagnostic>& get_diagnostics() const {
        return semantic.diagnostics;
    }

    [[nodiscard]] bool has_diagnostics() const {
        return !semantic.diagnostics.empty();
    }

    // Look up a user-defined function by name.
    [[nodiscard]] optional_ref<const UserFunctionInfo>
    find_function(const std::string& name) const {
        auto it = semantic.symbols.user_functions.find(name);
        if (it != semantic.symbols.user_functions.end()) {
            return it->second;
        }
        return {};
    }

    // Look up a top-level symbol definition by name.
    [[nodiscard]] optional_ref<const SymbolDefinition>
    find_definition(const std::string& name) const {
        auto it = semantic.symbols.definitions.find(name);
        if (it != semantic.symbols.definitions.end()) {
            return it->second;
        }
        return {};
    }

    // Look up a record definition by name.
    [[nodiscard]] optional_ref<const RecordInfo> find_record(const std::string& name) const {
        auto it = semantic.symbols.record_definitions.find(name);
        if (it != semantic.symbols.record_definitions.end()) {
            return it->second;
        }
        return {};
    }

    // ─── Codepoint ↔ UTF-16 position encoding (LSP wire boundary) ───

    // A PositionEncoder bound to this document's analysed source. Use it to
    // translate token/location geometry (codepoint columns) to the UTF-16
    // columns the LSP wire protocol expects, and incoming request positions the
    // other way. Valid only while this result is alive (under the read lock).
    [[nodiscard]] PositionEncoder encoder() const {
        return PositionEncoder{&metadata.source_text, &metadata.line_starts};
    }

    // Convert an internal codepoint-column Range/Position to a UTF-16 one for a
    // response sent to the client.
    [[nodiscard]] Range to_wire(const Range& codepoint_range) const {
        return encoder().to_utf16(codepoint_range);
    }

    [[nodiscard]] Position to_wire(const Position& codepoint_pos) const {
        return encoder().to_utf16(codepoint_pos);
    }

    // Convert an incoming UTF-16 column (0-based) on a 0-based line to the
    // codepoint column used internally.
    [[nodiscard]] int to_codepoint_col(int line0, int utf16_col0) const {
        return encoder().to_codepoint(line0, utf16_col0);
    }
};

// ─── Include file token cache ───
// Keyed by resolved file system path.  Used to avoid re-lexing included
// files when their content has not changed between analyses.
struct IncludeCache {
    std::size_t content_hash{0};
    std::vector<Token> cached_tokens;
};

// Shared, immutable handle to a cached include's tokens.  The analysis
// service stores these in its LRU cache and threads them through include
// processing, so snapshotting the cache copies pointers rather than whole
// token vectors, and cache hits reuse the tokens without a deep copy.
using IncludeCachePtr = std::shared_ptr<const IncludeCache>;

// Map from resolved include file path to its cached token handle.
using IncludeCacheMap = StringMap<IncludeCachePtr>;

} // namespace luma::lsp

#endif // LUMA_LSP_ANALYSIS_RESULT_HPP
