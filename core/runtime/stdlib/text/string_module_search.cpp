// String module — search, match, split/join, parsing, number formatting, and
// text-layout operations: character_at/substring/matches, split/split_n/join,
// parse_integer/parse_number, format_number, is_empty, and the
// truncate/wrap/indent/dedent/template text formatters.
// Split from string_module.cpp for readability.  Registered by
// register_string_search() called from register_string_ns().

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/string_utils.hpp"
#include "common/utf8.hpp"
#include "common/utf8_iterator.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/numeric_helpers.hpp"
#include "runtime/stdlib/text/string_module.hpp"

namespace luma {

// Shared delimiter-scan used by String.split and String.split_n.  `max_parts`
// caps the number of pieces (String.split_n); std::nullopt means unlimited
// (String.split).  `function_name` names the caller for the size-limit error.
[[nodiscard]] static Value split_impl(const std::string& s, const std::string& delim,
                                      std::optional<std::int64_t> max_parts,
                                      std::string_view function_name, const SourceLocation& loc) {
    auto arr = std::make_shared<ArrayValue>();

    if (delim.empty() || (max_parts && *max_parts <= 1)) {
        arr->elements->push_back(Value{s});

        return Value{std::move(arr)};
    }

    std::size_t start{0};
    std::size_t pos{std::string::npos};
    std::int64_t parts{1};

    while ((!max_parts || parts < *max_parts) &&
           (pos = s.find(delim, start)) != std::string::npos) {
        if (arr->elements->size() >= ResourceLimits::max_array_size) {
            throw RuntimeError{
                error_msg("String", function_name, "result exceeds maximum array size"), loc};
        }

        arr->elements->push_back(Value{s.substr(start, pos - start)});

        start = pos + delim.size();

        ++parts;
    }

    arr->elements->push_back(Value{s.substr(start)});

    return Value{std::move(arr)};
}

// Shared body for String.parse_integer / parse_number.  `parse` converts the
// whole string (writing the consumed-character count through its second
// argument) and returns the parsed value, or std::nullopt when the value is not
// representable (e.g. a non-finite number).  Parsing succeeds only when the
// entire string is consumed; otherwise a failure result carrying
// "cannot parse '<s>' as <type_name>" is returned.
template <typename ParseFn>
[[nodiscard]] static Value parse_with(const std::string& s, std::string_view type_name,
                                      ParseFn parse) {
    try {
        std::size_t pos{0};

        if (auto value = parse(s, pos); value && pos == s.size()) {
            return make_success_value(std::move(*value));
        }
    } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
        // Parse failed — fall through to return failure.
    }

    return make_failure_value(std::format("cannot parse '{}' as {}", s, type_name));
}

// Throw the shared String.<fn> "result exceeds maximum string size" error.
[[noreturn]] static void throw_string_too_large(std::string_view function_name,
                                                const SourceLocation& loc, std::string_view hint) {
    throw RuntimeError{error_msg("String", function_name, "result exceeds maximum string size"),
                       loc, std::string{hint}};
}

// String.indent: prepend `prefix` to every non-empty line start.  The output
// is s.size() + line_starts * prefix.size(), which can vastly exceed both
// inputs (a string of newlines times a long prefix), so the exact size is
// computed and checked — overflow-safe — before allocating.
[[nodiscard]] static std::string indent_impl(const std::string& s, const std::string& prefix,
                                             const SourceLocation& loc) {
    // A prefix is emitted before the first byte and after every '\n' that is
    // followed by more text.
    std::size_t line_starts{s.empty() ? 0U : 1U};

    if (!s.empty()) {
        line_starts += static_cast<std::size_t>(std::count(s.begin(), std::prev(s.end()), '\n'));
    }

    const auto max = ResourceLimits::max_string_size;

    if (s.size() > max || (!prefix.empty() && line_starts > (max - s.size()) / prefix.size())) {
        throw_string_too_large("indent", loc, "use a shorter prefix or fewer lines");
    }

    std::string result{};
    result.reserve(s.size() + (line_starts * prefix.size()));

    bool at_start{true};

    for (const char ch : s) {
        if (at_start) {
            result += prefix;
            at_start = false;
        }

        result += ch;

        if (ch == '\n') {
            at_start = true;
        }
    }

    return result;
}

// Leading indentation recognised by String.dedent: spaces and tabs.
[[nodiscard]] static std::string_view leading_indent(std::string_view line) {
    const auto end = line.find_first_not_of(" \t");

    return end == std::string_view::npos ? line : line.substr(0, end);
}

// A line with no visible content (empty, or only spaces, tabs, CR, and the
// line's own '\n' terminator).
[[nodiscard]] static bool is_blank_line(std::string_view line) {
    return line.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

// String.dedent: remove the leading whitespace (spaces and tabs) common to
// every non-blank line.  Like Python's textwrap.dedent the common prefix is
// compared character-by-character, so a tab never matches a space.  Line
// terminators are preserved exactly — including a trailing newline — and
// blank lines do not constrain the common prefix (they lose as much of it as
// they share).
[[nodiscard]] static std::string dedent_impl(const std::string& s) {
    std::vector<std::string_view> lines{};
    const std::string_view text{s};

    for (std::size_t pos{0}; pos < text.size();) {
        const auto newline = text.find('\n', pos);
        const auto end = newline == std::string_view::npos ? text.size() : newline + 1;

        lines.push_back(text.substr(pos, end - pos));
        pos = end;
    }

    std::optional<std::string_view> common{};

    for (const auto line : lines) {
        if (is_blank_line(line)) {
            continue;
        }

        const auto indent = leading_indent(line);

        if (!common) {
            common = indent;
            continue;
        }

        const auto mismatch = std::ranges::mismatch(*common, indent).in1;
        common = common->substr(0, static_cast<std::size_t>(mismatch - common->begin()));
    }

    const auto strip = common ? *common : std::string_view{};

    std::string result{};
    result.reserve(s.size());

    for (const auto line : lines) {
        const auto mismatch = std::ranges::mismatch(strip, line).in1;
        result.append(line.substr(static_cast<std::size_t>(mismatch - strip.begin())));
    }

    return result;
}

// String.template: substitute `{name}` placeholders in a single left-to-right
// pass over the *original* template.  Substituted values are appended
// literally and never re-scanned, so a value containing `{other}` cannot
// trigger a further expansion (template injection) and the result does not
// depend on dictionary insertion order.  Unknown placeholders are kept
// verbatim.  The output size is bounded before every append.
//
// The scan is linear in the template length: the position of the next '}' is
// cached and reused while it still lies after the current '{' (so a run of
// many '{' before one '}' does not re-search the tail for each of them), and a
// candidate name whose length matches no dictionary key — in particular one
// longer than the longest key — is rejected without hashing it.  The names
// tried against one '}' all differ in length, so at most one per key length
// is hashed.
[[nodiscard]] static std::string template_impl(const std::string& tmpl, const DictionaryValue& dict,
                                               const SourceLocation& loc) {
    std::string result{};
    result.reserve(tmpl.size());

    std::unordered_set<std::size_t> key_lengths{};

    for (const auto& entry : dict.entries) {
        key_lengths.insert(entry.first.size());
    }

    const auto append_bounded = [&result, &loc](std::string_view piece) {
        if (piece.size() > ResourceLimits::max_string_size - result.size()) {
            throw_string_too_large("template", loc,
                                   "reduce the number or size of the template substitutions");
        }

        result.append(piece);
    };

    std::size_t pos{0};
    // Position of the first '}' after the most recent search; it stays valid
    // for every later '{' that precedes it.
    std::size_t close{0};

    while (pos < tmpl.size()) {
        const auto open = tmpl.find('{', pos);

        if (open == std::string::npos) {
            append_bounded(std::string_view{tmpl}.substr(pos));
            break;
        }

        append_bounded(std::string_view{tmpl}.substr(pos, open - pos));

        if (close <= open) {
            close = tmpl.find('}', open + 1);
        }

        if (close == std::string::npos) {
            append_bounded(std::string_view{tmpl}.substr(open));
            break;
        }

        const auto name = std::string_view{tmpl}.substr(open + 1, close - open - 1);
        const Value* value = key_lengths.contains(name.size()) ? dict.find(name) : nullptr;

        if (value == nullptr) {
            // Not a known placeholder: emit the '{' literally and resume just
            // after it, so a nested "{{name}" still resolves its inner "{name}".
            append_bounded("{");
            pos = open + 1;
            continue;
        }

        if (value->is_string()) {
            append_bounded(value->as_string());
        } else {
            append_bounded(value->to_string());
        }

        pos = close + 1;
    }

    return result;
}

void register_string_search(const EnvPtr& env) {
    ModuleBuilder{"String", env}
        .func("split", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          return split_impl(s, expect_string(args[1], "String.split", loc),
                                            std::nullopt, "split", loc);
                      })

        .func("split_n", 3)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          return split_impl(s, expect_string(args[1], "String.split_n", loc),
                                            expect_integer(args[2], "String.split_n", loc),
                                            "split_n", loc);
                      })

        // String.lines(string) -> array<string>
        // Splits on universal newlines (\n, \r\n, \r), stripping the line
        // terminators, and — unlike String.split(text, "\n") — never emits a
        // spurious trailing empty element when the text ends with a newline.
        .func("lines", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation loc) -> Value {
                          auto arr = std::make_shared<ArrayValue>();

                          const std::size_t n = s.size();
                          std::size_t start{0};
                          std::size_t i{0};

                          const auto push_line = [&](std::size_t from, std::size_t to) {
                              if (arr->elements->size() >= ResourceLimits::max_array_size) {
                                  throw RuntimeError{error_msg("String", "lines",
                                                               "result exceeds maximum array size"),
                                                     loc};
                              }
                              arr->elements->push_back(Value{s.substr(from, to - from)});
                          };

                          while (i < n) {
                              const char c = s[i];
                              if (c == '\n' || c == '\r') {
                                  push_line(start, i);
                                  // Treat "\r\n" as a single terminator.
                                  if (c == '\r' && i + 1 < n && s[i + 1] == '\n') {
                                      i += 2;
                                  } else {
                                      i += 1;
                                  }
                                  start = i;
                              } else {
                                  ++i;
                              }
                          }

                          // Emit the final segment only when it is non-empty, so
                          // text ending in a newline yields no trailing empty line.
                          if (start < n) {
                              push_line(start, n);
                          }

                          return Value{std::move(arr)};
                      })

        .func("join", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems_arr = expect_array(args[0], "String.join", loc);
            const auto& sep = expect_string(args[1], "String.join", loc);

            const auto& elems = *elems_arr->elements;

            std::vector<std::string> converted;
            converted.reserve(elems.size());

            bool first{true};
            std::size_t converted_count{0};
            std::size_t total_size{0};

            const auto add_size = [&total_size, loc](const std::size_t size) {
                if (size > ResourceLimits::max_string_size - total_size) {
                    throw RuntimeError{
                        error_msg("String", "join", "result exceeds maximum string size"), loc,
                        "reduce the number of elements or their size"};
                }
                total_size += size;
            };

            for (const auto& elem : elems) {
                if (!first) {
                    add_size(sep.size());
                }

                first = false;

                if (elem.is_string()) {
                    add_size(elem.as_string().size());
                } else {
                    converted.push_back(elem.to_string());
                    add_size(converted.back().size());
                    ++converted_count;
                }
            }

            std::string result{};
            result.reserve(total_size);

            converted_count = 0;
            first = true;
            for (const auto& elem : elems) {
                if (!first) {
                    result.append(sep);
                }

                first = false;

                if (elem.is_string()) {
                    result.append(elem.as_string());
                } else {
                    result.append(converted[converted_count]);
                    ++converted_count;
                }
            }

            return Value{std::move(result)};
        })

        .func("character_at", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto i = expect_integer(args[1], "String.character_at", loc);
                          const auto cp_count = utf8_count(s);

                          if (i < 0 || i >= cp_count) {
                              return make_failure_value(ErrorMessages::index_out_of_bounds(
                                  i, static_cast<std::size_t>(cp_count)));
                          }

                          const auto byte_pos = utf8_byte_offset(s, i);

                          return make_success_value(Value{utf8_char_at_byte(s, byte_pos)});
                      })

        .func("substring", 3)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto raw_start = expect_integer(args[1], "String.substring", loc);
                          const auto raw_end = expect_integer(args[2], "String.substring", loc);
                          const auto cp_count = utf8_count(s);
                          const auto start = std::max(std::int64_t{0}, raw_start);
                          const auto end = std::max(std::int64_t{0}, std::min(cp_count, raw_end));

                          if (start >= end) {
                              return Value{std::string{}};
                          }

                          const auto byte_start = std::min(utf8_byte_offset(s, start), s.size());
                          const auto byte_end = std::min(utf8_byte_offset(s, end), s.size());

                          return Value{s.substr(byte_start, byte_end - byte_start)};
                      })

        .func("format_number", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_numeric(args[0], "String.format_number", loc);
            const auto decimals = expect_integer(args[1], "String.format_number", loc);

            if (decimals < 0 || decimals > 100) {
                throw RuntimeError{
                    error_msg("String", "format_number", "precision must be between 0 and 100"),
                    loc};
            }

            return Value{std::format("{:.{}f}", n, static_cast<int>(decimals))};
        })

        .func("truncate", 2)
        .extract_body(
            expect_string,
            [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                // Clamp negative max_length to 0, consistent with String.substring's
                // index clamping.  Both functions treat out-of-range values as
                // boundary values rather than errors — friendlier for beginners and
                // standard in most string libraries.
                const auto max_len = static_cast<std::size_t>(
                    std::max(std::int64_t{0}, expect_integer(args[1], "String.truncate", loc)));
                const auto cp_count = utf8_count_size(s);

                if (cp_count <= max_len) {
                    return Value{s};
                }

                if (max_len <= 3) {
                    const auto byte_end = utf8_byte_offset(s, static_cast<std::int64_t>(max_len));

                    return Value{s.substr(0, byte_end)};
                }

                const auto byte_end = utf8_byte_offset(s, static_cast<std::int64_t>(max_len - 3));

                return Value{s.substr(0, byte_end) + "..."};
            })

        .func("is_empty", 1)
        .extract_body(
            expect_string,
            [](const auto& s, const Args&, SourceLocation) -> Value { return Value{s.empty()}; })

        .func("parse_integer", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return parse_with(
                              s, "integer",
                              [](const std::string& str, std::size_t& pos) -> std::optional<Value> {
                                  return Value{static_cast<std::int64_t>(std::stoll(str, &pos))};
                              });
                      })

        .func("parse_number", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return parse_with(
                              s, "number",
                              [](const std::string& str, std::size_t& pos) -> std::optional<Value> {
                                  const auto val = std::stod(str, &pos);

                                  if (!stdlib::is_valid_numeric(val)) {
                                      return std::nullopt;
                                  }

                                  return Value{val};
                              });
                      })

        .func("wrap", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto width =
                              static_cast<std::size_t>(expect_integer(args[1], "String.wrap", loc));

                          std::string result{};
                          std::size_t col{0};
                          std::size_t i{0};

                          // Track the last space so we can break there when the
                          // line exceeds width (standard greedy word-wrap).
                          std::size_t last_space_byte{std::string::npos};
                          std::size_t last_space_col{0};

                          while (i < s.size()) {
                              if (s[i] == '\n') {
                                  result += '\n';
                                  col = 0;
                                  last_space_byte = std::string::npos;
                                  ++i;
                              } else if (s[i] == ' ') {
                                  last_space_byte = result.size();
                                  last_space_col = col;
                                  result += ' ';
                                  ++col;
                                  ++i;
                              } else {
                                  if (col >= width && last_space_byte != std::string::npos) {
                                      result[last_space_byte] = '\n';
                                      col -= (last_space_col + 1);
                                      last_space_byte = std::string::npos;
                                  }

                                  const auto cplen = utf8_advance(s, i);

                                  result += s.substr(i, cplen);
                                  ++col;
                                  i += cplen;
                              }
                          }

                          return Value{std::move(result)};
                      })

        .func("indent", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          return Value{
                              indent_impl(s, expect_string(args[1], "String.indent", loc), loc)};
                      })

        .func("dedent", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{dedent_impl(s)};
                      })

        .func("matches", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto& pattern = expect_string(args[1], "String.matches", loc);

                          // Linear, backtracking-free glob match — safe to run on
                          // untrusted patterns/subjects (no ReDoS).  Every string is a
                          // valid glob, so this never fails; the result<boolean> wrapper
                          // is kept for API symmetry with RegularExpression.matches.
                          return make_success_value(Value{glob_match(pattern, s)});
                      })

        .func("template", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& tmpl = expect_string(args[0], "String.template", loc);
            const auto& dict = expect_dict(args[1], "String.template", loc);

            return Value{template_impl(tmpl, *dict, loc)};
        });
}

} // namespace luma
