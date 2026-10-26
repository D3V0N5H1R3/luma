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
// LUMA_LIMIT_, e.g. LUMA_LIMIT_MAX_CALL_DEPTH=512.  Each override must lie in
// [1, ceiling] (see LUMA_RESOURCE_LIMITS_X): a non-numeric or zero value is
// ignored and a value above the ceiling is clamped to it, both with a warning.
// Call ResourceLimits::init_from_env() at startup to apply overrides.
//
// Thread safety: The static members are read-only after init_from_env()
// is called once at startup. Concurrent reads are safe; concurrent
// writes (calling init_from_env() from multiple threads) are NOT safe.

// ─── Compile-time limits ───
// Fixed constants known at compile time. These cannot be overridden at runtime
// because they affect data structure sizing or are baked into compiled bytecode.
namespace CompileTimeLimits {

constexpr std::size_t k_one_megabyte = std::size_t{1024} * 1024;

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
    static inline std::size_t max_source_size = std::size_t{64} * 1024 * 1024; // 64 MB

    // ── Container sizes ──

    // Runtime limit. Compile-time limit is Compiler::k_max_array_elements (16-bit operand).
    static inline std::size_t max_array_size = 10'000'000;

    // Maximum number of entries in a single dictionary.
    static inline std::size_t max_dictionary_size = 10'000'000;

    // Maximum number of elements in a single queue.
    static inline std::size_t max_queue_size = 10'000'000;

    // Maximum number of elements in a single stack.
    static inline std::size_t max_stack_size = 10'000'000;

    // Maximum number of elements in a single set.
    static inline std::size_t max_set_size = 10'000'000;

    // ── String limits ──

    // Maximum byte length of a single string value.
    static inline std::size_t max_string_size = std::size_t{256} * 1024 * 1024; // 256 MB

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
    static inline std::size_t max_process_output_size = std::size_t{64} * 1024 * 1024; // 64 MB

    // Maximum byte length of an environment variable name or value.
    static inline std::size_t max_env_size = std::size_t{32} * 1024; // 32 KB

    // ── HTTP limits ──

    // Maximum byte size of HTTP response headers.
    static inline std::size_t max_http_header_size = std::size_t{16} * 1024; // 16 KB

    // Maximum byte size of an HTTP response body.
    static inline std::size_t max_http_body_size = std::size_t{256} * 1024 * 1024; // 256 MB

    // Maximum number of HTTP response headers retained before header parsing stops.
    static inline std::size_t max_http_header_count = 256;

    // Maximum byte size of a single HTTP response header value; longer values are
    // truncated to this length.
    static inline std::size_t max_http_header_value_size = std::size_t{8} * 1024; // 8 KB

    // Maximum byte size of a raw HTTP response (headers plus body) read before the
    // reader bails out.
    static inline std::size_t max_http_response_size = std::size_t{64} * 1024 * 1024; // 64 MB

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
// Lists every mutable limit together with the largest value an environment
// override may set.  Format: X(type, field_name, ceiling)
//
// init_from_env() expands this table to read LUMA_LIMIT_<FIELD_NAME> for every
// field, so a new limit only needs a field above plus an entry here.  Every
// override must lie in [1, ceiling]: 0 would disable the feature it guards
// (LUMA_LIMIT_MAX_PARSE_DEPTH=0 rejects every program), and the ceilings keep
// the stack-sensitive recursion limits (parse, expression, interpolation,
// JSON nesting, call depth) safely below the depth at which recursion would
// exhaust the native stack — the guard these limits exist to provide.  The
// size ceilings are sanity bounds well above any realistic program.
//
// HOW TO ADD A NEW LIMIT
// ──────────────────────
//   1. Add a documented `static inline` field to ResourceLimits above.
//   2. Add a matching X(...) entry, with a ceiling, to this macro.
//
// Note: `k_max_exception_handler_depth` is a compile-time constant (constexpr)
// and is not included here because it cannot be changed at runtime.

// clang-format off
#define LUMA_RESOURCE_LIMITS_X(X)                                             \
    /* Recursion and parsing (stack-sensitive) */                             \
    X(int,           max_call_depth,             4'096)                       \
    X(int,           max_parse_depth,            512)                         \
    X(int,           max_expression_depth,       4'000)                       \
    X(int,           max_interpolation_depth,    256)                         \
    X(std::size_t,   max_source_size,            1ULL << 30)  /* 1 GiB */    \
    /* Container sizes */                                                     \
    X(std::size_t,   max_array_size,             1'000'000'000)               \
    X(std::size_t,   max_dictionary_size,        1'000'000'000)               \
    X(std::size_t,   max_queue_size,             1'000'000'000)               \
    X(std::size_t,   max_stack_size,             1'000'000'000)               \
    X(std::size_t,   max_set_size,               1'000'000'000)               \
    /* String limits */                                                       \
    X(std::size_t,   max_string_size,            1ULL << 32)  /* 4 GiB */    \
    X(std::int64_t,  max_string_repeat,          1'000'000'000)               \
    X(std::size_t,   max_pad_width,              1'000'000'000)               \
    X(std::int64_t,  max_levenshtein_input,      100'000)                     \
    /* Regex limits */                                                        \
    X(std::size_t,   max_regex_pattern_size,     1'000'000)                   \
    X(std::size_t,   max_regex_input_size,       1ULL << 30)  /* 1 GiB */    \
    /* JSON limits */                                                         \
    X(std::size_t,   max_json_nesting_depth,     512)                         \
    X(std::size_t,   max_json_elements,          1'000'000'000)               \
    /* Concurrency and I/O */                                                 \
    X(std::size_t,   max_channel_queue_size,     1'000'000'000)               \
    X(std::size_t,   max_task_queue_size,        100'000'000)                 \
    X(int,           max_open_sockets,           65'536)                      \
    X(std::size_t,   max_hostname_length,        1'024)                       \
    /* Process and environment */                                             \
    X(std::size_t,   max_process_output_size,    1ULL << 32)  /* 4 GiB */    \
    X(std::size_t,   max_env_size,               1ULL << 20)  /* 1 MiB */    \
    /* HTTP limits */                                                         \
    X(std::size_t,   max_http_header_size,       1ULL << 24)  /* 16 MiB */   \
    X(std::size_t,   max_http_body_size,         1ULL << 32)  /* 4 GiB */    \
    X(std::size_t,   max_http_header_count,      100'000)                     \
    X(std::size_t,   max_http_header_value_size, 1ULL << 24)  /* 16 MiB */   \
    X(std::size_t,   max_http_response_size,     1ULL << 32)  /* 4 GiB */    \
    /* Loop limits */                                                         \
    X(std::int64_t,  max_while_iterations,       1'000'000'000'000)
// clang-format on

} // namespace luma

#endif // LUMA_COMMON_RESOURCE_LIMITS_HPP
