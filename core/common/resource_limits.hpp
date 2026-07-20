#ifndef LUMA_COMMON_RESOURCE_LIMITS_HPP
#define LUMA_COMMON_RESOURCE_LIMITS_HPP

#include <cstddef>
#include <cstdint>

namespace luma {

// Centralised resource limits to prevent denial-of-service through
// unbounded allocation, deep recursion, or excessive computation.
// These values are intentionally generous for normal programs but
// block pathological inputs from exhausting system resources.
//
// All limits can be overridden via environment variables prefixed with
// LUMA_LIMIT_, e.g. LUMA_LIMIT_MAX_CALL_DEPTH=512.
// Call ResourceLimits::init_from_env() at startup to apply overrides.
//
// Thread safety: The static members are read-only after init_from_env()
// is called once at startup. Concurrent reads are safe; concurrent
// writes (calling init_from_env() from multiple threads) are NOT safe.

// ─── Compile-time limits ───
// Fixed constants known at compile time. These cannot be overridden at runtime
// because they affect data structure sizing or are baked into compiled bytecode.
namespace CompileTimeLimits {

constexpr std::size_t k_one_megabyte = 1024 * 1024;

// Maximum VM value-stack depth (number of Value slots).
constexpr std::size_t max_vm_stack_depth = 65536;

// Maximum number of VM call frames.
constexpr std::size_t max_call_frames = 256;

// Maximum nesting depth for display/to_string formatting of nested values.
constexpr int max_display_depth = 64;

// Maximum nesting depth for recursive structural hashing of nested values.
// 8 levels covers typical nested data while preventing O(n^depth) blowup
// on pathological inputs (e.g. deeply nested arrays of arrays).
constexpr int max_hash_depth = 8;

// Maximum nesting depth for JSON serialization/deserialization in stdlib.
constexpr int max_json_depth = 128;

// Maximum nesting depth for XML clone/serialize/search in stdlib.  Mirrors the
// XML parser's incoming-nesting cap (xml_module_parser) so programmatically
// built trees cannot exceed what the parser accepts and, crucially, cannot
// overflow the native stack during recursive clone/serialize/find.
constexpr int max_xml_depth = 128;

// Maximum recursion depth for dictionary merge operations.
constexpr int max_merge_depth = 100;

// Maximum iterations for numerical root-finding algorithms.
constexpr int max_root_iterations = 100;

// Soft threshold at which the REPL warns about accumulated program
// snapshots. Advisory only — not a hard cap.
constexpr std::size_t max_repl_program_snapshots = 500;

} // namespace CompileTimeLimits

// ─── Mutable runtime limits ───
// All limits are static inline — shared across all VM instances.
// Per-instance limits would require passing ResourceLimits by reference to each VM.
//
// Future: Replace global ResourceLimits with a per-environment RuntimeConstraints
// object passed to the VM and stdlib modules at construction time.  This would
// enable per-sandbox limits without global state mutation.
struct ResourceLimits {
    // ── Recursion and parsing ──

    // Maximum recursion depth for interpreter function calls.
    static inline int max_call_depth = 256;

    // Maximum recursion depth for the parser (nested expressions and type
    // annotations).  Recursive-descent parsing consumes one native stack frame
    // chain per nesting level, so this doubles as a stack-overflow guard: it is
    // kept well below the depth at which parsing would exhaust the smallest
    // supported stack (Windows threads default to a 1 MB reserve).  Over-nested
    // input therefore yields a "maximum nesting depth exceeded" diagnostic
    // instead of crashing.  See cmake/LumaTargetHelpers.cmake, which also
    // enlarges the reserved stack so this limit has ample head-room.
    static inline int max_parse_depth = 128;

    // Maximum AST nesting depth for expression analysis and compilation.
    // Unlike the parser, which builds left-associative operator, postfix, and
    // pipe chains *iteratively* (so max_parse_depth never trips on a flat
    // `a + b + c + ...` chain), the type checker, linter, and compiler
    // walk the resulting AST *recursively* — one native stack frame
    // per nesting level.  This limit bounds that recursion so a pathologically
    // long flat chain yields a clean "maximum expression nesting depth
    // exceeded" diagnostic instead of crashing with a native stack overflow.
    // Set well above any realistic hand-written nesting yet far below the depth
    // at which recursion would exhaust the (enlarged) thread stack.
    static inline int max_expression_depth = 1000;

    // Maximum nesting depth for string interpolation.
    static inline int max_interpolation_depth = 32;

    // Maximum byte size of a single source file.
    static inline std::size_t max_source_size = 64 * 1024 * 1024; // 64 MB

    // ── Container sizes ──

    // Runtime limit. Compile-time limit is Compiler::k_max_array_elements (16-bit operand).
    static inline std::size_t max_array_size = 10'000'000;

    // Maximum number of entries in a single dictionary.
    static inline std::size_t max_dictionary_size = 10'000'000;

    // Maximum number of elements in a single queue.
    static inline std::size_t max_queue_size = 10'000'000;

    // Maximum number of elements in a single stack.
    static inline std::size_t max_stack_size = 10'000'000;

    // Maximum number of elements in a single linked list.
    static inline std::size_t max_linked_list_size = 10'000'000;

    // Maximum number of elements in a single hash set.
    static inline std::size_t max_hash_set_size = 10'000'000;

    // Maximum number of elements in a single set.
    static inline std::size_t max_set_size = 10'000'000;

    // ── Graph limits ──

    // Maximum number of vertices in a single graph.
    // 10× smaller than general container limits to prevent O(V²) memory in adjacency matrices.
    static inline std::size_t max_graph_vertices = 1'000'000;

    // Maximum number of edges in a single graph.
    static inline std::size_t max_graph_edges = 10'000'000;

    // ── String limits ──

    // Maximum byte length of a single string value.
    static inline std::size_t max_string_size = 256 * 1024 * 1024; // 256 MB

    // Maximum repeat count for String.repeat().
    static inline std::int64_t max_string_repeat = 10'000'000;

    // Maximum width for String.pad_left() / String.pad_right().
    static inline std::size_t max_pad_width = 10'000'000;

    // Maximum codepoint length of each input to String.levenshtein_distance().
    // The algorithm is O(len_a × len_b) in time and O(len_b) in space, so this
    // bounds the work a single call can request.
    static inline std::int64_t max_levenshtein_input = 10'000;

    // ── Regex limits ──

    // Maximum byte length of a regular expression pattern.
    static inline std::size_t max_regex_pattern_size = 10'000;

    // Maximum byte length of the input string for a regex operation.
    static inline std::size_t max_regex_input_size = CompileTimeLimits::k_one_megabyte;

    // ── JSON limits ──

    // Maximum nesting depth for JSON parsing.
    static inline std::size_t max_json_nesting_depth = 128;

    // Maximum number of array elements or object keys in a single JSON document.
    static inline std::size_t max_json_elements = 100'000;

    // ── Concurrency and I/O ──

    // Maximum number of values in an unbuffered (capacity=0) channel.
    static inline std::size_t max_channel_queue_size = 1'000'000;

    // Maximum number of pending tasks in the thread pool queue.
    static inline std::size_t max_task_queue_size = 100'000;

    // Maximum number of concurrently open sockets.
    static inline int max_open_sockets = 1'000;

    // Maximum byte length of a hostname for socket operations.
    static inline std::size_t max_hostname_length = 253;

    // ── Process and environment ──

    // Maximum byte length of command output captured by Process.run().
    static inline std::size_t max_process_output_size = 64 * 1024 * 1024; // 64 MB

    // Maximum byte length of an environment variable name or value.
    static inline std::size_t max_env_size = 32 * 1024; // 32 KB

    // ── HTTP limits ──

    // Maximum byte size of HTTP response headers.
    static inline std::size_t max_http_header_size = 16 * 1024; // 16 KB

    // Maximum byte size of an HTTP response body.
    static inline std::size_t max_http_body_size = 256 * 1024 * 1024; // 256 MB

    // Maximum number of HTTP response headers retained before header parsing stops.
    static inline std::size_t max_http_header_count = 256;

    // Maximum byte size of a single HTTP response header value; longer values are
    // truncated to this length.
    static inline std::size_t max_http_header_value_size = 8 * 1024; // 8 KB

    // Maximum byte size of a raw HTTP response (headers plus body) read before the
    // reader bails out.
    static inline std::size_t max_http_response_size = 64 * 1024 * 1024; // 64 MB

    // ── Loop limits ──

    // Safety budget guarding against runaway/effectively-infinite while loops.
    // The VM keeps one running counter of loop back-edges taken: `for` loops
    // reset it on entry (so sequential loops do not accumulate), but `while`
    // loops do not, so a run of while loops accumulates toward this cap — it is
    // a cumulative budget, not a strict per-loop count. Resetting the counter at
    // while-entry would let a finite inner while repeatedly zero it and mask an
    // infinite outer `while true`, so the conservative cumulative budget is kept
    // deliberately. Set high enough that only pathological loops trip it.
    static inline std::int64_t max_while_iterations = 10'000'000;

    // Maximum nesting depth for exception handlers in the VM.
    static constexpr int k_max_exception_handler_depth = 256;

    // Load overrides from LUMA_LIMIT_* environment variables.
    static void init_from_env();
};

// ─── X-macro field table ───────────────────────────────────────────────────
//
// Lists every mutable limit shared between ResourceLimits and RuntimeConstraints.
// Format: X(type, field_name)
//
// WHY THIS EXISTS
// ───────────────
// RuntimeConstraints must mirror every field of ResourceLimits so that the VM
// can switch from global static limits to per-instance limits in a single step.
// Without the macro, every new limit field requires edits in two places; an
// omission silently leaves RuntimeConstraints with a stale default.
//
// HOW TO ADD A NEW LIMIT
// ──────────────────────
//   1. Add a documented `static inline` field to ResourceLimits above.
//   2. Add a matching X(...) entry to this macro.
//   RuntimeConstraints will gain the field automatically.
//
// Note: `k_max_exception_handler_depth` is a compile-time constant (constexpr)
// and is not included here because it cannot be changed at runtime.

// clang-format off
#define LUMA_RESOURCE_LIMITS_X(X)                             \
    /* Recursion and parsing */                               \
    X(int,           max_call_depth)                          \
    X(int,           max_parse_depth)                         \
    X(int,           max_expression_depth)                    \
    X(int,           max_interpolation_depth)                 \
    X(std::size_t,   max_source_size)                         \
    /* Container sizes */                                     \
    X(std::size_t,   max_array_size)                          \
    X(std::size_t,   max_dictionary_size)                     \
    X(std::size_t,   max_queue_size)                          \
    X(std::size_t,   max_stack_size)                          \
    X(std::size_t,   max_linked_list_size)                    \
    X(std::size_t,   max_hash_set_size)                       \
    X(std::size_t,   max_set_size)                            \
    /* Graph limits */                                        \
    X(std::size_t,   max_graph_vertices)                      \
    X(std::size_t,   max_graph_edges)                         \
    /* String limits */                                       \
    X(std::size_t,   max_string_size)                         \
    X(std::int64_t,  max_string_repeat)                       \
    X(std::size_t,   max_pad_width)                           \
    X(std::int64_t,  max_levenshtein_input)                   \
    /* Regex limits */                                        \
    X(std::size_t,   max_regex_pattern_size)                  \
    X(std::size_t,   max_regex_input_size)                    \
    /* JSON limits */                                         \
    X(std::size_t,   max_json_nesting_depth)                  \
    X(std::size_t,   max_json_elements)                       \
    /* Concurrency and I/O */                                 \
    X(std::size_t,   max_channel_queue_size)                  \
    X(std::size_t,   max_task_queue_size)                     \
    X(int,           max_open_sockets)                        \
    X(std::size_t,   max_hostname_length)                     \
    /* Process and environment */                             \
    X(std::size_t,   max_process_output_size)                 \
    X(std::size_t,   max_env_size)                            \
    /* HTTP limits */                                         \
    X(std::size_t,   max_http_header_size)                    \
    X(std::size_t,   max_http_body_size)                      \
    X(std::size_t,   max_http_header_count)                   \
    X(std::size_t,   max_http_header_value_size)              \
    X(std::size_t,   max_http_response_size)                  \
    /* Loop limits */                                         \
    X(std::int64_t,  max_while_iterations)
// clang-format on

// ─── Per-execution resource constraints ───────────────────────────────────
//
// Constructed from ResourceLimits defaults but can be customised per-VM
// to enable sandboxed execution with reduced limits.  The VM currently
// uses global ResourceLimits directly; this struct provides a migration
// path toward per-instance limits without global state mutation.
//
// RuntimeConstraints fields are generated from LUMA_RESOURCE_LIMITS_X above.
// Each field is initialised from the corresponding ResourceLimits static value.
// To add a new limit, follow the instructions in the macro comment above.

/// Per-execution resource constraints.
/// Constructed from ResourceLimits defaults but can be customized per-VM.
struct RuntimeConstraints {
#define RC_MEMBER(type, name) type name{ResourceLimits::name};
    LUMA_RESOURCE_LIMITS_X(RC_MEMBER)
#undef RC_MEMBER

    /// Creates constraints from the current global ResourceLimits.
    [[nodiscard]] static RuntimeConstraints from_defaults() {
        return RuntimeConstraints{};
    }

    /// Creates sandbox-safe constraints with lower limits.
    ///
    /// Fields intentionally NOT reduced (kept at default):
    ///   - max_parse_depth, max_expression_depth, max_interpolation_depth:
    ///     Parsing/analysis depth limits are compile-time safety rails, not
    ///     runtime resource concerns.
    ///   - max_source_size: Source code is loaded before sandbox is applied.
    ///   - max_hostname_length: Capped by DNS spec (253); already minimal.
    ///   - max_env_size: Environment reads are cheap and bounded by OS.
    ///   - max_http_header_size: Already conservative (16 KB).
    [[nodiscard]] static RuntimeConstraints sandboxed() {
        RuntimeConstraints c{};
        c.max_call_depth = 64;
        c.max_array_size = 100'000;
        c.max_dictionary_size = 100'000;
        c.max_queue_size = 100'000;
        c.max_stack_size = 100'000;
        c.max_linked_list_size = 100'000;
        c.max_hash_set_size = 100'000;
        c.max_set_size = 100'000;
        c.max_graph_vertices = 10'000;
        c.max_graph_edges = 100'000;
        c.max_string_size = 1024 * 1024; // 1 MB
        c.max_string_repeat = 100'000;
        c.max_pad_width = 100'000;
        c.max_levenshtein_input = 1'000;
        c.max_while_iterations = 1'000'000;
        c.max_process_output_size = 1024 * 1024; // 1 MB
        c.max_open_sockets = 10;
        c.max_channel_queue_size = 10'000;
        c.max_task_queue_size = 1'000;
        c.max_regex_pattern_size = 1'000; // 1 KB (vs 10 KB default)
        c.max_regex_input_size = 64'000;  // 64 KB (vs 1 MB default)
        c.max_json_nesting_depth = 32;
        c.max_json_elements = 10'000;
        c.max_http_body_size = 1024 * 1024; // 1 MB
        return c;
    }
};

} // namespace luma

#endif // LUMA_COMMON_RESOURCE_LIMITS_HPP
