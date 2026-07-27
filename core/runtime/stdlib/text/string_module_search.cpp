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
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

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

void register_string_search(const EnvPtr& env) {
    ModuleBuilder{"String", env}
        .func("split", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          return split_impl(s, args[1].as_string(), std::nullopt, "split", loc);
                      })

        .func("split_n", 3)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          return split_impl(s, args[1].as_string(), args[2].as_integer(), "split_n",
                                            loc);
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

            (void)expect_string(args[1], "String.join", loc);

            const auto& elems = *elems_arr->elements;
            const auto& sep = args[1].as_string();

            std::string result{};
            result.reserve(elems.size() * 8);

            bool first{true};

            for (const auto& elem : elems) {
                if (!first) {
                    result += sep;
                }

                first = false;

                if (elem.is_string()) {
                    result += elem.as_string();
                } else {
                    result += elem.to_string();
                }

                if (result.size() > ResourceLimits::max_string_size) {
                    throw RuntimeError{
                        error_msg("String", "join", "result exceeds maximum string size"), loc,
                        "reduce the number of elements or their size"};
                }
            }

            return Value{std::move(result)};
        })

        .func("character_at", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation) -> Value {
                          const auto i = args[1].as_integer();
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
                      [](const auto& s, const Args& args, SourceLocation) -> Value {
                          const auto cp_count = utf8_count(s);
                          const auto start = std::max(std::int64_t{0}, args[1].as_integer());
                          const auto end =
                              std::max(std::int64_t{0}, std::min(cp_count, args[2].as_integer()));

                          if (start >= end) {
                              return Value{std::string{}};
                          }

                          const auto byte_start = utf8_byte_offset(s, start);
                          const auto byte_end = utf8_byte_offset(s, end);

                          return Value{s.substr(byte_start, byte_end - byte_start)};
                      })

        .func("format_number", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = args[0].to_numeric();
            const auto decimals = args[1].as_integer();

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
            [](const auto& s, const Args& args, SourceLocation) -> Value {
                // Clamp negative max_length to 0, consistent with String.substring's
                // index clamping.  Both functions treat out-of-range values as
                // boundary values rather than errors — friendlier for beginners and
                // standard in most string libraries.
                const auto max_len =
                    static_cast<std::size_t>(std::max(std::int64_t{0}, args[1].as_integer()));
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
                      [](const auto& s, const Args& args, SourceLocation) -> Value {
                          const auto width = static_cast<std::size_t>(args[1].as_integer());

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
                      [](const auto& s, const Args& args, SourceLocation) -> Value {
                          const auto& prefix = args[1].as_string();

                          std::string result{};

                          bool at_start{true};

                          for (auto ch : s) {
                              if (at_start) {
                                  result += prefix;

                                  at_start = false;
                              }

                              result += ch;

                              if (ch == '\n') {
                                  at_start = true;
                              }
                          }

                          return Value{std::move(result)};
                      })

        .func("dedent", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          std::vector<std::string> lines{};
                          std::istringstream iss{s};
                          std::string line{};

                          while (std::getline(iss, line)) {
                              lines.push_back(line);
                          }

                          std::size_t min_indent{std::numeric_limits<std::size_t>::max()};

                          for (const auto& l : lines) {
                              if (l.empty()) {
                                  continue;
                              }

                              std::size_t indent{0};

                              while (indent < l.size() && l[indent] == ' ') {
                                  ++indent;
                              }

                              if (indent < l.size()) {
                                  min_indent = std::min(min_indent, indent);
                              }
                          }

                          if (min_indent == std::numeric_limits<std::size_t>::max()) {
                              min_indent = 0;
                          }

                          std::string result{};

                          bool first{true};

                          for (const auto& l : lines) {
                              if (!first) {
                                  result += '\n';
                              }

                              first = false;

                              if (l.size() > min_indent) {
                                  result += l.substr(min_indent);
                              }
                          }

                          return Value{std::move(result)};
                      })

        .func("matches", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation) -> Value {
                          const auto& pattern = args[1].as_string();

                          // Linear, backtracking-free glob match — safe to run on
                          // untrusted patterns/subjects (no ReDoS).  Every string is a
                          // valid glob, so this never fails; the result<boolean> wrapper
                          // is kept for API symmetry with RegularExpression.matches.
                          return make_success_value(Value{glob_match(pattern, s)});
                      })

        .func("template", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto s = args[0].as_string();

            (void)expect_dict(args[1], "String.template", loc);

            const auto& dict = args[1].as_dictionary();

            for (const auto& [key, val] : dict->entries) {
                const std::string placeholder{"{" + key + "}"};

                std::size_t pos = s.find(placeholder);
                if (pos == std::string::npos) {
                    continue;
                }

                // The replacement is constant across this key's occurrences, so
                // compute it once and rewrite the string in a single forward
                // pass into a fresh buffer. An in-place std::string::replace per
                // occurrence shifts the whole tail every time (quadratic); the
                // appending builder keeps each key's pass linear in the length.
                const auto replacement = val.to_string();

                std::string result;
                result.reserve(s.size());
                std::size_t last{0};

                // Track the length the in-place rewrite would have reached so
                // the resource-limit guard fires at exactly the same point, with
                // the same overflow-safe arithmetic (projected >= placeholder
                // size holds because find() located a placeholder within it).
                std::size_t projected{s.size()};

                while (pos != std::string::npos) {
                    // Bound the result *before* growing it, using overflow-safe
                    // arithmetic. A post-replace check would still permit a large
                    // transient allocation — and a std::bad_alloc rather than a
                    // clean RuntimeError — under a self-expanding "billion laughs"
                    // input.
                    if (replacement.size() > ResourceLimits::max_string_size ||
                        projected - placeholder.size() >
                            ResourceLimits::max_string_size - replacement.size()) {
                        throw RuntimeError{
                            error_msg("String", "template", "result exceeds maximum string size"),
                            loc, "reduce the number or size of the template substitutions"};
                    }

                    result.append(s, last, pos - last);
                    result.append(replacement);
                    projected = projected - placeholder.size() + replacement.size();

                    last = pos + placeholder.size();
                    pos = s.find(placeholder, last);
                }

                result.append(s, last, s.size() - last);
                s = std::move(result);
            }

            return Value{std::move(s)};
        });
}

} // namespace luma
