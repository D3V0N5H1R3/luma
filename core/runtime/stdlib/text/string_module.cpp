#include "runtime/stdlib/text/string_module.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/utf8.hpp"
#include "common/utf8_iterator.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

constexpr std::string_view k_whitespace = " \t\n\r";

// Apply an ASCII-only character transformation, preserving multi-byte UTF-8.
template <typename CharFn> [[nodiscard]] std::string transform_ascii(std::string s, CharFn fn) {
    std::ranges::transform(s, s.begin(), [&fn](unsigned char c) {
        return (c < 0x80) ? static_cast<char>(fn(c)) : static_cast<char>(c);
    });
    return s;
}

// ASCII-only lowercase of a single byte; bytes >= 0x80 are left untouched so
// multi-byte UTF-8 sequences compare literally (matching String.uppercase /
// String.lowercase, which only fold the ASCII range).
[[nodiscard]] inline char ascii_lower(char c) {
    const auto uc = static_cast<unsigned char>(c);
    return uc < 0x80 ? static_cast<char>(std::tolower(uc)) : c;
}

// ASCII case-insensitive string equality.
[[nodiscard]] bool ascii_iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
               return ascii_lower(x) == ascii_lower(y);
           });
}

// ASCII case-insensitive substring test.  An empty needle always matches.
[[nodiscard]] bool ascii_icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto found = std::ranges::search(
        haystack, needle, [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
    return !found.empty();
}

// Shared body for String.index_of / last_index_of.  `from_end` selects rfind
// (last occurrence) over find (first occurrence).  Returns the codepoint index
// of the match as a success result, or a failure when `sub` is absent.
[[nodiscard]] Value find_index(const std::string& s, const std::string& sub, bool from_end) {
    const auto pos = from_end ? s.rfind(sub) : s.find(sub);

    if (pos == std::string::npos) {
        return make_failure_value(std::string{"substring not found"});
    }

    return make_success_value(Value{utf8_codepoint_index(s, pos)});
}

// Which end(s) of a string String.trim* should strip whitespace from.
enum class TrimSide {
    Start,
    End,
    Both
};

// Shared body for String.trim / trim_start / trim_end.  TrimSide::Start strips
// leading whitespace, TrimSide::End strips trailing whitespace, TrimSide::Both
// strips both.  A string that is entirely whitespace collapses to empty for
// every variant.
[[nodiscard]] std::string trim_impl(const std::string& s, TrimSide side) {
    std::size_t start{0};
    std::size_t end{s.size()};

    if (side == TrimSide::Start || side == TrimSide::Both) {
        const auto first = s.find_first_not_of(k_whitespace);

        if (first == std::string::npos) {
            return {};
        }

        start = first;
    }

    if (side == TrimSide::End || side == TrimSide::Both) {
        const auto last = s.find_last_not_of(k_whitespace);

        if (last == std::string::npos) {
            return {};
        }

        end = last + 1;
    }

    return s.substr(start, end - start);
}

// ASCII whitespace as recognised by String.split_whitespace / words /
// word_count: space, tab, newline, carriage return, form feed, vertical tab.
[[nodiscard]] inline bool is_ascii_whitespace(char c) {
    switch (c) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case '\f':
        case '\v':
            return true;
        default:
            return false;
    }
}

// Visit each run of non-ASCII-whitespace in `s` (leading, trailing, and
// repeated whitespace are collapsed, so no empty token is produced).  The
// visitor receives each token as a string_view into `s`, so callers decide
// whether to allocate — String.word_count only counts, and
// String.split_whitespace / words check the array limit *before* allocating
// each token rather than after materialising every one.
template <typename Visitor> void for_each_whitespace_token(std::string_view s, Visitor&& visit) {
    std::size_t i{0};
    const std::size_t n = s.size();

    while (i < n) {
        while (i < n && is_ascii_whitespace(s[i])) {
            ++i;
        }

        const std::size_t start = i;

        while (i < n && !is_ascii_whitespace(s[i])) {
            ++i;
        }

        if (i > start) {
            visit(s.substr(start, i - start));
        }
    }
}

// Shared body for String.split_whitespace / words: build the token array,
// enforcing max_array_size as each token is produced.
[[nodiscard]] Value split_whitespace_impl(std::string_view s, std::string_view function_name,
                                          SourceLocation loc) {
    auto arr = std::make_shared<ArrayValue>();

    for_each_whitespace_token(s, [&](std::string_view token) {
        if (arr->elements->size() >= ResourceLimits::max_array_size) {
            throw RuntimeError{
                error_msg("String", function_name, "result exceeds maximum array size"), loc};
        }

        arr->elements->push_back(Value{std::string{token}});
    });

    return Value{std::move(arr)};
}

// Clamp a codepoint-derived byte offset to the string size.  utf8_byte_offset
// trusts each lead byte's declared sequence length, so a string ending in a
// truncated multi-byte sequence (e.g. String.from_bytes([97, 226])) can yield
// an offset past size() — std::string::insert/erase/replace would then throw
// std::out_of_range ("invalid string position").
[[nodiscard]] std::size_t clamped_byte_offset(const std::string& s, std::int64_t codepoint_index) {
    return std::min(utf8_byte_offset(s, codepoint_index), s.size());
}

// Throw when replacing `removed` bytes of `s` with `added` bytes would exceed
// max_string_size.  Overflow-safe: `removed <= s.size()` and each operand is
// at most max_string_size (strings are bounded at creation).
void ensure_edit_within_limit(std::size_t base_size, std::size_t removed, std::size_t added,
                              std::string_view function_name, SourceLocation loc) {
    const auto kept = base_size - removed;

    if (added > ResourceLimits::max_string_size - std::min(kept, ResourceLimits::max_string_size)) {
        throw RuntimeError{error_msg("String", function_name, "result exceeds maximum string size"),
                           loc, "the resulting string would be too large"};
    }
}

// Validate a codepoint range [start, end) against a string of `cp_count`
// codepoints for String.delete / replace_range.  Returns a failure message when
// the range is out of bounds (start < 0, end > cp_count) or inverted
// (start > end); std::nullopt when the range is valid.
[[nodiscard]] std::optional<std::string> validate_range(std::int64_t start, std::int64_t end,
                                                        std::int64_t cp_count,
                                                        std::string_view function_name) {
    if (start < 0 || end > cp_count) {
        return ErrorMessages::index_out_of_bounds(start < 0 ? start : end,
                                                  static_cast<std::size_t>(cp_count));
    }

    if (start > end) {
        return ErrorMessages::value_out_of_range("String", function_name,
                                                 "start must not exceed end");
    }

    return std::nullopt;
}

} // anonymous namespace

// Build a string of exactly `codepoint_count` codepoints by cycling the fill
// pattern.  `fill` must be non-empty (callers guard against an empty fill).
// Declared in string_module.hpp so the String sub-modules share one
// implementation (pad_left/pad_right via apply_padding, and center).
std::string build_cycled_fill(const std::string& fill, std::size_t codepoint_count) {
    const auto fill_cp_len = utf8_count_size(fill);
    const auto full_repeats = codepoint_count / fill_cp_len;
    const auto remainder = codepoint_count % fill_cp_len;

    std::string result;
    result.reserve((full_repeats * fill.size()) + fill.size());

    for (std::size_t i = 0; i < full_repeats; ++i) {
        result += fill;
    }

    if (remainder > 0) {
        const auto byte_end = utf8_byte_offset(fill, static_cast<std::int64_t>(remainder));
        result += fill.substr(0, byte_end);
    }

    return result;
}

// Pad input to target_width using fill as the repeating pattern.
// Declared in string_module.hpp for use across string sub-modules.
std::string apply_padding(const std::string& input, const std::string& fill,
                          std::size_t target_width, PaddingDirection direction) {
    const auto cp_len = utf8_count_size(input);

    if (cp_len >= target_width) {
        return input;
    }

    // build_cycled_fill returns exactly `target_width - cp_len` codepoints, so
    // the concatenation is already the requested width — no trimming needed.
    auto fill_str = build_cycled_fill(fill, target_width - cp_len);

    if (direction == PaddingDirection::Start) {
        fill_str += input;
        return fill_str;
    }

    return input + fill_str;
}

void register_string_ns(const EnvPtr& env) {
    // Character-class constants — canonical alphabets beginners can draw from
    // instead of hand-typing magic literals (mirrors Python's string module).
    env->define("String.digits", Value{std::string{"0123456789"}}, false);
    env->define("String.hex_digits", Value{std::string{"0123456789abcdef"}}, false);
    env->define("String.ascii_lowercase", Value{std::string{"abcdefghijklmnopqrstuvwxyz"}}, false);
    env->define("String.ascii_uppercase", Value{std::string{"ABCDEFGHIJKLMNOPQRSTUVWXYZ"}}, false);
    env->define("String.ascii_letters",
                Value{std::string{"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"}}, false);
    // Space, tab, newline, carriage return, form-feed, vertical-tab.
    env->define("String.whitespace", Value{std::string{" \t\n\r\f\v"}}, false);
    env->define("String.punctuation", Value{std::string{R"(!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)"}},
                false);

    ModuleBuilder{"String", env}
        .func("length", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{utf8_count(s)};
                      })

        .func("byte_length", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{static_cast<std::int64_t>(s.size())};
                      })

        .func("uppercase", 1)
        .extract_body(expect_string,
                      [](const auto& self, const Args&, SourceLocation) -> Value {
                          return Value{transform_ascii(
                              self, [](unsigned char c) { return std::toupper(c); })};
                      })

        .func("lowercase", 1)
        .extract_body(expect_string,
                      [](const auto& self, const Args&, SourceLocation) -> Value {
                          return Value{transform_ascii(
                              self, [](unsigned char c) { return std::tolower(c); })};
                      })

        .func("trim", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{trim_impl(s, TrimSide::Both)};
                      })

        .func("trim_start", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{trim_impl(s, TrimSide::Start)};
                      })

        .func("trim_end", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{trim_impl(s, TrimSide::End)};
                      })

        .func("reverse", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          std::vector<std::size_t> offsets{};
                          offsets.reserve(s.size());

                          std::size_t i{0};

                          while (i < s.size()) {
                              offsets.push_back(i);
                              i += utf8_advance(s, i);
                          }

                          std::string result{};
                          result.reserve(s.size());

                          for (std::size_t j = offsets.size(); j > 0; --j) {
                              const auto start = offsets[j - 1];
                              const auto end = (j < offsets.size()) ? offsets[j] : s.size();
                              result.append(s, start, end - start);
                          }

                          return Value{std::move(result)};
                      })

        .func("title_case", 1)
        .extract_body(expect_string,
                      [](const auto& self, const Args&, SourceLocation) -> Value {
                          auto s = self;
                          bool next_upper{true};

                          for (auto& ch : s) {
                              if (std::isspace(static_cast<unsigned char>(ch))) {
                                  next_upper = true;
                              } else if (next_upper && static_cast<unsigned char>(ch) < 0x80) {
                                  ch = static_cast<char>(
                                      std::toupper(static_cast<unsigned char>(ch)));

                                  next_upper = false;
                              } else {
                                  next_upper = false;
                              }
                          }

                          return Value{std::move(s)};
                      })

        .func("capitalize", 1)
        .extract_body(expect_string,
                      [](const auto& self, const Args&, SourceLocation) -> Value {
                          auto s = self;

                          if (!s.empty()) {
                              const auto c = static_cast<unsigned char>(s[0]);

                              if (c < 0x80) {
                                  s[0] = static_cast<char>(std::toupper(c));
                              }
                          }

                          return Value{std::move(s)};
                      })

        .func("chunk", 2)
        .extract_body(
            expect_string,
            [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                const auto n = expect_integer(args[1], "String.chunk", loc);

                if (n <= 0) {
                    throw RuntimeError{ErrorMessages::must_be_positive("String", "chunk", "size"),
                                       loc, "pass a positive integer as the chunk size"};
                }

                auto arr = std::make_shared<ArrayValue>();

                std::size_t byte_pos{0};

                while (byte_pos < s.size()) {
                    std::size_t cp_count{0};
                    std::size_t chunk_start{byte_pos};

                    while (byte_pos < s.size() && cp_count < static_cast<std::size_t>(n)) {
                        byte_pos += utf8_advance(s, byte_pos);
                        ++cp_count;
                    }

                    if (arr->elements->size() >= ResourceLimits::max_array_size) {
                        throw RuntimeError{
                            error_msg("String", "chunk", "result exceeds maximum array size"), loc};
                    }

                    arr->elements->push_back(Value{s.substr(chunk_start, byte_pos - chunk_start)});
                }

                return Value{std::move(arr)};
            })

        .func("contains", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto& sub = expect_string(args[1], "String.contains", loc);

                          return Value{s.find(sub) != std::string::npos};
                      })

        .func("equals_ignore_case", 2)
        .extract_body(expect_string,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          return Value{ascii_iequals(
                              a, expect_string(args[1], "String.equals_ignore_case", loc))};
                      })

        .func("contains_ignore_case", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          return Value{ascii_icontains(
                              s, expect_string(args[1], "String.contains_ignore_case", loc))};
                      })

        .func("starts_with", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto& prefix = expect_string(args[1], "String.starts_with", loc);

                          return Value{s.starts_with(prefix)};
                      })

        .func("ends_with", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto& suffix = expect_string(args[1], "String.ends_with", loc);

                          return Value{s.ends_with(suffix)};
                      })

        .func("index_of", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          return find_index(s, expect_string(args[1], "String.index_of", loc),
                                            /*from_end=*/false);
                      })

        .func("last_index_of", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          return find_index(s, expect_string(args[1], "String.last_index_of", loc),
                                            /*from_end=*/true);
                      })

        .func("count", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto& sub = expect_string(args[1], "String.count", loc);

                          if (sub.empty()) {
                              return Value{std::int64_t{0}};
                          }

                          std::int64_t count{0};
                          std::size_t pos{0};

                          while ((pos = s.find(sub, pos)) != std::string::npos) {
                              ++count;

                              pos += sub.size();
                          }

                          return Value{count};
                      })

        .func("replace", 3)
        .extract_body(expect_string,
                      [](const auto& self, const Args& args, SourceLocation loc) -> Value {
                          const auto& from = expect_string(args[1], "String.replace", loc);
                          const auto& to = expect_string(args[2], "String.replace", loc);

                          if (from.empty()) {
                              return Value{self};
                          }

                          const auto pos = self.find(from);

                          if (pos == std::string::npos) {
                              return Value{self};
                          }

                          ensure_edit_within_limit(self.size(), from.size(), to.size(), "replace",
                                                   loc);

                          auto s = self;
                          s.replace(pos, from.size(), to);

                          return Value{std::move(s)};
                      })

        .func("replace_all", 3)
        .extract_body(expect_string,
                      [](const auto& self, const Args& args, SourceLocation loc) -> Value {
                          const auto& from = expect_string(args[1], "String.replace_all", loc);
                          const auto& to = expect_string(args[2], "String.replace_all", loc);

                          if (from.empty()) {
                              return Value{self};
                          }

                          // Build the result in a single left-to-right pass: copy the run
                          // before each match, then the replacement.  This avoids the
                          // O(n * matches) tail-shifting that in-place s.replace() incurs when
                          // `from` and `to` differ in length.  The size limit is still checked
                          // incrementally (a pre-check would require counting all matches first,
                          // and the worst-case estimate is very pessimistic when matches overlap
                          // the replacement text) — suitable for a teaching language that
                          // prioritises clarity over micro-optimisation.
                          auto ensure_within_limit = [&](std::size_t size) {
                              if (size > ResourceLimits::max_string_size) {
                                  throw RuntimeError{
                                      error_msg("String", "replace_all",
                                                "result exceeds maximum string size"),
                                      loc, "the replacement produces a string that is too large"};
                              }
                          };

                          std::string result;
                          result.reserve(self.size());

                          std::size_t search{0};
                          std::size_t match{0};

                          while ((match = self.find(from, search)) != std::string::npos) {
                              result.append(self, search, match - search);
                              result += to;
                              search = match + from.size();

                              ensure_within_limit(result.size());
                          }

                          result.append(self, search, std::string::npos);
                          ensure_within_limit(result.size());

                          return Value{std::move(result)};
                      })

        .func("pad_left", 3)
        .extract_body(expect_string,
                      [](const auto& self, const Args& args, SourceLocation loc) -> Value {
                          const auto raw_width = expect_integer(args[1], "String.pad_left", loc);
                          const auto& fill = expect_string(args[2], "String.pad_left", loc);

                          // Check the sign before the size_t cast — a negative width
                          // would otherwise wrap to a huge value and be misreported as
                          // "width exceeds maximum" (matches String.center).
                          if (raw_width < 0) {
                              return make_failure_value(
                                  error_msg("String", "pad_left", "width must not be negative"));
                          }

                          const auto width = static_cast<std::size_t>(raw_width);

                          if (fill.empty()) {
                              return make_success_value(Value{self});
                          }

                          if (width > ResourceLimits::max_pad_width) {
                              return make_failure_value(
                                  error_msg("String", "pad_left", "width exceeds maximum"));
                          }

                          return make_success_value(
                              Value{apply_padding(self, fill, width, PaddingDirection::Start)});
                      })

        .func("pad_right", 3)
        .extract_body(expect_string,
                      [](const auto& self, const Args& args, SourceLocation loc) -> Value {
                          const auto raw_width = expect_integer(args[1], "String.pad_right", loc);
                          const auto& fill = expect_string(args[2], "String.pad_right", loc);

                          if (raw_width < 0) {
                              return make_failure_value(
                                  error_msg("String", "pad_right", "width must not be negative"));
                          }

                          const auto width = static_cast<std::size_t>(raw_width);

                          if (fill.empty()) {
                              return make_success_value(Value{self});
                          }

                          if (width > ResourceLimits::max_pad_width) {
                              return make_failure_value(
                                  error_msg("String", "pad_right", "width exceeds maximum"));
                          }

                          return make_success_value(
                              Value{apply_padding(self, fill, width, PaddingDirection::End)});
                      })

        .func("split_whitespace", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation loc) -> Value {
                          return split_whitespace_impl(s, "split_whitespace", loc);
                      })

        // String.words is an alias of String.split_whitespace.
        .func("words", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation loc) -> Value {
                          return split_whitespace_impl(s, "words", loc);
                      })

        .func("word_count", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          // Count tokens without allocating any of them.
                          std::int64_t count{0};

                          for_each_whitespace_token(s, [&count](std::string_view) { ++count; });

                          return Value{count};
                      })

        .func("insert", 3)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto index = expect_integer(args[1], "String.insert", loc);
                          const auto& text = expect_string(args[2], "String.insert", loc);
                          const auto cp_count = utf8_count(s);

                          if (index < 0 || index > cp_count) {
                              return make_failure_value(ErrorMessages::index_out_of_bounds(
                                  index, static_cast<std::size_t>(cp_count)));
                          }

                          ensure_edit_within_limit(s.size(), 0, text.size(), "insert", loc);

                          const auto byte_pos = clamped_byte_offset(s, index);

                          std::string result = s;
                          result.insert(byte_pos, text);

                          return make_success_value(Value{std::move(result)});
                      })

        .func("delete", 3)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto cp_count = utf8_count(s);
                          const auto start = expect_integer(args[1], "String.delete", loc);
                          const auto end = expect_integer(args[2], "String.delete", loc);

                          if (auto err = validate_range(start, end, cp_count, "delete")) {
                              return make_failure_value(std::move(*err));
                          }

                          const auto byte_start = clamped_byte_offset(s, start);
                          const auto byte_end = clamped_byte_offset(s, end);

                          std::string result = s;
                          result.erase(byte_start, byte_end - byte_start);

                          return make_success_value(Value{std::move(result)});
                      })

        .func("replace_range", 4)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto& text = expect_string(args[3], "String.replace_range", loc);

                          const auto cp_count = utf8_count(s);
                          const auto start = expect_integer(args[1], "String.replace_range", loc);
                          const auto end = expect_integer(args[2], "String.replace_range", loc);

                          if (auto err = validate_range(start, end, cp_count, "replace_range")) {
                              return make_failure_value(std::move(*err));
                          }

                          const auto byte_start = clamped_byte_offset(s, start);
                          const auto byte_end = clamped_byte_offset(s, end);

                          ensure_edit_within_limit(s.size(), byte_end - byte_start, text.size(),
                                                   "replace_range", loc);

                          std::string result = s;
                          result.replace(byte_start, byte_end - byte_start, text);

                          return make_success_value(Value{std::move(result)});
                      })

        .func("starts_with_any", 2)
        .extract_body(
            expect_string,
            [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                const auto& prefixes = expect_array(args[1], "String.starts_with_any", loc);

                for (const auto& elem : *prefixes->elements) {
                    if (s.starts_with(expect_string(elem, "String.starts_with_any", loc))) {
                        return Value{true};
                    }
                }

                return Value{false};
            })

        .func("ends_with_any", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                          const auto& suffixes = expect_array(args[1], "String.ends_with_any", loc);

                          for (const auto& elem : *suffixes->elements) {
                              if (s.ends_with(expect_string(elem, "String.ends_with_any", loc))) {
                                  return Value{true};
                              }
                          }

                          return Value{false};
                      })

        .func("repeat", 2)
        .extract_body(
            expect_string, [](const auto& s, const Args& args, SourceLocation loc) -> Value {
                const auto n = expect_integer(args[1], "String.repeat", loc);

                if (n <= 0) {
                    return make_success_value(Value{std::string{}});
                }

                if (n > ResourceLimits::max_string_repeat) {
                    return make_failure_value(
                        error_msg("String", "repeat", "count exceeds maximum"));
                }

                const auto result_size = s.size() * static_cast<std::size_t>(n);

                if (result_size > ResourceLimits::max_string_size) {
                    return make_failure_value(
                        error_msg("String", "repeat", "result exceeds maximum string size"));
                }

                std::string result{};
                result.reserve(result_size);

                for (std::int64_t i{0}; i < n; ++i) {
                    result += s;
                }

                return make_success_value(Value{std::move(result)});
            });

    register_string_search(env);
    register_string_transform(env);
}

} // namespace luma
