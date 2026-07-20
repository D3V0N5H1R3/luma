// String module — transform, validation, and encoding operations.
// Split from string_module.cpp for readability.  Registered by
// register_string_transform() called from register_string_ns().

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "common/utf8.hpp"
#include "common/utf8_iterator.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/text/string_module.hpp"

namespace luma {

// Shared body for String.is_uppercase / is_lowercase.  Returns true when the
// string contains at least one alphabetic character and every alphabetic
// character satisfies `case_pred` (std::isupper for uppercase, std::islower for
// lowercase).  Non-alphabetic characters are ignored; the empty string and
// strings containing no letters return false.
template <typename CasePred>
[[nodiscard]] static bool all_letters_have_case(const std::string& s, CasePred case_pred) {
    bool has_alpha{false};

    for (const auto ch : s) {
        const auto uc = static_cast<unsigned char>(ch);

        if (std::isalpha(uc)) {
            has_alpha = true;

            if (!case_pred(uc)) {
                return false;
            }
        }
    }

    return has_alpha;
}

// Convert camelCase/PascalCase to a delimiter-separated lower-case form.  `sep`
// is inserted before each internal capital (snake uses '_', kebab uses '-');
// `convert_from`, when set, rewrites an existing delimiter to `sep` (kebab
// rewrites '_' → '-'; snake performs no such rewrite).  ASCII only.
[[nodiscard]] static std::string to_delimited_case(std::string_view s, char sep,
                                                   std::optional<char> convert_from) {
    std::string result;

    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);

        if (convert_from && c == static_cast<unsigned char>(*convert_from)) {
            result += sep;
        } else if (c >= 'A' && c <= 'Z') {
            if (i > 0) {
                const bool prev_upper = static_cast<unsigned char>(s[i - 1]) >= 'A' &&
                                        static_cast<unsigned char>(s[i - 1]) <= 'Z';
                const bool next_lower = (i + 1 < s.size()) &&
                                        static_cast<unsigned char>(s[i + 1]) >= 'a' &&
                                        static_cast<unsigned char>(s[i + 1]) <= 'z';

                if (!prev_upper || next_lower) {
                    result += sep;
                }
            }

            result += static_cast<char>(c + 32);
        } else {
            result += static_cast<char>(c);
        }
    }

    return result;
}

// Convert snake_case/kebab-case to a run-together word form, capitalising the
// letter after each delimiter.  `upper_first` selects PascalCase (capitalise the
// first word too) versus camelCase (force the first letter lower-case).  ASCII
// only.
[[nodiscard]] static std::string to_word_case(std::string_view s, bool upper_first) {
    std::string result;
    bool capitalize_next = upper_first;
    bool first_letter_seen = upper_first;

    for (const auto c : s) {
        if (c == '_' || c == '-') {
            capitalize_next = true;
        } else {
            const auto uc = static_cast<unsigned char>(c);

            if (capitalize_next && first_letter_seen && uc < 0x80) {
                if (uc >= 'a' && uc <= 'z') {
                    result += static_cast<char>(uc - 32);
                } else {
                    result += c;
                }

                capitalize_next = false;
            } else {
                if (!first_letter_seen && uc >= 'A' && uc <= 'Z' && uc < 0x80) {
                    result += static_cast<char>(uc + 32);
                } else {
                    result += c;
                }

                if ((uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z')) {
                    first_letter_seen = true;
                }
            }
        }
    }

    return result;
}

void register_string_transform(const EnvPtr& env) {
    ModuleBuilder{"String", env}
        .func("center", 3)
        .extract_body(expect_string,
                      [](const auto& self, const Args& args, SourceLocation) -> Value {
                          const auto raw_width = args[1].as_integer();

                          if (raw_width < 0) {
                              return make_failure_value(
                                  error_msg("String", "center", "width must not be negative"));
                          }

                          const auto width = static_cast<std::size_t>(raw_width);
                          const auto& fill = args[2].as_string();
                          const auto cp_len = utf8_count_size(self);

                          if (cp_len >= width || fill.empty()) {
                              return make_success_value(Value{self});
                          }

                          if (width > ResourceLimits::max_pad_width) {
                              return make_failure_value(
                                  error_msg("String", "center", "width exceeds maximum"));
                          }

                          const auto total_pad = width - cp_len;
                          const auto left_pad = total_pad / 2;
                          const auto right_pad = total_pad - left_pad;

                          std::string result = build_cycled_fill(fill, left_pad);
                          result += self;
                          result += build_cycled_fill(fill, right_pad);

                          return make_success_value(Value{std::move(result)});
                      })

        .func("characters", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation loc) -> Value {
                          auto arr = std::make_shared<ArrayValue>();

                          std::size_t i{0};

                          while (i < s.size()) {
                              if (arr->elements->size() >= ResourceLimits::max_array_size) {
                                  throw RuntimeError{error_msg("String", "characters",
                                                               "result exceeds maximum array size"),
                                                     loc};
                              }

                              arr->elements->push_back(Value{utf8_char_at_byte(s, i)});

                              i += utf8_advance(s, i);
                          }

                          return Value{std::move(arr)};
                      })

        .func("remove_prefix", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation) -> Value {
                          const auto& prefix = args[1].as_string();

                          if (s.starts_with(prefix)) {
                              return Value{s.substr(prefix.size())};
                          }

                          return Value{s};
                      })

        .func("remove_suffix", 2)
        .extract_body(expect_string,
                      [](const auto& s, const Args& args, SourceLocation) -> Value {
                          const auto& suffix = args[1].as_string();

                          if (s.ends_with(suffix)) {
                              return Value{s.substr(0, s.size() - suffix.size())};
                          }

                          return Value{s};
                      })

        .char_predicate(
            "is_whitespace", [](unsigned char c) { return std::isspace(c) != 0; },
            /*empty_result=*/false)

        .char_predicate(
            "is_blank", [](unsigned char c) { return std::isspace(c) != 0; },
            /*empty_result=*/true)

        .func("is_uppercase", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{all_letters_have_case(
                              s, [](unsigned char c) { return std::isupper(c) != 0; })};
                      })

        .func("is_lowercase", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{all_letters_have_case(
                              s, [](unsigned char c) { return std::islower(c) != 0; })};
                      })

        .char_predicate(
            "is_digit", [](unsigned char c) { return std::isdigit(c) != 0; },
            /*empty_result=*/false)

        // Relocated from string_module_search.cpp so the character-class
        // predicates live together.  is_numeric keeps its bespoke sign/decimal
        // scan; is_alpha and is_alphanumeric are plain all-of predicates.
        .func("is_numeric", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          if (s.empty()) {
                              return Value{false};
                          }

                          bool has_dot{false};
                          bool has_digit{false};

                          bool first{true};

                          for (const auto ch : s) {
                              if (ch == '-' && first) {
                                  first = false;

                                  continue;
                              }

                              first = false;

                              if (ch == '.' && !has_dot) {
                                  has_dot = true;

                                  continue;
                              }

                              if (!std::isdigit(static_cast<unsigned char>(ch))) {
                                  return Value{false};
                              }

                              has_digit = true;
                          }

                          return Value{has_digit};
                      })

        .char_predicate(
            "is_alpha", [](unsigned char c) { return std::isalpha(c) != 0; },
            /*empty_result=*/false)

        .char_predicate(
            "is_alphanumeric", [](unsigned char c) { return std::isalnum(c) != 0; },
            /*empty_result=*/false)

        .func("to_codepoints", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation loc) -> Value {
                          auto arr = std::make_shared<ArrayValue>();

                          std::size_t i{0};

                          while (i < s.size()) {
                              if (arr->elements->size() >= ResourceLimits::max_array_size) {
                                  throw RuntimeError{error_msg("String", "to_codepoints",
                                                               "result exceeds maximum array size"),
                                                     loc};
                              }

                              arr->elements->emplace_back(
                                  static_cast<std::int64_t>(utf8_decode_at(s, i)));

                              i += utf8_advance(s, i);
                          }

                          return Value{std::move(arr)};
                      })

        .func("from_codepoints", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "String.from_codepoints", loc)->elements;

            std::string result{};

            for (const auto& elem : elems) {
                const auto raw = elem.as_integer();

                if (raw < 0 || raw > 0x10FFFF || (raw >= 0xD800 && raw <= 0xDFFF)) {
                    return failure_msg("String", "from_codepoints",
                                       std::format("invalid codepoint: {}", raw));
                }

                const auto cp = static_cast<std::uint32_t>(raw);
                result += utf8_encode(cp);
            }

            return make_success_value(Value{std::move(result)});
        })

        .func("to_bytes", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation loc) -> Value {
                          auto arr = std::make_shared<ArrayValue>();

                          for (auto ch : s) {
                              if (arr->elements->size() >= ResourceLimits::max_array_size) {
                                  throw RuntimeError{error_msg("String", "to_bytes",
                                                               "result exceeds maximum array size"),
                                                     loc};
                              }

                              arr->elements->emplace_back(
                                  static_cast<std::int64_t>(static_cast<std::uint8_t>(ch)));
                          }

                          return Value{std::move(arr)};
                      })

        .func("from_bytes", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "String.from_bytes", loc)->elements;

            std::string result{};
            result.reserve(elems.size());

            for (const auto& elem : elems) {
                const auto byte = elem.as_integer();

                if (byte < 0 || byte > 255) {
                    return failure_msg("String", "from_bytes",
                                       std::format("byte value out of range: {}", byte));
                }

                result += static_cast<char>(byte);
            }

            return make_success_value(Value{std::move(result)});
        })

        // String.is_ascii(value: string) -> boolean
        // Returns true if the string contains only ASCII characters (0x00-0x7F).
        .char_predicate(
            "is_ascii", [](unsigned char c) { return c <= 0x7F; },
            /*empty_result=*/true)

        // String.common_prefix(a: string, b: string) -> string
        // Returns the longest common prefix of two strings.
        .func("common_prefix", 2)
        .extract_body(expect_string,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          const auto& b = expect_string(args[1], "String.common_prefix", loc);

                          std::size_t i{0};

                          while (i < a.size() && i < b.size()) {
                              const auto len_a = utf8_advance(a, i);
                              const auto len_b = utf8_advance(b, i);

                              if (len_a != len_b || i + len_a > a.size() || i + len_b > b.size() ||
                                  a.compare(i, len_a, b, i, len_b) != 0) {
                                  break;
                              }

                              i += len_a;
                          }

                          return Value{a.substr(0, i)};
                      })

        // String.common_suffix(a: string, b: string) -> string
        // Returns the longest common suffix of two strings.
        .func("common_suffix", 2)
        .extract_body(expect_string,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          const auto& b = expect_string(args[1], "String.common_suffix", loc);

                          std::size_t ia = a.size();
                          std::size_t ib = b.size();

                          while (ia > 0 && ib > 0) {
                              // Back up to the start of the previous codepoint in a.
                              std::size_t sa = ia - 1;

                              while (sa > 0 && (static_cast<unsigned char>(a[sa]) & 0xC0) == 0x80) {
                                  --sa;
                              }

                              // Back up to the start of the previous codepoint in b.
                              std::size_t sb = ib - 1;

                              while (sb > 0 && (static_cast<unsigned char>(b[sb]) & 0xC0) == 0x80) {
                                  --sb;
                              }

                              const auto len_a = ia - sa;
                              const auto len_b = ib - sb;

                              if (len_a != len_b || a.compare(sa, len_a, b, sb, len_b) != 0) {
                                  break;
                              }

                              ia = sa;
                              ib = sb;
                          }

                          return Value{a.substr(ia)};
                      })

        // String.to_snake_case(value: string) -> string
        // Converts camelCase/PascalCase to snake_case. ASCII only.
        .func("to_snake_case", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{to_delimited_case(s, '_', std::nullopt)};
                      })

        // String.to_camel_case(value: string) -> string
        // Converts snake_case/kebab-case to camelCase. ASCII only.
        .func("to_camel_case", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{to_word_case(s, false)};
                      })

        // String.to_kebab_case(value: string) -> string
        // Converts camelCase/PascalCase/snake_case to kebab-case. ASCII only.
        .func("to_kebab_case", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{to_delimited_case(s, '-', '_')};
                      })

        // String.to_pascal_case(value: string) -> string
        // Converts snake_case/kebab-case to PascalCase. ASCII only.
        .func("to_pascal_case", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          return Value{to_word_case(s, true)};
                      })

        .func("levenshtein_distance", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "String.levenshtein_distance", loc);
            (void)expect_string(args[1], "String.levenshtein_distance", loc);

            const auto& a = args[0].as_string();
            const auto& b = args[1].as_string();

            const auto len_a = utf8_count(a);
            const auto len_b = utf8_count(b);

            if (len_a > ResourceLimits::max_levenshtein_input ||
                len_b > ResourceLimits::max_levenshtein_input) {
                throw RuntimeError{error_msg("String", "levenshtein_distance",
                                             std::format("input exceeds {} codepoints",
                                                         ResourceLimits::max_levenshtein_input)),
                                   loc, "use shorter strings"};
            }

            const auto cols = static_cast<std::size_t>(len_b) + 1;

            std::vector<std::int64_t> prev(cols);
            std::vector<std::int64_t> curr(cols);

            std::iota(prev.begin(), prev.end(), std::int64_t{0});

            std::size_t byte_i = 0;

            for (std::int64_t i = 1; i <= len_a; ++i) {
                const auto cp_a = utf8_char_at_byte(a, byte_i);
                byte_i += utf8_advance(a, byte_i);

                curr[0] = i;

                std::size_t byte_j = 0;

                for (std::int64_t j = 1; j <= len_b; ++j) {
                    const auto cp_b = utf8_char_at_byte(b, byte_j);
                    byte_j += utf8_advance(b, byte_j);

                    const auto cost = (cp_a == cp_b) ? 0 : 1;

                    curr[static_cast<std::size_t>(j)] = std::min({
                        prev[static_cast<std::size_t>(j)] + 1,
                        curr[static_cast<std::size_t>(j) - 1] + 1,
                        prev[static_cast<std::size_t>(j) - 1] + cost,
                    });
                }

                std::swap(prev, curr);
            }

            return Value{prev[static_cast<std::size_t>(len_b)]};
        })

        .func("is_palindrome", 1)
        .extract_body(expect_string,
                      [](const auto& s, const Args&, SourceLocation) -> Value {
                          if (s.empty()) {
                              return Value{true};
                          }

                          std::vector<std::string> codepoints;

                          std::size_t i = 0;

                          while (i < s.size()) {
                              codepoints.push_back(utf8_char_at_byte(s, i));
                              i += utf8_advance(s, i);
                          }

                          const auto len = codepoints.size();

                          const auto half =
                              codepoints.begin() + static_cast<std::ptrdiff_t>(len / 2);
                          return Value{std::equal(codepoints.begin(), half, codepoints.rbegin())};
                      })

        .func("slug", 1)
        .extract_body(expect_string, [](const auto& self, const Args&, SourceLocation) -> Value {
            std::string result;

            for (const auto ch : self) {
                const auto c = static_cast<unsigned char>(ch);

                if (c < 0x80 && std::isalnum(c)) {
                    result += static_cast<char>(std::tolower(c));
                } else {
                    if (!result.empty() && result.back() != '-') {
                        result += '-';
                    }
                }
            }

            // Trim trailing hyphens.
            while (!result.empty() && result.back() == '-') {
                result.pop_back();
            }

            return Value{std::move(result)};
        });
}

} // namespace luma
