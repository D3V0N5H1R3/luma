#ifndef LUMA_STDLIB_COMPRESSION_CODEC_HPP
#define LUMA_STDLIB_COMPRESSION_CODEC_HPP

#include <optional>
#include <string>

#include "common/result.hpp"

// Pure compression/decompression codec layer, free of any Value or environment
// dependencies.  The functions here are the trust-boundary parsers that decode
// untrusted bytes (inflate/gunzip/decode_rle) together with their matching
// encoders.  They are exposed in this header — rather than kept file-local —
// so the fuzz target (fuzz/fuzz_compression.cpp) can drive them directly,
// mirroring how the JSON parser and bytecode deserializer are fuzzed.
//
// Encoders return a plain string (they cannot fail for in-range input apart
// from the resource-limit guard in rle_encode, which throws RuntimeError).
// Decoders come in two forms: the std::optional-returning functions
// (rle_decode / deflate_decompress / gzip_decompress) collapse every failure to
// std::nullopt for the string-error stdlib functions, while the *_checked
// counterparts classify the failure into a DecodeError kind for the opt-in
// typed-error slice (Compression.decompress_typed / inflate_typed / gunzip_typed
// → result<string, Compression.Error>).  The optional forms delegate to the
// checked forms, so there is a single source of truth for the decode logic.
// Neither form ever throws.

namespace luma::compression {

// Classified decompression failure, surfaced by the *_checked decoders and
// mapped 1:1 onto the Compression.Error choice in
// core/analysis/types/stdlib_type_arities.cpp:
//   Corrupt           — well-framed but internally inconsistent data (bad
//                        deflate codes, CRC/size trailer mismatch, invalid RLE
//                        count digit).
//   Truncated         — the stream ends before a complete unit was decoded
//                        (missing bytes / premature end of input).
//   UnsupportedFormat — the container is not the expected format (bad gzip
//                        magic, or a compression method other than deflate).
//   TooLarge          — the decoded output would exceed the interpreter's
//                        maximum string size.
enum class DecodeError {
    Corrupt,
    Truncated,
    UnsupportedFormat,
    TooLarge
};

// A decoded string on success, or a classified DecodeError on failure.
using DecodeResult = Result<std::string, DecodeError>;

// miniz accepts deflate compression levels 1..9.  deflate_compress clamps a
// caller-supplied level to this range, and the Compression.*_with builtins
// reject any level outside it.  Shared here so the codec and its stdlib
// registration agree on a single source of truth.
inline constexpr int k_min_deflate_level = 1;
inline constexpr int k_max_deflate_level = 9;

// === Run-Length Encoding ===

[[nodiscard]] std::string rle_encode(const std::string& input);

[[nodiscard]] std::optional<std::string> rle_decode(const std::string& input);

[[nodiscard]] DecodeResult rle_decode_checked(const std::string& input);

// === Raw deflate (RFC 1951, no zlib wrapper) ===

[[nodiscard]] std::string deflate_compress(const std::string& input,
                                           std::optional<int> level = std::nullopt);

[[nodiscard]] std::optional<std::string> deflate_decompress(const std::string& input);

[[nodiscard]] DecodeResult deflate_decompress_checked(const std::string& input);

// === Gzip wrapper (RFC 1952) ===

[[nodiscard]] std::string gzip_compress(const std::string& input,
                                        std::optional<int> level = std::nullopt);

[[nodiscard]] std::optional<std::string> gzip_decompress(const std::string& input);

[[nodiscard]] DecodeResult gzip_decompress_checked(const std::string& input);

} // namespace luma::compression

#endif // LUMA_STDLIB_COMPRESSION_CODEC_HPP
