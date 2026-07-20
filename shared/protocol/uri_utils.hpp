#ifndef LUMA_PROTOCOL_URI_UTILS_HPP
#define LUMA_PROTOCOL_URI_UTILS_HPP

#include <optional>
#include <string>
#include <string_view>

namespace luma::protocol {

// Decode all RFC 3986 percent-encoded (%XX) sequences in a URI component.
// Null bytes are silently dropped to prevent path validation bypass.
[[nodiscard]] std::string percent_decode(std::string_view encoded);

// Percent-encode a string for use in a URI path component.
// Characters in the URI unreserved set (RFC 3986: A-Z a-z 0-9 - . _ ~)
// and ':' and '/' (path separators) are left as-is; all others are encoded.
[[nodiscard]] std::string percent_encode_path(std::string_view path);

// Convert a file:// URI to a local filesystem path.
// Full RFC 3986 percent-decoding for all %XX sequences.
// Returns std::nullopt for non-file:// URIs — callers must handle the
// missing value rather than silently operating on an empty path.
[[nodiscard]] std::optional<std::string> uri_to_path(const std::string& uri);

// Convert a local filesystem path to a file:// URI.
// Percent-encodes characters that are not valid in URIs.
[[nodiscard]] std::string path_to_uri(const std::string& path);

// Canonicalize a URI for use as a cache key: lowercase drive letter on Windows.
[[nodiscard]] std::string canonicalize_uri(const std::string& uri);

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_URI_UTILS_HPP
