#include <cctype>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "analysis/errors/error.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/io/graphicalui_css.hpp"

// ═══════════════════════════════════════════════════════════
// CSS sanitisation — allowlist-based token filter
// ═══════════════════════════════════════════════════════════

namespace luma::gui_detail {

namespace {

// Allowlisted at-rules (case-insensitive, without the '@').
const std::unordered_set<std::string_view>& safe_at_rules() {
    static const std::unordered_set<std::string_view> rules = {
        "media", "keyframes", "supports", "layer", "container", "font-face",
    };
    return rules;
}

// Allowlisted CSS function names (lowercase).
const std::unordered_set<std::string_view>& safe_functions() {
    static const std::unordered_set<std::string_view> fns = {
        // Math & variables.
        "calc",
        "clamp",
        "min",
        "max",
        "var",
        "env",
        // Colours.
        "rgb",
        "rgba",
        "hsl",
        "hsla",
        "oklch",
        "oklab",
        "lch",
        "lab",
        "color",
        "color-mix",
        "light-dark",
        // Gradients.
        "linear-gradient",
        "radial-gradient",
        "conic-gradient",
        "repeating-linear-gradient",
        "repeating-radial-gradient",
        "repeating-conic-gradient",
        // Transforms.
        "translate",
        "translatex",
        "translatey",
        "translatez",
        "translate3d",
        "rotate",
        "rotatex",
        "rotatey",
        "rotatez",
        "rotate3d",
        "scale",
        "scalex",
        "scaley",
        "scalez",
        "scale3d",
        "skew",
        "skewx",
        "skewy",
        "matrix",
        "matrix3d",
        "perspective",
        // Timing.
        "cubic-bezier",
        "steps",
        // Grid.
        "minmax",
        "repeat",
        "fit-content",
        // Counters.
        "counter",
        "counters",
        "attr",
        // Filters.
        "blur",
        "brightness",
        "contrast",
        "drop-shadow",
        "grayscale",
        "hue-rotate",
        "invert",
        "opacity",
        "saturate",
        "sepia",
        // Shapes.
        "polygon",
        "circle",
        "ellipse",
        "inset",
        "path",
    };
    return fns;
}

// Skip an HTML tag starting at position `i`.  Returns the position just past
// the closing '>', or std::string::npos if the tag is unterminated.
[[nodiscard]] std::size_t skip_html_tag(const std::string& css, std::size_t i) {
    auto end = css.find('>', i);

    if (end != std::string::npos) {
        return end + 1;
    }

    return std::string::npos; // Unterminated tag.
}

// Skip a CSS comment starting at position `i` (which points to the '/').
// Returns the position just past the closing "*/", or std::string::npos if
// the comment is unterminated.
[[nodiscard]] std::size_t skip_css_comment(const std::string& css, std::size_t i) {
    auto end = css.find("*/", i + 2);

    if (end != std::string::npos) {
        return end + 2;
    }

    return std::string::npos; // Unterminated comment.
}

// Validate an at-rule starting at position `i` (which points to '@').
// If the at-rule is allowlisted, appends the '@' to `result` and advances
// by one character.  If unsafe, skips the entire rule block and appends nothing.
// Returns the new scan position.
[[nodiscard]] std::size_t validate_at_rule(const std::string& css, const std::string& lower,
                                           std::size_t i, std::string& result) {
    auto name_start = i + 1;
    auto name_end = name_start;

    while (name_end < lower.size() &&
           ((std::isalpha(static_cast<unsigned char>(lower[name_end])) != 0) ||
            lower[name_end] == '-')) {
        ++name_end;
    }

    auto name = std::string_view{lower.data() + name_start, name_end - name_start};

    if (safe_at_rules().contains(name)) {
        // Safe at-rule — pass through character by character.
        result += css[i];
        return i + 1;
    }

    // Unsafe at-rule — skip to end of rule block or semicolon.
    int brace_depth = 0;
    auto j = name_end;

    while (j < css.size()) {
        if (css[j] == '{') {
            ++brace_depth;
        } else if (css[j] == '}') {
            if (brace_depth <= 1) {
                j++;
                break;
            }

            --brace_depth;
        } else if (css[j] == ';' && brace_depth == 0) {
            j++;
            break;
        }

        ++j;
    }

    return j;
}

// Validate a url() function call.  `fn_start` is the start of the "url"
// identifier and `fn_end` points to the '(' character.  Appends the safe
// url() expression to `result` or drops it entirely.  Returns the new scan
// position, or std::string::npos if the url() is malformed (unterminated).
[[nodiscard]] std::size_t validate_url_function(const std::string& css, const std::string& lower,
                                                std::size_t fn_start, std::size_t fn_end,
                                                std::string& result) {
    auto close = css.find(')', fn_end + 1);

    if (close == std::string::npos) {
        return std::string::npos; // Malformed — drop rest.
    }

    auto url_body = lower.substr(fn_end + 1, close - (fn_end + 1));
    auto start_pos = url_body.find_first_not_of(" \t\n\r'\"");

    if (start_pos != std::string::npos) {
        auto trimmed = url_body.substr(start_pos);

        // Reject any body containing a backslash — CSS escape sequences like
        // \2F\2F decode to // in the browser after this sanitizer has already
        // passed them, enabling protocol-relative URL injection.
        const bool has_escape = trimmed.find('\\') != std::string::npos;

        // Allow only data: URIs, fragment refs, and relative paths.
        if (!has_escape &&
            (trimmed.starts_with("data:") || trimmed.starts_with("#") ||
             (trimmed.find(':') == std::string::npos && !trimmed.starts_with("//")))) {
            result.append(css, fn_start, close - fn_start + 1);
        }
        // else: drop the entire url() expression.
    }

    return close + 1;
}

// Validate a CSS function call or plain identifier starting at position `i`.
// Handles url() specially, passes through allowlisted functions, and strips
// unknown function calls.  Returns the new scan position, or std::string::npos
// if a malformed construct requires dropping the rest of the input.
[[nodiscard]] std::size_t validate_css_function(const std::string& css, const std::string& lower,
                                                std::size_t i, std::string& result) {
    auto fn_start = i;
    auto fn_end = fn_start;

    while (fn_end < lower.size() &&
           ((std::isalnum(static_cast<unsigned char>(lower[fn_end])) != 0) ||
            lower[fn_end] == '-' || lower[fn_end] == '_')) {
        ++fn_end;
    }

    if (fn_end < css.size() && css[fn_end] == '(') {
        auto fn_name = std::string_view{lower.data() + fn_start, fn_end - fn_start};

        // Special handling for url() — validate the URL scheme.
        if (fn_name == "url") {
            return validate_url_function(css, lower, fn_start, fn_end, result);
        }

        if (safe_functions().contains(fn_name)) {
            // Safe function — pass through the name and opening paren.
            result.append(css, fn_start, fn_end - fn_start + 1);
            return fn_end + 1;
        }

        // Unknown function — skip the entire function call including
        // its balanced parentheses.
        int paren_depth = 1;
        auto j = fn_end + 1;

        while (j < css.size() && paren_depth > 0) {
            if (css[j] == '(') {
                ++paren_depth;
            } else if (css[j] == ')') {
                --paren_depth;
            }

            ++j;
        }

        return j;
    }

    // Not a function call — pass through the identifier.
    result.append(css, fn_start, fn_end - fn_start);
    return fn_end;
}

} // anonymous namespace

// Allowlist-based CSS sanitisation for user-loaded stylesheets.  This
// tokenises the CSS and only passes through constructs that match known-safe
// patterns.  Unknown at-rules, CSS functions, and URL schemes are stripped.
[[nodiscard]] std::string sanitise_loaded_css(const std::string& css) {
    std::string result;
    result.reserve(css.size());

    // Lowercase copy for case-insensitive scanning.
    auto lower = luma::to_lower_copy(css);

    std::size_t i = 0;

    while (i < css.size()) {
        // ── Strip HTML tags entirely ──
        if (css[i] == '<') {
            i = skip_html_tag(css, i);

            if (i == std::string::npos) {
                break; // Unterminated tag — drop rest of input.
            }

            continue;
        }

        // ── Strip CSS comments ──
        if (i + 1 < css.size() && css[i] == '/' && css[i + 1] == '*') {
            i = skip_css_comment(css, i);

            if (i == std::string::npos) {
                break; // Unterminated comment — drop rest.
            }

            continue;
        }

        // ── Validate at-rules ──
        if (css[i] == '@') {
            i = validate_at_rule(css, lower, i, result);
            continue;
        }

        // ── Validate CSS function calls or pass through identifiers ──
        if ((std::isalpha(static_cast<unsigned char>(lower[i])) != 0) || lower[i] == '-') {
            i = validate_css_function(css, lower, i, result);

            if (i == std::string::npos) {
                break; // Malformed construct — drop rest.
            }

            continue;
        }

        // ── Default: pass through safe characters ──
        result += css[i];
        ++i;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════
// CSS security validation (GraphicalUi.stylesheet / load_stylesheet)
// ═══════════════════════════════════════════════════════════

// Substrings that are disallowed in inline CSS for security reasons.
// Each pattern is matched case-insensitively against the stylesheet text.
// These prevent script injection, data exfiltration, and sandbox escapes.
static constexpr std::string_view k_css_blocked_patterns[] = {
    "<script",       // HTML script injection
    "javascript:",   // JavaScript URL scheme
    "vbscript:",     // VBScript URL scheme (legacy IE)
    "expression(",   // IE CSS expression (script execution)
    "-moz-binding:", // Mozilla XBL binding (arbitrary code)
    "url(",          // url() can embed javascript: and data: schemes
    "@import",       // @import can load external resources
};

void validate_inline_css(const std::string& css, SourceLocation loc) {
    const auto lower = to_lower_copy(css);

    for (const auto pattern : k_css_blocked_patterns) {
        if (lower.find(pattern) != std::string::npos) {
            throw RuntimeError{
                error_msg("GraphicalUi", "stylesheet",
                          std::format("CSS contains disallowed content '{}'", pattern)),
                loc};
        }
    }
}

void validate_stylesheet_path(const std::string& path, SourceLocation loc) {
    // Validate extension.
    if (path.size() < 5 || path.substr(path.size() - 4) != ".css") {
        throw RuntimeError{"GraphicalUi.load_stylesheet: path must end in .css", loc};
    }

    // Reject absolute paths.
    if (!path.empty() &&
        (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'))) {
        throw RuntimeError{"GraphicalUi.load_stylesheet: absolute paths are not allowed; "
                           "use a path relative to the working directory",
                           loc};
    }

    // Reject URLs.
    if (path.find("://") != std::string::npos) {
        throw RuntimeError{"GraphicalUi.load_stylesheet: remote URLs are not allowed; "
                           "download the CSS file and reference it locally",
                           loc};
    }

    // Reject path traversal sequences.
    if (path.find("..") != std::string::npos) {
        throw RuntimeError{"GraphicalUi.load_stylesheet: path traversal ('..') is not allowed; "
                           "use a direct relative path within the working directory",
                           loc};
    }
}

// ═══════════════════════════════════════════════════════════
// Font-face validation (GraphicalUi.font_face)
// ═══════════════════════════════════════════════════════════

std::optional<FontFormat> font_format_for_path(const std::string& path) {
    // MIME type and CSS src format() token for each supported extension. .woff2
    // is listed before .woff so the longer suffix matches first.
    struct Entry {
        std::string_view ext;
        std::string_view mime;
        std::string_view format;
    };

    static constexpr Entry table[] = {
        {".woff2", "font/woff2", "woff2"},
        {".woff", "font/woff", "woff"},
        {".ttf", "font/ttf", "truetype"},
        {".otf", "font/otf", "opentype"},
    };

    const auto lower = to_lower_copy(path);

    for (const auto& entry : table) {
        if (lower.size() >= entry.ext.size() &&
            lower.compare(lower.size() - entry.ext.size(), entry.ext.size(), entry.ext) == 0) {
            return FontFormat{entry.mime, entry.format};
        }
    }

    return std::nullopt;
}

void validate_font_path(const std::string& path, SourceLocation loc) {
    // Validate extension against the supported font formats.
    if (!font_format_for_path(path).has_value()) {
        throw RuntimeError{"GraphicalUi.font_face: path must end in .woff2, .woff, .ttf, or .otf",
                           loc};
    }

    // Reject absolute paths.
    if (!path.empty() &&
        (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'))) {
        throw RuntimeError{"GraphicalUi.font_face: absolute paths are not allowed; "
                           "use a path relative to the working directory",
                           loc};
    }

    // Reject URLs.
    if (path.find("://") != std::string::npos) {
        throw RuntimeError{"GraphicalUi.font_face: remote URLs are not allowed; "
                           "download the font file and reference it locally",
                           loc};
    }

    // Reject path traversal sequences.
    if (path.find("..") != std::string::npos) {
        throw RuntimeError{"GraphicalUi.font_face: path traversal ('..') is not allowed; "
                           "use a direct relative path within the working directory",
                           loc};
    }
}

void validate_font_family(const std::string& family, SourceLocation loc) {
    if (family.empty() || family.size() > 64) {
        throw RuntimeError{"GraphicalUi.font_face: family must be 1 to 64 characters", loc};
    }

    // Restrict to a safe charset so the name can be embedded inside a quoted CSS
    // string without any possibility of breaking out of it.
    for (const char c : family) {
        const auto u = static_cast<unsigned char>(c);

        if (std::isalnum(u) == 0 && c != ' ' && c != '-' && c != '_') {
            throw RuntimeError{"GraphicalUi.font_face: family may contain only letters, digits, "
                               "spaces, hyphens, and underscores",
                               loc};
        }
    }
}

void validate_font_weight(const std::string& weight, SourceLocation loc) {
    if (weight == "normal" || weight == "bold" || weight == "bolder" || weight == "lighter") {
        return;
    }

    // Otherwise accept a numeric weight: digits and spaces only, with at least
    // one digit.  Spaces permit a variable-font range such as "100 900".
    bool has_digit = false;

    for (const char c : weight) {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            has_digit = true;
        } else if (c != ' ') {
            has_digit = false;
            break;
        }
    }

    if (!has_digit) {
        throw RuntimeError{"GraphicalUi.font_face: weight must be a keyword "
                           "(normal/bold/bolder/lighter) or a numeric value like \"400\" or "
                           "\"100 900\"",
                           loc};
    }
}

void validate_font_style(const std::string& style, SourceLocation loc) {
    if (style != "normal" && style != "italic" && style != "oblique") {
        throw RuntimeError{"GraphicalUi.font_face: style must be normal, italic, or oblique", loc};
    }
}

} // namespace luma::gui_detail
