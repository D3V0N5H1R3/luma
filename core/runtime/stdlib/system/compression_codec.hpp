#ifndef LUMA_STDLIB_COMPRESSION_CODEC_HPP
#define LUMA_STDLIB_COMPRESSION_CODEC_HPP

#include <optional>
#include <string>

// Pure compression/decompression codec layer, free of any Value or environment
// dependencies.  The functions here are the trust-boundary parsers that decode
// untrusted bytes (inflate/gunzip/decode_rle) together with their matching
// encoders.  They are exposed in this header — rather than kept file-local —
// so the fuzz target (fuzz/fuzz_compression.cpp) can drive them directly,
// mirroring how the JSON parser and bytecode deserializer are fuzzed.
//
// Encoders return a plain string (they cannot fail for in-range input apart
// from the resource-limit guard in rle_encode, which throws RuntimeError).
// Decoders return std::nullopt for malformed input and never throw.

namespace luma::compression {

// miniz accepts deflate compression levels 1..9.  deflate_compress clamps a
// caller-supplied level to this range, and the Compression.*_with builtins
// reject any level outside it.  Shared here so the codec and its stdlib
// registration agree on a single source of truth.
inline constexpr int k_min_deflate_level = 1;
inline constexpr int k_max_deflate_level = 9;

// === Run-Length Encoding ===

[[nodiscard]] std::string rle_encode(const std::string& input);

[[nodiscard]] std::optional<std::string> rle_decode(const std::string& input);

// === Raw deflate (RFC 1951, no zlib wrapper) ===

[[nodiscard]] std::string deflate_compress(const std::string& input,
                                           std::optional<int> level = std::nullopt);

[[nodiscard]] std::optional<std::string> deflate_decompress(const std::string& input);

// === Gzip wrapper (RFC 1952) ===

[[nodiscard]] std::string gzip_compress(const std::string& input,
                                        std::optional<int> level = std::nullopt);

[[nodiscard]] std::optional<std::string> gzip_decompress(const std::string& input);

} // namespace luma::compression

#endif // LUMA_STDLIB_COMPRESSION_CODEC_HPP
