#include "lsp_text_formatter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "lsp_lexical_context.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Formatting helpers
// ═══════════════════════════════════════════════════════════

namespace {

// Check whether a character is an identifier/number character (not an operator boundary).
[[nodiscard]] bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Returns true if a space should NOT be added before an operator
// (because the previous char is already a space or an opener).
[[nodiscard]] constexpr bool is_no_space_before(char prev) {
    return prev == ' ' || prev == '(' || prev == '[' || prev == ',';
}

// ───────────────────────────────────────────────────────────
// Operator spacing helpers.
// Apply spacing rules to two-character and single-character
// binary operators.  These assume the caller has already
// verified that the position is in a code context (not inside
// a string literal or comment).
// ───────────────────────────────────────────────────────────

// Two-character operator pairs recognised by the formatter.
constexpr std::array<std::pair<char, char>, 11> two_char_operators{{
    {'=', '='},
    {'!', '='},
    {'<', '='},
    {'>', '='},
    {'|', '>'},
    {'-', '>'},
    {'.', '.'},
    {'+', '='},
    {'-', '='},
    {'*', '='},
    {'/', '='},
}};

// Try to normalise a two-character operator starting at `i`.
// Returns true (and advances `i` past the second character) if a
// two-character operator was found and handled; false otherwise.
[[nodiscard]] bool try_normalize_two_char_operator(const std::string& line, std::size_t& i,
                                                   std::string& out) {
    if (i + 1 >= line.size()) {
        return false;
    }

    const char c = line[i];
    const char c2 = line[i + 1];
    const bool is_two_char = std::ranges::any_of(
        two_char_operators, [c, c2](const auto& p) { return p.first == c && p.second == c2; });

    if (!is_two_char) {
        return false;
    }

    // Ensure space before the operator (unless at line start).
    if (!out.empty() && !is_no_space_before(out.back())) {
        out += ' ';
    }

    out += c;
    out += c2;

    // Ensure space after the operator.
    if (i + 2 < line.size() && line[i + 2] != ' ' && line[i + 2] != '\n') {
        out += ' ';
    }

    ++i; // skip second char
    return true;
}

// Normalise spacing around a single-character binary operator.
// Returns true if the character was handled as an operator.
[[nodiscard]] bool try_normalize_single_char_operator(const std::string& line, std::size_t i,
                                                      std::string& out) {
    const char c = line[i];

    // '=' — normalize assignment spacing.
    if (c == '=') {
        if (!out.empty() && (is_ident_char(out.back()) || out.back() == ')' || out.back() == ']')) {
            if (out.back() != ' ') {
                out += ' ';
            }
            out += c;
            if (i + 1 < line.size() && line[i + 1] != ' ' && line[i + 1] != '\n') {
                out += ' ';
            }
            return true;
        }

        out += c;
        return true;
    }

    // '+', '*', '/' — always binary.
    if (c == '+' || c == '*' || c == '/') {
        if (!out.empty() && !is_no_space_before(out.back())) {
            out += ' ';
        }
        out += c;
        if (i + 1 < line.size() && line[i + 1] != ' ' && line[i + 1] != '\n') {
            out += ' ';
        }
        return true;
    }

    return false;
}

// ───────────────────────────────────────────────────────────
// Top-level operator spacing normalizer
// ───────────────────────────────────────────────────────────

// Normalize spacing around binary operators in a single line of Luma code.
// Skips string literals and comments. Returns the rewritten line.
[[nodiscard]] std::string normalize_operator_spacing(const std::string& line) {
    std::string out;
    out.reserve(line.size() + 16);

    lexical::LineContext ctx;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        // Let the context tracker handle strings, comments, interpolation.
        bool append_rest = false;

        if (ctx.update(line, i, out, append_rest)) {
            if (append_rest) {
                return out;
            }
            continue;
        }

        // Two-character operators: ==, !=, <=, >=, |>, ->, .., +=, -=, *=, /=
        if (try_normalize_two_char_operator(line, i, out)) {
            continue;
        }

        // Single-character binary operators: = + * /
        // Don't touch '<' and '>' — can't distinguish generics from comparisons
        // without semantic analysis, so leave them as-is.
        if ((c == '=' || c == '+' || c == '*' || c == '/') && !ctx.in_string) {
            if (try_normalize_single_char_operator(line, i, out)) {
                continue;
            }
        }

        // '-' — distinguish binary minus from unary minus.
        if (c == '-' && !ctx.in_string) {
            const bool is_binary = !out.empty() && (is_ident_char(out.back()) ||
                                                    out.back() == ')' || out.back() == ']');
            if (is_binary) {
                if (out.back() != ' ') {
                    out += ' ';
                }
                out += c;
                if (i + 1 < line.size() && line[i + 1] != ' ' && line[i + 1] != '\n') {
                    out += ' ';
                }
                continue;
            }
        }

        out += c;
    }

    return out;
}

// Ensure a blank line before top-level declarations (function, record, choice, etc.)
// when they are immediately preceded by a non-blank non-declaration line.
[[nodiscard]] bool is_declaration_start(const std::string& trimmed) {
    return trimmed.starts_with("function ") || trimmed.starts_with("record ") ||
           trimmed.starts_with("choice ") || trimmed.starts_with("interface ") ||
           trimmed.starts_with("namespace ") || trimmed.starts_with("@");
}

// Check whether a blank separator line should be inserted before a
// top-level declaration.  Examines the result buffer to see if the
// previous line was non-blank and not a comment/annotation.
[[nodiscard]] bool should_add_blank_line_before_declaration(const std::string& result,
                                                            const std::string& trimmed,
                                                            int indent_level) {
    if (indent_level != 0 || !is_declaration_start(trimmed) || result.empty()) {
        return false;
    }

    if (result.size() < 2 || result[result.size() - 1] != '\n' ||
        result[result.size() - 2] == '\n') {
        return false;
    }

    // Previous line was non-blank — check whether it was a comment or annotation.
    const auto prev_nl = result.rfind('\n', result.size() - 2);
    const auto prev_start = (prev_nl == std::string::npos) ? 0 : prev_nl + 1;
    auto prev_content = result.substr(prev_start, result.size() - 1 - prev_start);

    const auto fc = prev_content.find_first_not_of(" \t");
    if (fc != std::string::npos) {
        prev_content = prev_content.substr(fc);
    }

    return !prev_content.starts_with("#") && !prev_content.starts_with("@");
}

// Determine whether `trimmed` starts with a continuation keyword that
// causes the indent level to decrease before writing the line and
// increase again afterwards (e.g. else, catch, finally, case).
[[nodiscard]] bool is_continuation_keyword(const std::string& trimmed) {
    return trimmed.starts_with("else ") || trimmed == "else" || trimmed.starts_with("else{") ||
           trimmed.starts_with("catch ") || trimmed == "catch" || trimmed.starts_with("catch(") ||
           trimmed.starts_with("catch{") || trimmed.starts_with("finally ") ||
           trimmed == "finally" || trimmed.starts_with("finally{") ||
           trimmed.starts_with("case ") || trimmed == "case";
}

// Compute the indent adjustment to apply before writing a line.
// Returns a negative value when the line starts with a closing
// brace/bracket or a continuation keyword that "outdents".
[[nodiscard]] int compute_indent_before(const std::string& trimmed, int indent_level) {
    int delta = 0;

    // Decrease indent before lines starting with closing braces/brackets.
    if (trimmed.starts_with("}") || trimmed.starts_with("]") || trimmed.starts_with(")")) {
        if (indent_level + delta > 0) {
            --delta;
        }
    }

    // Also decrease for continuation keywords at the start.
    if (is_continuation_keyword(trimmed)) {
        if (indent_level + delta > 0) {
            --delta;
        }
    }

    return delta;
}

// Compute the indent adjustment to apply after writing a line.
// Returns a positive value when the line ends with an opening
// brace/bracket or starts with a continuation keyword.
[[nodiscard]] int compute_indent_after(const std::string& trimmed) {
    if (is_continuation_keyword(trimmed) || trimmed.ends_with("{") || trimmed.ends_with("[") ||
        trimmed.ends_with("(")) {
        return 1;
    }

    return 0;
}

// Strip a raw line of trailing whitespace/CR and leading whitespace,
// returning the trimmed content ready for re-indentation.
[[nodiscard]] std::string strip_line(const std::string& line) {
    auto stripped = line;
    // Strip trailing whitespace and carriage returns.
    while (!stripped.empty() &&
           (stripped.back() == ' ' || stripped.back() == '\t' || stripped.back() == '\r')) {
        stripped.pop_back();
    }
    // Strip leading whitespace.
    std::size_t content_start = 0;
    while (content_start < stripped.size() &&
           (stripped[content_start] == ' ' || stripped[content_start] == '\t')) {
        ++content_start;
    }
    return stripped.substr(content_start);
}

// Handle a blank line, enforcing the maximum consecutive blank line limit.
void emit_blank_line(std::string& result, int& consecutive_blank_lines) {
    ++consecutive_blank_lines;
    if (consecutive_blank_lines <= 2) {
        result += '\n';
    }
}

// Apply formatting to a single non-blank line: operator normalization,
// blank-line insertion before declarations, indent computation, and output.
void apply_line_formatting(std::string& result, const std::string& trimmed_input, int& indent_level,
                           const std::string& indent_unit) {
    // Normalize operator spacing on this line.
    auto trimmed = normalize_operator_spacing(trimmed_input);

    // Ensure a blank line before top-level declarations when the
    // previous line was non-blank and not a comment or annotation.
    if (should_add_blank_line_before_declaration(result, trimmed, indent_level)) {
        result += '\n';
    }

    // Adjust indent level before writing the line.
    const int before_delta = compute_indent_before(trimmed, indent_level);
    indent_level += before_delta;

    // Write the indented line. indent_unit is a run of spaces, so append the
    // whole indent in one call instead of looping once per level.
    if (indent_level > 0) {
        result.append(static_cast<std::size_t>(indent_level) * indent_unit.size(), ' ');
    }
    result += trimmed;
    result += '\n';

    // Adjust indent level after writing the line.
    indent_level += compute_indent_after(trimmed);
}

// Ensure the result has exactly one trailing newline.
void normalize_trailing_newline(std::string& result) {
    while (result.size() > 1 && result[result.size() - 1] == '\n' &&
           result[result.size() - 2] == '\n') {
        result.pop_back();
    }
    if (result.empty() || result.back() != '\n') {
        result += '\n';
    }
}

// ───────────────────────────────────────────────────────────
// Range formatting indent helpers.
// ───────────────────────────────────────────────────────────

// Returns the indent level of the first non-blank line in `range_text`.
[[nodiscard]] int compute_base_indent(const std::string& range_text, int tab_size) {
    std::size_t p = 0;
    while (p < range_text.size()) {
        auto nl = range_text.find('\n', p);
        if (nl == std::string::npos) {
            nl = range_text.size();
        }
        auto segment = range_text.substr(p, nl - p);
        while (!segment.empty() &&
               (segment.back() == ' ' || segment.back() == '\t' || segment.back() == '\r')) {
            segment.pop_back();
        }
        if (!segment.empty()) {
            int spaces = 0;
            for (const char c : range_text.substr(p, nl - p)) {
                if (c == ' ') {
                    ++spaces;
                } else if (c == '\t') {
                    spaces += tab_size;
                } else {
                    break;
                }
            }
            return spaces / tab_size;
        }
        p = nl + 1;
    }
    return 0;
}

// Strips `indent_level * tab_size` leading spaces from each line of `text`.
[[nodiscard]] std::string strip_base_indent(const std::string& text, int indent_level,
                                            int tab_size) {
    std::string result;
    result.reserve(text.size());
    const std::string prefix(static_cast<std::size_t>(indent_level * tab_size), ' ');
    std::size_t p = 0;
    while (p <= text.size()) {
        auto nl = text.find('\n', p);
        if (nl == std::string::npos) {
            nl = text.size();
        }
        auto line = text.substr(p, nl - p);
        if (line.starts_with(prefix)) {
            line = line.substr(prefix.size());
        }
        result += line;
        if (nl < text.size()) {
            result += '\n';
        }
        p = nl + 1;
        if (nl == text.size()) {
            break;
        }
    }
    return result;
}

// Adds `indent_level * tab_size` spaces to the start of each non-empty line.
[[nodiscard]] std::string apply_base_indent(const std::string& text, int indent_level,
                                            int tab_size) {
    if (indent_level == 0) {
        return text;
    }
    const std::string prefix(static_cast<std::size_t>(indent_level * tab_size), ' ');
    std::string result;
    result.reserve(text.size() + (prefix.size() * 20));
    std::size_t p = 0;
    while (p <= text.size()) {
        auto nl = text.find('\n', p);
        if (nl == std::string::npos) {
            nl = text.size();
        }
        auto line = text.substr(p, nl - p);
        if (!line.empty()) {
            result += prefix;
        }
        result += line;
        if (nl < text.size()) {
            result += '\n';
        }
        p = nl + 1;
        if (nl == text.size()) {
            break;
        }
    }
    return result;
}

} // anonymous namespace

std::string format_luma_source(const std::string& source, int tab_size) {
    std::string result;
    result.reserve(source.size());

    const std::string indent_unit(static_cast<std::size_t>(tab_size), ' ');

    int indent_level = 0;
    int consecutive_blank_lines = 0;

    std::size_t pos = 0;

    while (pos <= source.size()) {
        // Find end of line.
        auto eol = source.find('\n', pos);
        if (eol == std::string::npos) {
            eol = source.size();
        }

        // Extract line content (without newline).
        auto line = source.substr(pos, eol - pos);

        auto trimmed = strip_line(line);

        if (trimmed.empty()) {
            emit_blank_line(result, consecutive_blank_lines);
            pos = eol + 1;
            if (eol == source.size()) {
                break;
            }
            continue;
        }

        consecutive_blank_lines = 0;
        apply_line_formatting(result, trimmed, indent_level, indent_unit);

        pos = eol + 1;
        if (eol == source.size()) {
            break;
        }
    }

    normalize_trailing_newline(result);

    return result;
}

std::string format_range_text(const std::string& range_text, int tab_size) {
    const int base_indent = compute_base_indent(range_text, tab_size);
    const auto stripped = strip_base_indent(range_text, base_indent, tab_size);
    const auto formatted_raw = format_luma_source(stripped, tab_size);
    return apply_base_indent(formatted_raw, base_indent, tab_size);
}

} // namespace luma::lsp
