// json_module_path.cpp — dot/bracket path navigation for the Json module.
//
// Extracted from json_module_parser.cpp so the parser translation unit owns
// only the JSON grammar.  Shared by the Json.get/set and Json.get_path/set_path
// native bodies in json_module.cpp.

#include "runtime/stdlib/text/json_module_path.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/index_validator.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma::json_path {

namespace {

// Parse a decimal index from `key`, reproducing the prefix-parse semantics of
// the std::stoll calls this replaces: skip leading ASCII whitespace, accept an
// optional leading sign, consume the digit run, and ignore trailing characters.
// std::from_chars skips neither whitespace nor a leading '+', so both are peeled
// off explicitly (a leading '-' is left for from_chars).  Returns std::nullopt
// when no digits are present or the value overflows std::int64_t.
[[nodiscard]] std::optional<std::int64_t> try_parse_index(std::string_view key) {
    constexpr std::string_view whitespace = " \t\n\v\f\r";
    const auto start = key.find_first_not_of(whitespace);

    if (start == std::string_view::npos) {
        return std::nullopt;
    }

    key.remove_prefix(start);

    if (key.front() == '+') {
        key.remove_prefix(1);
    }

    std::int64_t value{0};
    const auto* const first = key.data();
    const auto* const last = first + key.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);

    if (ec != std::errc{} || ptr == first) {
        return std::nullopt;
    }

    return value;
}

[[nodiscard]] std::string array_key_error(ArrayKeyError style, const std::string& key) {
    return style == ArrayKeyError::invalid_array_index
               ? std::format("invalid array index '{}'", key)
               : std::format("cannot use key '{}' on an array", key);
}

} // namespace

std::vector<PathSegment> parse_path_segments(std::string_view path) {
    std::vector<PathSegment> segments;
    std::string current;

    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '.') {
            if (!current.empty()) {
                segments.push_back({.key = current, .index = -1, .is_index = false});
                current.clear();
            }
        } else if (path[i] == '[') {
            if (!current.empty()) {
                segments.push_back({.key = current, .index = -1, .is_index = false});
                current.clear();
            }

            ++i;
            std::string idx_str;

            while (i < path.size() && path[i] != ']') {
                idx_str += path[i];
                ++i;
            }

            // i now points to ']'.
            if (const auto idx = try_parse_index(idx_str)) {
                segments.push_back({.key = "", .index = *idx, .is_index = true});
            } else {
                segments.push_back({.key = idx_str, .index = -1, .is_index = false});
            }
        } else {
            current += path[i];
        }
    }

    if (!current.empty()) {
        segments.push_back({.key = current, .index = -1, .is_index = false});
    }

    return segments;
}

ArrayIndexResult resolve_array_index(std::string_view key, const std::vector<Value>& elems,
                                     ArrayKeyError style) {
    const auto idx = try_parse_index(key);

    if (!idx) {
        return {.index = 0, .ok = false, .error = array_key_error(style, std::string{key})};
    }

    if (is_index_out_of_bounds(*idx, elems.size())) {
        return {.index = 0, .ok = false, .error = std::format("index {} out of bounds", *idx)};
    }

    return {.index = static_cast<std::size_t>(*idx), .ok = true, .error = {}};
}

NavigateResult navigate_path(Value& root, const std::vector<PathSegment>& segments,
                             std::size_t count, ArrayKeyError key_error, NavigateArrays arrays) {
    Value* current = &root;

    for (std::size_t i{0}; i < count; ++i) {
        const auto& seg = segments[i];

        if (seg.is_index) {
            if (!current->is_array()) {
                return {.value = nullptr,
                        .error = std::format("expected array at index segment [{}]", seg.index)};
            }

            auto& elems = *current->as_array()->elements;

            if (is_index_out_of_bounds(seg.index, elems.size())) {
                return {.value = nullptr,
                        .error = std::format("index {} out of bounds", seg.index)};
            }

            current = &elems[static_cast<std::size_t>(seg.index)];
        } else {
            if (current->is_dictionary()) {
                auto* found = current->as_dictionary()->find(seg.key);

                if (found == nullptr) {
                    return {.value = nullptr, .error = std::format("key '{}' not found", seg.key)};
                }

                current = found;
            } else if (arrays == NavigateArrays::allow && current->is_array()) {
                auto& elems = *current->as_array()->elements;
                auto resolved = resolve_array_index(seg.key, elems, key_error);

                if (!resolved.ok) {
                    return {.value = nullptr, .error = std::move(resolved.error)};
                }

                current = &elems[resolved.index];
            } else {
                return {.value = nullptr,
                        .error = std::format("cannot traverse into '{}' at '{}'",
                                             current->display_type_name(), seg.key)};
            }
        }
    }

    return {.value = current, .error = ""};
}

std::vector<PathSegment> split_dot_path(std::string_view path) {
    std::vector<PathSegment> segments;
    std::string current;

    for (const auto c : path) {
        if (c == '.') {
            if (!current.empty()) {
                segments.push_back({.key = std::move(current), .index = -1, .is_index = false});
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        segments.push_back({.key = std::move(current), .index = -1, .is_index = false});
    }

    return segments;
}

} // namespace luma::json_path
