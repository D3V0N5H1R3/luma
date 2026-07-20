#ifndef LUMA_RUNTIME_STDLIB_JSON_MODULE_PATH_HPP
#define LUMA_RUNTIME_STDLIB_JSON_MODULE_PATH_HPP

// ═══════════════════════════════════════════════════════════════════════════
// Json module path navigation (dot- and bracket-notation)
// ═══════════════════════════════════════════════════════════════════════════
//
// Shared by the Json.get/set (dot notation) and Json.get_path/set_path
// (bracket notation) native bodies in json_module.cpp.  Extracted from
// json_module_parser.cpp so the parser translation unit owns only the
// JSON-text → Value grammar, mirroring the serializer/json_value_writer split.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace luma {

class Value;

namespace json_path {

// A single step of a parsed path: either a dictionary key or an array index.
struct PathSegment {
    std::string key;
    std::int64_t index{-1};
    bool is_index{false};
};

// Result of walking a path: the reached slot, or a non-empty error string.
struct NavigateResult {
    Value* value{nullptr};
    std::string error;
};

// Error style for a non-numeric path segment applied to an array.  Dot-notation
// (Json.get) reports "invalid array index"; the bracket API (Json.get_path /
// Json.set_path) reports "cannot use key ... on an array".
enum class ArrayKeyError {
    cannot_use_key,
    invalid_array_index
};

// Whether navigate_path may descend into arrays when a key segment lands on one.
// Json.set_path allows it (its bracket paths address arrays); Json.set rejects
// it to keep its dictionary-only traversal.
enum class NavigateArrays {
    allow,
    reject
};

// Resolved array subscript, or the error string the caller should surface.
struct ArrayIndexResult {
    std::size_t index{0};
    bool ok{false};
    std::string error;
};

// Parse a bracket/dot path (e.g. `a.b[0].c`) into segments, resolving numeric
// bracket contents to index segments.
[[nodiscard]] std::vector<PathSegment> parse_path_segments(std::string_view path);

// Split a dot-notation path into key segments only; unlike parse_path_segments
// this performs no bracket handling, so `a[0]` stays a single literal key.
[[nodiscard]] std::vector<PathSegment> split_dot_path(std::string_view path);

// Resolve a string path segment to an in-bounds array index, centralising the
// "index vs key" error policy: a non-numeric key yields array_key_error(style,
// key); an out-of-range numeric index yields "index N out of bounds".
[[nodiscard]] ArrayIndexResult
resolve_array_index(std::string_view key, const std::vector<Value>& elems, ArrayKeyError style);

// Walk `count` segments of `segments` from `root`, returning the reached slot or
// an error.  `key_error` selects the array-key error wording and `arrays`
// controls whether a key segment may descend into an array.
[[nodiscard]] NavigateResult navigate_path(Value& root, const std::vector<PathSegment>& segments,
                                           std::size_t count,
                                           ArrayKeyError key_error = ArrayKeyError::cannot_use_key,
                                           NavigateArrays arrays = NavigateArrays::allow);

} // namespace json_path

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_JSON_MODULE_PATH_HPP
