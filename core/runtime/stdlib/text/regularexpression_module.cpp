#include "runtime/stdlib/text/regularexpression_module.hpp"

#include <concepts>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/stdlib_error_helpers.hpp"

namespace luma {

namespace {

// ReDoS heuristic: flags the two pattern shapes most likely to trigger
// catastrophic backtracking in std::regex: (1) nested quantifiers -- a
// quantified group whose body also contains a quantifier (e.g. (a+)+,
// ((a+))+, (?:a+)+); and (2) ambiguous alternation under repetition -- a
// quantified group whose alternatives can match the same text (e.g.
// (a|aa)+, (.|a)+, (a|)+).  It is a conservative approximation: it may flag
// safe patterns (e.g. (ab|ac)+) but must not miss these classes.  Disjoint
// alternation such as (cat|dog)+ is deliberately left alone.
//
// Uses a per-group stack so that deeply nested patterns like ((a+))+
// are detected.  Recognises +, *, ?, and {n,m} as quantifiers.
// Skips group-modifier syntax (?: ?= ?! ?<= ?<!) so the leading ?
// is not mistaken for a quantifier.

// Advance `i` past a [...] character class, handling escapes and
// negation.  `i` should point at the opening '[' on entry.
void skip_character_class(std::string_view pattern, std::size_t& i) {
    ++i;
    if (i < pattern.size() && pattern[i] == '^') {
        ++i;
    }
    while (i < pattern.size() && pattern[i] != ']') {
        if (pattern[i] == '\\' && i + 1 < pattern.size()) {
            ++i;
        }
        ++i;
    }
}

// Advance `i` past group-modifier syntax: (?:), (?=), (?!), (?<=),
// (?<!).  `i` should point at the opening '(' on entry.  The '?' in
// these constructs is NOT a quantifier.
void skip_group_modifier(std::string_view pattern, std::size_t& i) {
    if (i + 1 < pattern.size() && pattern[i + 1] == '?') {
        i += 2; // advance past '(?'
        if (i < pattern.size() && pattern[i] == '<') {
            ++i; // skip '<' in lookbehind
        }
    }
}

} // namespace

// Defined at namespace scope (declared in the header) so unit and fuzz tests can
// drive this trust-boundary walk directly.  The skip_* helpers above keep
// internal linkage; this function reaches them within the same translation unit.
bool has_dangerous_quantifier_nesting(std::string_view pattern) {
    // One entry per open parenthesis (groups[0] is a top-level sentinel).
    struct GroupState {
        bool has_quantifier{false};  // body holds a quantifier (incl. child-propagated)
        bool ambiguous_alt{false};   // body holds an ambiguous alternation (incl. child-propagated)
        bool saw_alternation{false}; // a top-level '|' separates this group's alternatives
        bool awaiting_first{true};   // still looking for the current alternative's first token
        bool dup_first{false};       // two alternatives share a literal first character
        bool wildcard_first{false};  // an alternative starts with a class/wildcard/nested group
        bool empty_alt{false};       // an alternative has no token, e.g. (a|) or (|a)
        std::string first_chars;     // literal first characters seen, for dup detection
    };

    std::vector<GroupState> groups;
    groups.emplace_back(); // top-level sentinel

    // Record the first token of the current alternative of the innermost group.
    // `wildcard` marks tokens that can overlap a sibling alternative (a '.', a
    // [...] class, a \d/\w/\s class escape, or a nested group).
    const auto note_first_token = [&groups](char literal, bool wildcard) {
        GroupState& g = groups.back();
        if (!g.awaiting_first) {
            return;
        }

        g.awaiting_first = false;

        if (wildcard) {
            g.wildcard_first = true;
            return;
        }

        if (g.first_chars.find(literal) != std::string::npos) {
            g.dup_first = true;
        } else {
            g.first_chars.push_back(literal);
        }
    };

    for (std::size_t i{0}; i < pattern.size(); ++i) {
        const char c = pattern[i];

        // Escaped character: it is this alternative's first token.  \d \D \w \W
        // \s \S are character classes (wildcard-like); every other escape stands
        // for the single literal character that follows the backslash.
        if (c == '\\' && i + 1 < pattern.size()) {
            const char esc = pattern[i + 1];
            const bool wild =
                esc == 'd' || esc == 'D' || esc == 'w' || esc == 'W' || esc == 's' || esc == 'S';
            note_first_token(esc, wild);
            ++i;
            continue;
        }

        // Character class: a wildcard-like first token.
        if (c == '[') {
            note_first_token('[', /*wildcard=*/true);
            skip_character_class(pattern, i);
            continue;
        }

        // Open group: a (wildcard-like) first token of the parent's current
        // alternative, then a new innermost group.
        if (c == '(') {
            note_first_token('(', /*wildcard=*/true);
            groups.emplace_back();
            skip_group_modifier(pattern, i);
            continue;
        }

        // Alternation separator at this group's level.
        if (c == '|') {
            GroupState& g = groups.back();
            if (g.awaiting_first) {
                g.empty_alt = true; // the alternative before '|' was empty
            }
            g.saw_alternation = true;
            g.awaiting_first = true; // the next alternative starts here
            continue;
        }

        // Close group.
        if (c == ')') {
            if (groups.size() <= 1) {
                continue; // unmatched paren -- ignore
            }

            GroupState& g = groups.back();
            if (g.saw_alternation && g.awaiting_first) {
                g.empty_alt = true; // trailing empty alternative, e.g. (a|)
            }

            const bool own_ambiguous =
                g.saw_alternation && (g.dup_first || g.wildcard_first || g.empty_alt);
            const bool subtree_quantifier = g.has_quantifier;
            const bool subtree_ambiguous = own_ambiguous || g.ambiguous_alt;

            groups.pop_back(); // g now dangles -- use the derived flags below

            // A group whose body holds a quantifier or an ambiguous alternation
            // becomes a backtracking risk once the group itself is repeated.
            if ((subtree_quantifier || subtree_ambiguous) && i + 1 < pattern.size()) {
                const char next = pattern[i + 1];
                if (next == '+' || next == '*' || next == '?' || next == '{') {
                    return true;
                }
            }

            // Propagate to the parent so an outer quantifier catches a nested
            // risk (e.g. ((a+))+ or ((a|aa))+).
            if (subtree_quantifier) {
                groups.back().has_quantifier = true;
            }
            if (subtree_ambiguous) {
                groups.back().ambiguous_alt = true;
            }

            continue;
        }

        // Quantifiers: +, *, ?
        if (c == '+' || c == '*' || c == '?') {
            if (groups.size() > 1) {
                groups.back().has_quantifier = true;
            }
            continue;
        }

        // Repetition quantifier: {n}, {n,}, {n,m}
        if (c == '{') {
            if (groups.size() > 1) {
                groups.back().has_quantifier = true;
            }
            while (i + 1 < pattern.size() && pattern[i + 1] != '}') {
                ++i;
            }
            if (i + 1 < pattern.size()) {
                ++i; // skip the '}'
            }
            continue;
        }

        // Any other character is an ordinary literal token ('.' matches any
        // character, so it is treated as wildcard-like).
        note_first_token(c, /*wildcard=*/c == '.');
    }

    return false;
}

namespace {

// Cache of regex patterns that have passed ReDoS validation.  Avoids
// re-running the heuristic when the same pattern is used repeatedly
// (e.g. inside a loop).  Guarded by a mutex for thread safety.
constexpr std::size_t k_max_validated_pattern_cache_size{1024};
std::mutex validated_pattern_mutex;

// Lazily-initialised on first use so the set is constructed in a catchable
// context rather than during unsequenced static initialisation.
[[nodiscard]] std::unordered_set<std::string>& validated_patterns() {
    static std::unordered_set<std::string> patterns;
    return patterns;
}

// Returns true if the pattern is safe (cached or passes validation).
// Returns false if the pattern contains dangerous quantifier nesting.
[[nodiscard]] bool is_pattern_redos_safe(const std::string& pattern) {
    {
        const std::scoped_lock lock{validated_pattern_mutex};
        if (validated_patterns().contains(pattern)) {
            return true;
        }
    }

    if (has_dangerous_quantifier_nesting(pattern)) {
        return false;
    }

    {
        const std::scoped_lock lock{validated_pattern_mutex};
        if (validated_patterns().size() < k_max_validated_pattern_cache_size) {
            validated_patterns().insert(pattern);
        }
    }

    return true;
}

// Cache of compiled std::regex objects, keyed by the cleaned pattern string
// (named-group syntax already stripped by parse_named_capture_groups -- see
// below).  Constructing a std::regex parses the pattern into an automaton,
// which is far more expensive than the ReDoS walk above; reusing the compiled
// object turns a pattern used in a loop into a one-time cost.  Capped lower
// than the validation cache because a compiled automaton is heavier than the
// pattern string it came from.  Like the validation cache it never evicts --
// once full it simply stops inserting.
//
// The map stores shared_ptr<const std::regex>: a handle returned to a caller
// stays valid and immutable after the lock is released, so concurrent const
// regex operations (regex_search / regex_replace, all const on the pattern) are
// data-race free even while another thread inserts a different pattern.  Keying
// by pattern alone is correct only because every call site constructs the regex
// with the same (default ECMAScript) flags; a flagged variant would need the
// flags folded into the key.
constexpr std::size_t k_max_compiled_regex_cache_size{256};
std::mutex compiled_regex_mutex;

[[nodiscard]] std::unordered_map<std::string, std::shared_ptr<const std::regex>>&
compiled_regexes() {
    static std::unordered_map<std::string, std::shared_ptr<const std::regex>> cache;
    return cache;
}

// Return a compiled regex for `pattern`, reusing a cached one when the pattern
// recurs.  Compilation happens outside the lock so concurrent callers never
// serialise on the regex engine; the mutex only guards the map.  Callers must
// have already cleared the size and ReDoS checks (expect_text_and_pattern /
// is_valid), so only safe patterns are ever cached.  A std::regex_error from an
// invalid pattern propagates exactly as a direct std::regex construction would.
[[nodiscard]] std::shared_ptr<const std::regex> get_compiled_regex(const std::string& pattern) {
    {
        const std::scoped_lock lock{compiled_regex_mutex};
        if (const auto it = compiled_regexes().find(pattern); it != compiled_regexes().end()) {
            return it->second;
        }
    }

    auto compiled = std::make_shared<const std::regex>(pattern);

    {
        const std::scoped_lock lock{compiled_regex_mutex};
        if (compiled_regexes().size() < k_max_compiled_regex_cache_size) {
            // try_emplace so a pattern another thread compiled and inserted in
            // the meantime wins harmlessly -- both callers get a valid regex.
            return compiled_regexes().try_emplace(pattern, std::move(compiled)).first->second;
        }
    }

    return compiled;
}

// Pattern with named-capture-group syntax stripped for the regex engine, plus
// a map from each capturing group's 1-based positional index (the std::smatch
// submatch index) to the name it was declared with.
//
// MEDIUM-RISK ENGINE LIMITATION: std::regex's ECMAScript grammar has no native
// named-group support -- neither PCRE-style `(?P<name>...)` nor
// .NET/PCRE2-style `(?<name>...)` parses; both throw std::regex_error from
// std::regex's constructor. Named groups are therefore a purely client-side
// convenience layered on top of an engine that does not know they exist: the
// name annotation is stripped down to an ordinary capturing `(` before
// compilation (`cleaned`), and RegularExpression.find/find_all separately
// build Match.named_groups from this index-to-name map. Once compiled, a named
// group behaves exactly like a plain `(...)` capturing group -- there is no
// engine-level notion of "named" beyond this bookkeeping.
struct ParsedPattern {
    std::string cleaned;
    std::unordered_map<std::size_t, std::string> group_names;
};

[[nodiscard]] ParsedPattern parse_named_capture_groups(std::string_view pattern) {
    ParsedPattern parsed;
    parsed.cleaned.reserve(pattern.size());

    std::size_t group_index{0};

    for (std::size_t i{0}; i < pattern.size(); ++i) {
        const char c = pattern[i];

        // Escaped character: copy through untouched -- never a group opener.
        if (c == '\\' && i + 1 < pattern.size()) {
            parsed.cleaned += c;
            parsed.cleaned += pattern[i + 1];
            ++i;
            continue;
        }

        // Character class: copy through untouched -- a literal '(' inside
        // [...] is not a group opener either.
        if (c == '[') {
            const std::size_t start = i;
            skip_character_class(pattern, i); // i now at the closing ']' (or end)
            parsed.cleaned.append(pattern.substr(start, i - start + 1));
            continue;
        }

        if (c != '(') {
            parsed.cleaned += c;
            continue;
        }

        // (?P<name>...): Python-style named group.
        if (i + 3 < pattern.size() && pattern[i + 1] == '?' && pattern[i + 2] == 'P' &&
            pattern[i + 3] == '<') {
            if (const auto name_end = pattern.find('>', i + 4);
                name_end != std::string_view::npos) {
                ++group_index;
                parsed.group_names[group_index] =
                    std::string(pattern.substr(i + 4, name_end - (i + 4)));
                parsed.cleaned += '(';
                i = name_end;
                continue;
            }
        }

        // (?<name>...): .NET/PCRE2-style named group -- but not a lookbehind
        // (?<=...) / (?<!...), which is not a capturing group at all.
        if (i + 2 < pattern.size() && pattern[i + 1] == '?' && pattern[i + 2] == '<' &&
            !(i + 3 < pattern.size() && (pattern[i + 3] == '=' || pattern[i + 3] == '!'))) {
            if (const auto name_end = pattern.find('>', i + 3);
                name_end != std::string_view::npos) {
                ++group_index;
                parsed.group_names[group_index] =
                    std::string(pattern.substr(i + 3, name_end - (i + 3)));
                parsed.cleaned += '(';
                i = name_end;
                continue;
            }
        }

        // Non-capturing / lookaround modifier: (?: (?= (?! (?<= (?<! -- copy
        // through unchanged; these do not consume a group index.
        if (i + 1 < pattern.size() && pattern[i + 1] == '?') {
            parsed.cleaned += c;
            continue;
        }

        // Plain capturing group.
        ++group_index;
        parsed.cleaned += c;
    }

    return parsed;
}

// Validate text (args[0]) and pattern (args[1]) are strings, and pattern is within size limit.
// Returns a failure Value if the pattern exceeds maximum size, std::nullopt otherwise.
[[nodiscard]] std::optional<Value> expect_text_and_pattern(std::string_view name,
                                                           std::span<const Value> args,
                                                           const SourceLocation& loc) {
    if (!args[0].is_string()) {
        throw RuntimeError{
            std::format("{}: text must be a string, got '{}'", name, args[0].display_type_name()),
            loc};
    }

    if (!args[1].is_string()) {
        throw RuntimeError{std::format("{}: pattern must be a string, got '{}'", name,
                                       args[1].display_type_name()),
                           loc};
    }

    if (args[0].as_string().size() > ResourceLimits::max_regex_input_size) {
        return make_failure_value(
            std::format("{}: input string exceeds maximum size for regex operations", name));
    }

    if (args[1].as_string().size() > ResourceLimits::max_regex_pattern_size) {
        return make_failure_value(std::format("{}: regex pattern exceeds maximum size", name));
    }

    if (!is_pattern_redos_safe(args[1].as_string())) {
        return make_failure_value(
            std::format("{}: regex pattern contains nested quantifiers or ambiguous "
                        "alternation which may cause excessive backtracking",
                        name));
    }

    return std::nullopt;
}

// Build a "Match" record ({text, position, length}) for one submatch: index 0
// is the whole match, index i >= 1 is capture group i.  Shared by the top-level
// match and every capture group so the field layout lives in one place.
[[nodiscard]] std::shared_ptr<RecordValue> make_submatch_record(const std::smatch& match,
                                                                std::size_t index) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Match";
    rec->fields.emplace_back("text", Value{match[index].str()});
    rec->fields.emplace_back("position", Value{static_cast<std::int64_t>(match.position(index))});
    rec->fields.emplace_back("length", Value{static_cast<std::int64_t>(match.length(index))});
    return rec;
}

// Build a "Capture" record ({name, text, position, length}) for one named
// submatch, mirroring make_submatch_record's fields plus the extracted name.
[[nodiscard]] std::shared_ptr<RecordValue>
make_capture_record(const std::smatch& match, std::size_t index, const std::string& name) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Capture";
    rec->fields.emplace_back("name", Value{name});
    rec->fields.emplace_back("text", Value{match[index].str()});
    rec->fields.emplace_back("position", Value{static_cast<std::int64_t>(match.position(index))});
    rec->fields.emplace_back("length", Value{static_cast<std::int64_t>(match.length(index))});
    return rec;
}

// Build a Match record from a regex match result, including capture groups.
// `group_names` maps a 1-based submatch index to the name it was declared
// under in the (already-cleaned) pattern -- see parse_named_capture_groups.
// The positional `groups` array is unchanged; `named_groups` is an additive
// name -> Capture lookup built over the very same submatches.
[[nodiscard]] Value
make_match_record(const std::smatch& match,
                  const std::unordered_map<std::size_t, std::string>& group_names) {
    auto rec = make_submatch_record(match, 0);

    auto groups = std::make_shared<ArrayValue>();
    auto named_groups = std::make_shared<DictionaryValue>();
    named_groups->rebuild_index();

    for (std::size_t i{1}; i < match.size(); ++i) {
        groups->elements->emplace_back(make_submatch_record(match, i));

        if (const auto name_it = group_names.find(i); name_it != group_names.end()) {
            named_groups->set(name_it->second,
                              Value{make_capture_record(match, i, name_it->second)});
        }
    }

    rec->fields.emplace_back("groups", Value{std::move(groups)});
    rec->fields.emplace_back("named_groups", Value{std::move(named_groups)});

    return Value{std::move(rec)};
}

// Run a result-producing regex operation that may hit a resource limit while
// building its output.  A RuntimeError (the match/token cap in find_all and
// split) propagates so the limit aborts the call rather than being reported as
// a recoverable failure; any other std::exception (notably std::regex_error
// from an invalid pattern) becomes a failure result prefixed with the operation
// name, mirroring wrap_result_operation.
template <typename Func>
    requires std::invocable<Func>
[[nodiscard]] Value run_regex_collection(std::string_view function, Func fn) {
    try {
        return fn();
    } catch (const RuntimeError&) {
        throw;
    } catch (const std::exception& e) {
        return make_failure_value(std::format("RegularExpression.{}: {}", function, e.what()));
    }
}

} // namespace

void register_regularexpression_ns(const EnvPtr& env) {
    ModuleBuilder{"RegularExpression", env}
        .func("matches", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (auto err = expect_text_and_pattern("RegularExpression.matches", args, loc)) {
                return *err;
            }

            return wrap_result_operation("RegularExpression", "matches", [&]() -> Value {
                const auto re =
                    get_compiled_regex(parse_named_capture_groups(args[1].as_string()).cleaned);
                const bool matched = std::regex_search(args[0].as_string(), *re);

                return make_success_value(Value{matched});
            });
        })
        .func("find", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (auto err = expect_text_and_pattern("RegularExpression.find", args, loc)) {
                return *err;
            }

            return run_regex_collection("find", [&]() -> Value {
                const auto parsed = parse_named_capture_groups(args[1].as_string());
                const auto re = get_compiled_regex(parsed.cleaned);
                std::smatch match;

                if (std::regex_search(args[0].as_string(), match, *re)) {
                    return make_success_value(make_match_record(match, parsed.group_names));
                }

                return make_failure_value("RegularExpression.find: no match found");
            });
        })
        .func("find_all", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (auto err = expect_text_and_pattern("RegularExpression.find_all", args, loc)) {
                return *err;
            }

            return run_regex_collection("find_all", [&]() -> Value {
                const auto parsed = parse_named_capture_groups(args[1].as_string());
                const auto re = get_compiled_regex(parsed.cleaned);
                const auto& s = args[0].as_string();
                const auto begin = std::sregex_iterator{s.begin(), s.end(), *re};
                const auto end = std::sregex_iterator{};

                auto arr = std::make_shared<ArrayValue>();

                for (auto i = begin; i != end; ++i) {
                    if (arr->elements->size() >= ResourceLimits::max_array_size) {
                        throw RuntimeError{"RegularExpression.find_all: too many matches", loc,
                                           "use a more specific pattern to reduce match count"};
                    }

                    arr->elements->push_back(make_match_record(*i, parsed.group_names));
                }

                return make_success_value(Value{std::move(arr)});
            });
        })
        .func("replace", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (auto err = expect_text_and_pattern("RegularExpression.replace", args, loc)) {
                return *err;
            }

            (void)expect_string(args[2], "RegularExpression.replace", loc);

            return wrap_result_operation("RegularExpression", "replace", [&]() -> Value {
                const auto re =
                    get_compiled_regex(parse_named_capture_groups(args[1].as_string()).cleaned);

                return make_success_value(
                    Value{std::regex_replace(args[0].as_string(), *re, args[2].as_string(),
                                             std::regex_constants::format_first_only)});
            });
        })
        .func("replace_all", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (auto err = expect_text_and_pattern("RegularExpression.replace_all", args, loc)) {
                return *err;
            }

            (void)expect_string(args[2], "RegularExpression.replace_all", loc);

            return wrap_result_operation("RegularExpression", "replace_all", [&]() -> Value {
                const auto re =
                    get_compiled_regex(parse_named_capture_groups(args[1].as_string()).cleaned);

                return make_success_value(
                    Value{std::regex_replace(args[0].as_string(), *re, args[2].as_string())});
            });
        })
        .func("split", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (auto err = expect_text_and_pattern("RegularExpression.split", args, loc)) {
                return *err;
            }

            return run_regex_collection("split", [&]() -> Value {
                const auto re =
                    get_compiled_regex(parse_named_capture_groups(args[1].as_string()).cleaned);
                const auto& s = args[0].as_string();
                const std::sregex_token_iterator begin{s.begin(), s.end(), *re, -1};
                const std::sregex_token_iterator end{};

                auto arr = std::make_shared<ArrayValue>();

                for (auto i = begin; i != end; ++i) {
                    if (arr->elements->size() >= ResourceLimits::max_array_size) {
                        throw RuntimeError{"RegularExpression.split: too many tokens", loc,
                                           "use a more specific pattern to reduce split count"};
                    }

                    arr->elements->emplace_back(std::string{*i});
                }

                return make_success_value(Value{std::move(arr)});
            });
        })
        .func("is_valid", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "RegularExpression.is_valid", loc);

            if (args[0].as_string().size() > ResourceLimits::max_regex_pattern_size) {
                return Value{false};
            }

            if (!is_pattern_redos_safe(args[0].as_string())) {
                return Value{false};
            }

            try {
                (void)get_compiled_regex(parse_named_capture_groups(args[0].as_string()).cleaned);

                return Value{true};
            } catch (const std::exception&) {
                return Value{false};
            }
        });
}

} // namespace luma
