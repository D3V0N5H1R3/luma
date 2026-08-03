// Pure compression/decompression codec implementation.
//
// Free of any Value or environment dependencies (see compression_codec.hpp):
// the functions here decode untrusted bytes and produce their matching
// encodings, and are exposed so fuzz/fuzz_compression.cpp can drive them
// directly.  The Luma-facing Compression module registration lives separately
// in compression_module.cpp, mirroring the json parser/serializer split.

#include "runtime/stdlib/system/compression_codec.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/byte_utils.hpp"
#include "common/crc32.hpp"
#include "common/resource_limits.hpp"
#include "miniz.h"

namespace luma::compression {

namespace {

// The RLE format stores each run count as a single decimal digit, so a run is
// emitted in chunks of at most nine identical bytes.
constexpr std::size_t k_max_rle_run_chunk = 9;

// Gzip framing sizes (RFC 1952): a fixed 10-byte header and an 8-byte trailer
// (CRC32 + ISIZE).  The smallest valid gzip stream is header + trailer around
// an empty deflate body.
constexpr std::size_t k_gzip_header_size = 10;
constexpr std::size_t k_gzip_trailer_size = 8;
constexpr std::size_t k_min_gzip_size = k_gzip_header_size + k_gzip_trailer_size;

// Inflate output is drained in fixed 64 KiB chunks.
constexpr std::size_t k_inflate_scratch_size = std::size_t{64} * 1024;

// Gzip magic bytes and fixed header-field values (RFC 1952 §2.3).
constexpr std::uint8_t k_gzip_id1 = 0x1f;
constexpr std::uint8_t k_gzip_id2 = 0x8b;
constexpr std::uint8_t k_gzip_cm_deflate = 0x08;

// Gzip FLG bit flags (RFC 1952 §2.3.1).  Only the optional-field flags the
// decoder skips over are named here.
constexpr std::uint8_t k_gzip_flag_fhcrc = 0x02;
constexpr std::uint8_t k_gzip_flag_fextra = 0x04;
constexpr std::uint8_t k_gzip_flag_fname = 0x08;
constexpr std::uint8_t k_gzip_flag_fcomment = 0x10;

// === Gzip helpers ===

void append_gzip_header(std::string& out) {
    out += static_cast<char>(k_gzip_id1);        // ID1
    out += static_cast<char>(k_gzip_id2);        // ID2
    out += static_cast<char>(k_gzip_cm_deflate); // CM (deflate)
    out += '\x00';                               // FLG
    out += '\x00';                               // MTIME (4 bytes, all zero)
    out += '\x00';
    out += '\x00';
    out += '\x00';
    out += '\x00'; // XFL
    out += '\xff'; // OS (unknown)
}

} // anonymous namespace

// === RLE (Run-Length Encoding) ===

std::string rle_encode(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    const auto max_size = ResourceLimits::max_string_size;
    std::string out{};
    out.reserve(input.size());

    std::size_t i{0};

    while (i < input.size()) {
        const char ch = input[i];

        std::size_t run{1};

        while (i + run < input.size() && input[i + run] == ch) {
            ++run;
        }

        // Emit single-digit counts (1-9) so the format is unambiguous
        // even when the character itself is a digit.
        std::size_t remaining{run};

        while (remaining > 0) {
            const auto chunk = std::min<std::size_t>(remaining, k_max_rle_run_chunk);
            out += static_cast<char>('0' + chunk);
            out += ch;
            remaining -= chunk;

            if (out.size() > max_size) {
                throw RuntimeError{
                    "Compression.encode_rle: RLE encoded output exceeds maximum string size",
                    SourceLocation{}, "reduce the input size"};
            }
        }

        i += run;
    }

    return out;
}

std::optional<std::string> rle_decode(const std::string& input) {
    auto result = rle_decode_checked(input);

    if (result.is_ok()) {
        return std::move(result).value();
    }

    return std::nullopt;
}

DecodeResult rle_decode_checked(const std::string& input) {
    const auto max_size = ResourceLimits::max_string_size;
    std::string out{};
    std::size_t i{0};

    while (i < input.size()) {
        // Each entry is exactly one digit (1-9) followed by one character.
        if (input[i] < '1' || input[i] > '9') {
            return DecodeResult::err(DecodeError::Corrupt); // invalid count digit
        }

        const auto count = static_cast<std::size_t>(input[i] - '0');
        ++i;

        if (i >= input.size()) {
            return DecodeResult::err(DecodeError::Truncated); // missing character after count
        }

        if (out.size() + count > max_size) {
            return DecodeResult::err(DecodeError::TooLarge);
        }

        out.append(count, input[i]);

        ++i;
    }

    return DecodeResult::ok(std::move(out));
}

// === Deflate / Inflate via miniz ===

namespace {

// Shared deflate compressor.  A negative `window_bits` selects raw deflate
// (RFC 1951, no wrapper); a positive value selects the zlib wrapper (RFC 1950,
// 2-byte header + Adler-32 trailer).
[[nodiscard]] std::string deflate_compress_windowed(const std::string& input,
                                                    std::optional<int> level, int window_bits) {
    if (input.empty()) {
        return {};
    }

    int compression_level = level.value_or(MZ_DEFAULT_COMPRESSION);
    if (level.has_value()) {
        compression_level = std::clamp(compression_level, k_min_deflate_level, k_max_deflate_level);
    }

    const mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(input.size()));
    std::vector<uint8_t> buf(bound);

    mz_stream stream{};
    stream.next_in = reinterpret_cast<const unsigned char*>(input.data());
    stream.avail_in = static_cast<mz_uint32>(input.size());
    stream.next_out = buf.data();
    stream.avail_out = static_cast<mz_uint32>(buf.size());

    if (mz_deflateInit2(&stream, compression_level, MZ_DEFLATED, window_bits, 9,
                        MZ_DEFAULT_STRATEGY) != MZ_OK) {
        return {};
    }

    const int status = mz_deflate(&stream, MZ_FINISH);
    mz_deflateEnd(&stream);

    if (status != MZ_STREAM_END) {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(buf.data()), stream.total_out);
}

// Shared inflate decompressor.  `window_bits` mirrors deflate_compress_windowed:
// negative for raw deflate, positive for the zlib wrapper.
[[nodiscard]] DecodeResult inflate_windowed(const std::string& input, int window_bits) {
    if (input.empty()) {
        return DecodeResult::ok(std::string{});
    }

    const auto max_size = ResourceLimits::max_string_size;

    mz_stream stream{};
    stream.next_in = reinterpret_cast<const unsigned char*>(input.data());
    stream.avail_in = static_cast<mz_uint32>(input.size());

    if (mz_inflateInit2(&stream, window_bits) != MZ_OK) {
        return DecodeResult::err(DecodeError::Corrupt);
    }

    // Stream the output in fixed chunks, appending as we go.  MZ_NO_FLUSH is
    // used deliberately: MZ_FINISH on the first call tells miniz the entire
    // output fits in the supplied buffer and permanently fails the stream when
    // it does not, which makes a grow-and-retry loop impossible.  The decoder
    // terminates on the deflate stream's final-block marker (MZ_STREAM_END).
    std::string out{};
    std::vector<unsigned char> scratch(k_inflate_scratch_size);

    mz_uint64 last_total_io{0};

    for (;;) {
        stream.next_out = scratch.data();
        stream.avail_out = static_cast<mz_uint32>(scratch.size());

        const int status = mz_inflate(&stream, MZ_NO_FLUSH);

        const std::size_t produced = scratch.size() - stream.avail_out;

        if (produced > 0) {
            if (out.size() + produced > max_size) {
                mz_inflateEnd(&stream);
                return DecodeResult::err(DecodeError::TooLarge);
            }

            out.append(reinterpret_cast<const char*>(scratch.data()), produced);
        }

        if (status == MZ_STREAM_END) {
            break;
        }

        if (status != MZ_OK) {
            mz_inflateEnd(&stream);

            // MZ_BUF_ERROR means miniz ran out of input mid-stream (a premature
            // end), which is a truncation; any other non-OK status is a genuine
            // data error in the compressed bytes.
            const DecodeError kind =
                (status == MZ_BUF_ERROR) ? DecodeError::Truncated : DecodeError::Corrupt;

            return DecodeResult::err(kind);
        }

        // Guard against a stalled stream (no input consumed and no output
        // produced) so malformed data can never spin forever.  A stall means
        // the stream needs more input it does not have — a truncated stream.
        const mz_uint64 total_io =
            static_cast<mz_uint64>(stream.total_in) + static_cast<mz_uint64>(stream.total_out);

        if (total_io == last_total_io) {
            mz_inflateEnd(&stream);
            return DecodeResult::err(DecodeError::Truncated);
        }

        last_total_io = total_io;
    }

    mz_inflateEnd(&stream);

    return DecodeResult::ok(std::move(out));
}

} // namespace

std::string deflate_compress(const std::string& input, std::optional<int> level) {
    // Negative window bits = raw deflate (RFC 1951, no zlib wrapper).
    return deflate_compress_windowed(input, level, -MZ_DEFAULT_WINDOW_BITS);
}

std::optional<std::string> deflate_decompress(const std::string& input) {
    auto result = deflate_decompress_checked(input);

    if (result.is_ok()) {
        return std::move(result).value();
    }

    return std::nullopt;
}

DecodeResult deflate_decompress_checked(const std::string& input) {
    // Negative window bits = raw deflate (no zlib header).
    return inflate_windowed(input, -MZ_DEFAULT_WINDOW_BITS);
}

// === Zlib wrapper (RFC 1950: 2-byte header + Adler-32 trailer) ===

std::string zlib_compress(const std::string& input, std::optional<int> level) {
    // Positive window bits = zlib-wrapped deflate.
    return deflate_compress_windowed(input, level, MZ_DEFAULT_WINDOW_BITS);
}

std::optional<std::string> zlib_decompress(const std::string& input) {
    auto result = zlib_decompress_checked(input);

    if (result.is_ok()) {
        return std::move(result).value();
    }

    return std::nullopt;
}

DecodeResult zlib_decompress_checked(const std::string& input) {
    // Positive window bits = zlib-wrapped deflate.
    return inflate_windowed(input, MZ_DEFAULT_WINDOW_BITS);
}

// === Gzip wrapper (RFC 1952) ===

std::string gzip_compress(const std::string& input, std::optional<int> level) {
    // Compressed body (raw deflate) framed by the gzip header and trailer.
    const std::string deflated = deflate_compress(input, level);

    std::string out{};
    out.reserve(k_gzip_header_size + deflated.size() + k_gzip_trailer_size);

    append_gzip_header(out);
    out += deflated;

    // CRC32 and ISIZE as 4-byte little-endian values.
    write_u32_le(out, crc32_hash(input));
    write_u32_le(out, static_cast<std::uint32_t>(input.size()));

    return out;
}

std::optional<std::string> gzip_decompress(const std::string& input) {
    auto result = gzip_decompress_checked(input);

    if (result.is_ok()) {
        return std::move(result).value();
    }

    return std::nullopt;
}

DecodeResult gzip_decompress_checked(const std::string& input) {
    if (input.size() < k_min_gzip_size) {
        return DecodeResult::err(DecodeError::Truncated);
    }

    // Verify gzip magic.
    if (static_cast<std::uint8_t>(input[0]) != k_gzip_id1 ||
        static_cast<std::uint8_t>(input[1]) != k_gzip_id2) {
        return DecodeResult::err(DecodeError::UnsupportedFormat);
    }

    // Only the deflate compression method (CM = 8) is defined by RFC 1952.
    if (static_cast<std::uint8_t>(input[2]) != k_gzip_cm_deflate) {
        return DecodeResult::err(DecodeError::UnsupportedFormat);
    }

    // Skip header (minimum k_gzip_header_size bytes).
    std::size_t offset{k_gzip_header_size};

    const auto flags = static_cast<uint8_t>(input[3]);

    // FEXTRA.
    if ((flags & k_gzip_flag_fextra) != 0) {
        if (offset + 2 > input.size()) {
            return DecodeResult::err(DecodeError::Truncated);
        }

        const auto xlen = static_cast<uint16_t>(static_cast<uint8_t>(input[offset])) |
                          (static_cast<uint16_t>(static_cast<uint8_t>(input[offset + 1])) << 8);

        // Validate the untrusted FEXTRA length before trusting it to advance offset.
        if (offset + 2 + static_cast<std::size_t>(xlen) > input.size()) {
            return DecodeResult::err(DecodeError::Truncated);
        }

        offset += 2 + static_cast<std::size_t>(xlen);
    }

    // FNAME.
    if ((flags & k_gzip_flag_fname) != 0) {
        while (offset < input.size() && input[offset] != '\0') {
            ++offset;
        }

        ++offset;
    }

    // FCOMMENT.
    if ((flags & k_gzip_flag_fcomment) != 0) {
        while (offset < input.size() && input[offset] != '\0') {
            ++offset;
        }

        ++offset;
    }

    // FHCRC.
    if ((flags & k_gzip_flag_fhcrc) != 0) {
        offset += 2;
    }

    if (offset + k_gzip_trailer_size > input.size()) {
        return DecodeResult::err(DecodeError::Truncated);
    }

    // Compressed data is between offset and input.size() - k_gzip_trailer_size.
    const auto compressed = input.substr(offset, input.size() - offset - k_gzip_trailer_size);

    auto result = deflate_decompress_checked(compressed);

    if (result.is_err()) {
        return result; // propagate the classified deflate failure
    }

    const std::string& decompressed = result.value();

    // Verify CRC32 and ISIZE from the gzip trailer (last k_gzip_trailer_size
    // bytes): CRC32 first, then the original input size, both little-endian.
    const auto trailer_start = input.size() - k_gzip_trailer_size;
    const auto* trailer = reinterpret_cast<const std::uint8_t*>(input.data());
    const std::uint32_t expected_crc = read_u32_le(trailer + trailer_start);
    const std::uint32_t expected_isize = read_u32_le(trailer + trailer_start + 4);

    if (crc32_hash(decompressed) != expected_crc) {
        return DecodeResult::err(DecodeError::Corrupt); // CRC32 mismatch
    }

    if (static_cast<std::uint32_t>(decompressed.size()) != expected_isize) {
        return DecodeResult::err(DecodeError::Corrupt); // size mismatch
    }

    return result;
}

} // namespace luma::compression
