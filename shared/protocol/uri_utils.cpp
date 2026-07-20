#include "protocol/uri_utils.hpp"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "common/hex_codec.hpp"

namespace luma::protocol {

namespace {

// The scheme prefix shared by every file URI ("file://…").
constexpr std::string_view k_file_uri_prefix{"file://"};

// Returns true if `text`, starting at `pos`, begins with a Windows drive
// prefix: an ASCII letter followed by ':' (e.g. "C:", "d:").
[[nodiscard]] bool has_drive_prefix(std::string_view text, std::size_t pos = 0) {
    return pos + 1 < text.size() && (std::isalpha(static_cast<unsigned char>(text[pos])) != 0) &&
           text[pos + 1] == ':';
}

// On Windows the URI path begins with an extra "/" before the drive letter.
// Remove it: "/C:/..." → "C:/..."
void normalize_windows_path(std::string& path) {
    if (path.starts_with('/') && has_drive_prefix(path, 1)) {
        path.erase(0, 1);
    }
}

} // namespace

// Decode all RFC 3986 percent-encoded (%XX) sequences in a URI component.
// Null bytes are silently dropped to prevent path validation bypass.
std::string percent_decode(std::string_view encoded) {
    std::string result;
    result.reserve(encoded.size());
    for (std::size_t i{0}; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            const int hv = luma::from_hex_digit(static_cast<char>(encoded[i + 1]));
            const int lv = luma::from_hex_digit(static_cast<char>(encoded[i + 2]));
            if (hv >= 0 && lv >= 0) {
                const auto decoded = static_cast<char>((hv * 16) + lv);

                // Reject null bytes — they can bypass path validation.
                if (decoded != '\0') {
                    result += decoded;
                }

                i += 2;
                continue;
            }
        }
        result += encoded[i];
    }
    return result;
}

// Percent-encode a string for use in a URI path component.
// Characters in the URI unreserved set (RFC 3986: A-Z a-z 0-9 - . _ ~)
// and ':' and '/' (path separators) are left as-is; all others are encoded.
std::string percent_encode_path(std::string_view path) {
    std::string result;
    // Most characters pass through unencoded, so reserve for the common case
    // (1:1) rather than the 3x worst case; the rare encoded byte grows the
    // buffer geometrically.
    result.reserve(path.size());
    for (const char c : path) {
        if (c == '\\' || c == '/') {
            result += '/';
        } else if (c == ':') {
            result += ':';
        } else {
            const auto uc = static_cast<unsigned char>(c);
            if ((std::isalnum(uc) != 0) || c == '-' || c == '.' || c == '_' || c == '~') {
                result += c;
            } else {
                result += '%';
                result += luma::to_hex_digit_upper((uc >> 4) & 0x0F);
                result += luma::to_hex_digit_upper(uc & 0x0F);
            }
        }
    }
    return result;
}

// Convert a file:// URI to a local filesystem path.
// Full RFC 3986 percent-decoding for all %XX sequences.
// Returns std::nullopt when the URI scheme is not "file://" — callers
// must handle the missing value (e.g., skip the operation or log a warning).
std::optional<std::string> uri_to_path(const std::string& uri) {
    if (!uri.starts_with(k_file_uri_prefix)) {
        // Reject non-file URIs — returning raw URIs as filesystem paths
        // could lead to unintended file access or confusing behavior.
        return std::nullopt;
    }

    std::string result = percent_decode(std::string_view{uri}.substr(k_file_uri_prefix.size()));

    normalize_windows_path(result);

    // Normalize the decoded path to resolve ".." segments that could
    // have been hidden behind percent-encoding (e.g., %2E%2E).
    // weakly_canonical collapses ".." without requiring the path to exist.
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(result, ec);
    if (!ec) {
        result = normalized.string();
    }

    return result;
}

// Convert a local filesystem path to a file:// URI.
// Percent-encodes characters that are not valid in URIs.
std::string path_to_uri(const std::string& path) {
    std::string uri{k_file_uri_prefix};

    // On Windows, prepend an extra "/" before the drive letter.
    if (has_drive_prefix(path)) {
        uri += '/';
    }

    uri += percent_encode_path(path);
    return uri;
}

// Canonicalize a URI for use as a cache key: lowercase drive letter on Windows.
std::string canonicalize_uri(const std::string& uri) {
    std::string result = uri;

    // A Windows file URI carries an extra '/' before the drive letter
    // ("file:///C:/…"), so the drive sits one byte past the "file://" prefix.
    // Normalize its case so the URI can serve as a stable cache key.
    constexpr std::size_t drive_letter_index = k_file_uri_prefix.size() + 1;
    if (result.starts_with(k_file_uri_prefix) && result[k_file_uri_prefix.size()] == '/' &&
        has_drive_prefix(result, drive_letter_index)) {
        result[drive_letter_index] =
            static_cast<char>(std::tolower(static_cast<unsigned char>(result[drive_letter_index])));
    }

    return result;
}

} // namespace luma::protocol
