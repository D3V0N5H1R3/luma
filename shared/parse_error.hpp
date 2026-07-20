#ifndef LUMA_PARSE_ERROR_HPP
#define LUMA_PARSE_ERROR_HPP

#include <cstdint>
#include <stdexcept>
#include <string>

namespace luma {

/// Base class for all parsing-related errors in the Luma shared layer.
/// Both transport header parsing (protocol::ParseError) and JSON body
/// parsing (JsonParseError) derive from this, allowing callers to catch
/// a single type for all parse failures.
class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;

    /// Character/byte offset where the error occurred (-1 if unknown).
    [[nodiscard]] std::ptrdiff_t offset() const noexcept {
        return offset_;
    }

protected:
    explicit ParseError(const std::string& msg, std::ptrdiff_t offset)
        : std::runtime_error(msg), offset_(offset) {}

private:
    std::ptrdiff_t offset_ = -1;
};

/// JSON syntax error thrown by the JSON parser.
/// Carries the byte position within the JSON text where the error was detected.
class JsonParseError : public ParseError {
public:
    explicit JsonParseError(std::size_t position, const std::string& message)
        : ParseError("JSON parse error at position " + std::to_string(position) + ": " + message,
                     static_cast<std::ptrdiff_t>(position)) {}

    /// Byte offset within the JSON input where the error was detected.
    /// Derived from the base-class offset(), which the constructor seeds with
    /// the (always non-negative) position.
    [[nodiscard]] std::size_t position() const noexcept {
        return static_cast<std::size_t>(offset());
    }
};

} // namespace luma

#endif // LUMA_PARSE_ERROR_HPP
