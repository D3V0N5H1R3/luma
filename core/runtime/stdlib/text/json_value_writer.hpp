#ifndef LUMA_RUNTIME_STDLIB_JSON_VALUE_WRITER_HPP
#define LUMA_RUNTIME_STDLIB_JSON_VALUE_WRITER_HPP

// ═══════════════════════════════════════════════════════════════════════════
// Shared Value → JSON walker (policy-parameterised)
// ═══════════════════════════════════════════════════════════════════════════
//
// The Json module's serialiser walks a Value and emits JSON, parameterised by a
// Policy so the per-kind traversal is captured once while escaping and limit
// behaviour stay pluggable:
//
//   * Json module's Json.serialize (json_module_serializer.cpp)
//       — no slash-escaping, appends "null" past the depth limit, supports
//         pretty-printing, and encodes only nullary choices (as a bare string;
//         choices with fields become null).
//
// This template captures the single traversal; the caller supplies a Policy
// that carries its escaping and limit behaviour, mirroring the
// escape_string_impl<Policy> precedent for "same algorithm, different
// escaping/limits".  The observable output is preserved exactly.
//
// Policy contract:
//   static void escape(std::string_view s, std::string& out);
//       Append s to out, escaped for a JSON string body (quotes not included).
//   static bool depth_exceeded(int depth);
//       True when depth is past the caller's nesting limit.
//   static void on_depth_exceeded(std::string& out);
//       React to an over-deep value: either throw, or append a token (e.g.
//       "null") and return.  MUST NOT be marked [[noreturn]] even when it
//       always throws — the shared walker's trailing `return` would then be
//       flagged unreachable (MSVC C4702).
//   static constexpr JsonChoiceMode choice_mode;
//       rich           → {"variant":…,"fields":[…]} for every arity;
//       nullary_string → nullary variant as a bare string, any fields → null.
//   static constexpr bool result_increments_depth;
//       Whether unwrapping result<T> counts as one extra nesting level.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "runtime/interpreter/value.hpp"

namespace luma::json_writer {

// How a Policy encodes choice (ADT) values.
enum class JsonChoiceMode {
    rich,           // {"variant":"X","fields":[...]} for every arity.
    nullary_string, // nullary variant → "X"; any fields → null.
};

// Growth hints only — never affect the emitted bytes.
inline constexpr std::size_t k_array_reserve_per_element = 16;
inline constexpr std::size_t k_object_reserve_per_entry = 24;

template <typename Policy>
void write_value(const Value& val, std::string& out, int indent, int depth, bool pretty);

// Append a newline plus (indent * depth) spaces when pretty-printing.
inline void write_indent(std::string& out, int indent, int depth) {
    if (indent <= 0) {
        return;
    }

    const auto spaces = static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth);

    // Reserve for newline + indentation spaces.
    out.reserve(out.size() + 1 + spaces);
    out += '\n';
    out.append(spaces, ' ');
}

template <typename Policy>
void write_array(std::span<const Value> elems, std::string& out, int indent, int depth,
                 bool pretty) {
    out.reserve(out.size() + 2 + (elems.size() * k_array_reserve_per_element));
    out += '[';

    for (std::size_t i{0}; i < elems.size(); ++i) {
        if (i > 0) {
            out += ',';
        }

        if (pretty) {
            write_indent(out, indent, depth + 1);
        }

        write_value<Policy>(elems[i], out, indent, depth + 1, pretty);
    }

    if (!elems.empty() && pretty) {
        write_indent(out, indent, depth);
    }

    out += ']';
}

template <typename Policy>
void write_object(const std::vector<std::pair<std::string, Value>>& entries, std::string& out,
                  int indent, int depth, bool pretty) {
    out.reserve(out.size() + 2 + (entries.size() * k_object_reserve_per_entry));
    out += '{';

    for (std::size_t i{0}; i < entries.size(); ++i) {
        if (i > 0) {
            out += ',';
        }

        if (pretty) {
            write_indent(out, indent, depth + 1);
        }

        out += '"';
        Policy::escape(entries[i].first, out);
        out += "\":";

        if (pretty) {
            out += ' ';
        }

        write_value<Policy>(entries[i].second, out, indent, depth + 1, pretty);
    }

    if (!entries.empty() && pretty) {
        write_indent(out, indent, depth);
    }

    out += '}';
}

// Per-value-kind dispatcher.  Each composite kind (array, dictionary, tuple,
// choice, record) owns its own encoding so no single branch carries every
// container's rules.
template <typename Policy>
void write_value(const Value& val, std::string& out, int indent, int depth, bool pretty) {
    if (Policy::depth_exceeded(depth)) {
        Policy::on_depth_exceeded(out);
        return;
    }

    if (val.is_null()) {
        out += "null";
        return;
    }

    // Transparently unwrap result<T> so success values serialise as their inner
    // value.  A failure — or a success with no inner value — becomes null.
    if (val.is_result()) {
        const auto& res = val.as_result();

        if (res->is_success && res->owned_inner) {
            const int inner_depth = Policy::result_increments_depth ? depth + 1 : depth;
            write_value<Policy>(*res->owned_inner, out, indent, inner_depth, pretty);
            return;
        }

        out += "null";
        return;
    }

    if (val.is_bool()) {
        out += val.as_bool() ? "true" : "false";
        return;
    }

    if (val.is_integer()) {
        // Write directly into a small stack buffer to avoid a heap allocation.
        std::array<char, 24> buf;
        auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), val.as_integer());
        out.append(buf.data(), ptr);
        return;
    }

    if (val.is_number()) {
        const double n = val.as_number();

        // JSON has no NaN/Infinity; non-finite doubles coerce to null (matching
        // JSON.stringify).
        if (!std::isfinite(n)) {
            out += "null";
        } else {
            std::format_to(std::back_inserter(out), "{}", n);
        }

        return;
    }

    if (val.is_string()) {
        out += '"';
        Policy::escape(val.as_string(), out);
        out += '"';
        return;
    }

    if (val.is_array()) {
        write_array<Policy>(*val.as_array()->elements, out, indent, depth, pretty);
        return;
    }

    if (val.is_dictionary()) {
        write_object<Policy>(val.as_dictionary()->entries, out, indent, depth, pretty);
        return;
    }

    if (val.is_tuple()) {
        // Tuples serialise as JSON arrays.
        write_array<Policy>(val.as_tuple()->elements, out, indent, depth, pretty);
        return;
    }

    if (val.is_choice()) {
        const auto& choice = *val.as_choice();

        if constexpr (Policy::choice_mode == JsonChoiceMode::rich) {
            out += R"({"variant":")";
            Policy::escape(choice.variant, out);
            out += R"(","fields":[)";

            for (std::size_t i{0}; i < choice.fields.size(); ++i) {
                if (i > 0) {
                    out += ',';
                }

                write_value<Policy>(choice.fields[i], out, indent, depth + 1, pretty);
            }

            out += "]}";
        } else if (choice.fields.empty()) {
            out += '"';
            Policy::escape(choice.variant, out);
            out += '"';
        } else {
            out += "null";
        }

        return;
    }

    if (val.is_record()) {
        // Records serialise as JSON objects.
        write_object<Policy>(val.as_record()->fields, out, indent, depth, pretty);
        return;
    }

    // Functions, channels, tasks, sockets — any other non-representable kind.
    out += "null";
}

} // namespace luma::json_writer

#endif // LUMA_RUNTIME_STDLIB_JSON_VALUE_WRITER_HPP
