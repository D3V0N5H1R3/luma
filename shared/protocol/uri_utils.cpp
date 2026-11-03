#include "protocol/uri_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "common/hex_codec.hpp"
#include "common/path_utils.hpp"
#include "common/string_utils.hpp"

namespace luma::protocol {

namespace {

// The scheme prefix shared by every file URI ("file://…").
constexpr std::string_view k_file_uri_prefix{"file://"};

// The authority that, like an empty one, names the local machine (RFC 8089).
constexpr std::string_view k_localhost{"localhost"};

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

    // RFC 8089: "file://<authority><path>".  An empty or "localhost"
    // authority names the local machine; any other host is a UNC share
    // ("file://server/share/x" → "//server/share/x").  Some tools emit the
    // non-standard "file://C:/x", where the "authority" is really a drive
    // letter — that is a local path, not a server named "C:".
    const auto rest = std::string_view{uri}.substr(k_file_uri_prefix.size());
    const auto path_start = std::min(rest.find('/'), rest.size());
    const auto authority = rest.substr(0, path_start);
    const auto decoded_authority = percent_decode(authority);

    const bool is_drive_authority =
        decoded_authority.size() == 2 && has_drive_prefix(decoded_authority);
    const bool is_local = authority.empty() || is_drive_authority ||
                          luma::case_insensitive_equal(authority, k_localhost);
    std::string result;

    if (is_drive_authority) {
        result = percent_decode(rest);
    } else if (is_local) {
        result = percent_decode(rest.substr(path_start));
        normalize_windows_path(result);
    } else {
        result = "//" + percent_decode(rest);
    }

    // Normalize the decoded path to resolve ".." segments that could
    // have been hidden behind percent-encoding (e.g., %2E%2E).  The decoded
    // text is UTF-8, so it must go through the UTF-8 path helpers: on MSVC a
    // plain std::string path is read in the ANSI code page.
    if (const auto fs_path = luma::try_path_from_utf8(result)) {
        if (!is_local) {
            // Canonicalizing a UNC path would query the network on every call.
            result = luma::path_to_utf8(fs_path->lexically_normal());
        } else {
            // weakly_canonical collapses ".." without requiring the path to exist.
            std::error_code ec;
            const auto normalized = std::filesystem::weakly_canonical(*fs_path, ec);
            if (!ec) {
                result = luma::path_to_utf8(normalized);
            }
        }
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

// Canonicalize a URI for use as a cache key: on a Windows drive URI, decode a
// percent-encoded drive colon and lowercase the drive letter.  Editors spell
// the same file differently — VS Code sends "file:///c%3A/x" while
// path_to_uri() produces "file:///C:/x" — and both must map to one key.
std::string canonicalize_uri(const std::string& uri) {
    // A Windows file URI carries an extra '/' before the drive letter
    // ("file:///C:/…"), so the drive sits one byte past the "file://" prefix.
    constexpr std::size_t drive_letter_index = k_file_uri_prefix.size() + 1;
    constexpr std::size_t colon_index = drive_letter_index + 1;
    constexpr std::string_view k_encoded_colon{"%3A"};

    if (!uri.starts_with(k_file_uri_prefix) || uri.size() <= colon_index ||
        uri[k_file_uri_prefix.size()] != '/' ||
        std::isalpha(static_cast<unsigned char>(uri[drive_letter_index])) == 0) {
        return uri;
    }

    const auto after_drive = std::string_view{uri}.substr(colon_index);
    std::size_t rest_index{0};

    if (after_drive.starts_with(':')) {
        rest_index = colon_index + 1;
    } else if (luma::case_insensitive_equal(after_drive.substr(0, k_encoded_colon.size()),
                                            k_encoded_colon)) {
        rest_index = colon_index + k_encoded_colon.size();
    } else {
        return uri;
    }

    std::string result{uri.substr(0, drive_letter_index)};
    result += static_cast<char>(std::tolower(static_cast<unsigned char>(uri[drive_letter_index])));
    result += ':';
    result.append(uri, rest_index);

    return result;
}

} // namespace luma::protocol
