#ifndef LUMA_COMMON_ESCAPE_HPP
#define LUMA_COMMON_ESCAPE_HPP

// ═══════════════════════════════════════════════════════════════════════════
// Unified escape utilities
// ═══════════════════════════════════════════════════════════════════════════
// Provides policy-based string escaping for JSON, XML, JavaScript, and HTML.
//
// Public API:
//   json_escape(input)                 — new JSON-escaped string (escapes /)
//   json_escape_string(input, out)     — append JSON-escaped (no / escape)
//   xml_escape_string(input, out)      — append XML-escaped
//   js_string_escape(input)            — new JS-escaped string
//   html_escape(input)                 — new HTML-escaped string
//   html_escape_string(input, out)     — append HTML-escaped
//
// Low-level:
//   escape_string_impl<Policy>(input, out)  — append with custom policy
//   escape_string_as<Policy>(input)         — return with custom policy
// ═══════════════════════════════════════════════════════════════════════════

#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace luma {

// ─── Shared constants ────────────────────────────────────────────────────

// ASCII control characters span U+0000 to U+001F.
constexpr unsigned char k_ascii_control_max = 0x1F;

// ─── Detail helpers ──────────────────────────────────────────────────────

namespace detail {

// An EscapePolicy must provide one of:
//   - static void escape_char(char, std::string&)        (direct output)
//   - static constexpr std::optional<std::string_view> try_escape(char) noexcept  (replacement lookup)
template <typename P>
concept HasEscapeChar = requires(char c, std::string& out) { P::escape_char(c, out); };

} // namespace detail

// ─── Generic escape engine ───────────────────────────────────────────────

// EscapePolicy must provide one of:
//   static constexpr std::optional<std::string_view> try_escape(char c) noexcept;
//   static void escape_char(char c, std::string& output);

template <typename EscapePolicy>
void escape_string_impl(std::string_view input, std::string& output) {
    output.reserve(output.size() + input.size());

    if constexpr (detail::HasEscapeChar<EscapePolicy>) {
        for (const char c : input) {
            EscapePolicy::escape_char(c, output);
        }
    } else {
        for (const char c : input) {
            if (auto escaped = EscapePolicy::try_escape(c)) {
                output.append(*escaped);
            } else {
                output.push_back(c);
            }
        }
    }
}

// Convenience: returns a new string instead of appending.
template <typename EscapePolicy>
[[nodiscard]] std::string escape_string_as(std::string_view input) {
    std::string result;
    escape_string_impl<EscapePolicy>(input, result);
    return result;
}

// ─── JSON escape policy ─────────────────────────────────────────────────
// Handles: \\, \n, \r, \", \t, \b, \f, and control chars as \uXXXX.
// Template parameter controls whether forward slashes are escaped.

template <bool EscapeForwardSlash = false> struct JsonEscapePolicy {
    static void escape_char(char c, std::string& out) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if constexpr (EscapeForwardSlash) {
                    if (c == '/') {
                        out += "\\/";
                        return;
                    }
                }
                if (static_cast<unsigned char>(c) <= k_ascii_control_max) {
                    out += std::format("\\u{:04x}",
                                       static_cast<unsigned int>(static_cast<unsigned char>(c)));
                } else {
                    out += c;
                }
                break;
        }
    }
};

// ─── XML escape policy ──────────────────────────────────────────────────
// Handles the five predefined XML entities.

struct XmlEscapePolicy {
    [[nodiscard]] static constexpr std::optional<std::string_view> try_escape(char c) noexcept {
        switch (c) {
            case '<':
                return "&lt;";
            case '>':
                return "&gt;";
            case '&':
                return "&amp;";
            case '"':
                return "&quot;";
            case '\'':
                return "&apos;";
            default:
                return std::nullopt;
        }
    }
};

// ─── JavaScript escape policy ───────────────────────────────────────────
// Escapes single quotes, backslashes, newlines, and carriage returns for
// embedding in single-quoted JS string literals.

struct JsEscapePolicy {
    [[nodiscard]] static constexpr std::optional<std::string_view> try_escape(char c) noexcept {
        switch (c) {
            case '\\':
                return "\\\\";
            case '\n':
                return "\\n";
            case '\r':
                return "\\r";
            case '\'':
                return "\\'";
            default:
                return std::nullopt;
        }
    }
};

// ─── HTML escape policy ─────────────────────────────────────────────────
// Handles the five characters that must be escaped in HTML content and
// attribute values: < > & " '

struct HtmlEscapePolicy {
    [[nodiscard]] static constexpr std::optional<std::string_view> try_escape(char c) noexcept {
        switch (c) {
            case '<':
                return "&lt;";
            case '>':
                return "&gt;";
            case '&':
                return "&amp;";
            case '"':
                return "&quot;";
            case '\'':
                return "&#x27;";
            default:
                return std::nullopt;
        }
    }
};

// ─── Public API functions ───────────────────────────────────────────────

// Escape a string for embedding in a JSON string literal.
// Forward slashes are escaped as `\/`.
[[nodiscard]] inline std::string json_escape(std::string_view s) {
    return escape_string_as<JsonEscapePolicy<true>>(s);
}

// Append a JSON-escaped version of the input string to an existing output buffer.
// Does not escape forward slashes — they are optional in the JSON spec and
// omitting them produces cleaner output for most use cases.
inline void json_escape_string(std::string_view input, std::string& out) {
    escape_string_impl<JsonEscapePolicy<false>>(input, out);
}

// Append an XML-escaped version of the input string to an existing output buffer.
// Handles the five predefined XML entities.
inline void xml_escape_string(std::string_view input, std::string& out) {
    escape_string_impl<XmlEscapePolicy>(input, out);
}

// Escape a string for embedding in a single-quoted JavaScript string literal.
[[nodiscard]] inline std::string js_string_escape(std::string_view s) {
    return escape_string_as<JsEscapePolicy>(s);
}

// Append an HTML-escaped version of the input string to an existing output buffer.
inline void html_escape_string(std::string_view input, std::string& out) {
    escape_string_impl<HtmlEscapePolicy>(input, out);
}

// Return a new HTML-escaped string.
[[nodiscard]] inline std::string html_escape(std::string_view input) {
    return escape_string_as<HtmlEscapePolicy>(input);
}

} // namespace luma

#endif // LUMA_COMMON_ESCAPE_HPP
