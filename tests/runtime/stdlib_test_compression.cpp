// Standard library tests: Compression.

#include <string>

#include "runtime/stdlib/system/compression_codec.hpp"
#include "stdlib_test_helpers.hpp"

static void test_compression_compressed_size() {
    const auto v = eval(R"(Compression.compressed_size("hello world"))");

    ASSERT_TRUE(v.is_integer());
    ASSERT_TRUE(v.as_integer() > 0);
}

static void test_compression_deflate_inflate() {
    const auto v = eval(R"(
        "hello world"
        |> Compression.deflate()
        |> Compression.inflate()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello world");
}

static void test_compression_gzip_gunzip() {
    const auto v = eval(R"(
        "test data 12345"
        |> Compression.gzip()
        |> Compression.gunzip()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "test data 12345");
}

static void test_compression_inflate_malformed() {
    ASSERT_EVAL_FAILURE(R"(Compression.inflate("not valid deflate"))");
}

static void test_compression_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Compression.deflate"));
    ASSERT_TRUE(env->has("Compression.inflate"));
    ASSERT_TRUE(env->has("Compression.gzip"));
    ASSERT_TRUE(env->has("Compression.gunzip"));
    ASSERT_TRUE(env->has("Compression.encode_rle"));
    ASSERT_TRUE(env->has("Compression.decode_rle"));
    ASSERT_TRUE(env->has("Compression.gzip_file"));
    ASSERT_TRUE(env->has("Compression.gunzip_file"));
    ASSERT_TRUE(env->has("Compression.compressed_size"));
}

static void test_compression_rle_decode() {
    const auto v = eval(R"(Compression.decode_rle("3a3b2c") |> Result.unwrap())");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "aaabbbcc");
}

static void test_compression_rle_encode() {
    const auto v = eval(R"(Compression.encode_rle("aaabbbcc"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "3a3b2c");
}

static void test_compression_rle_roundtrip() {
    const auto v = eval(R"(
        Compression.encode_rle("aabbcc")
        |> Compression.decode_rle()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "aabbcc");
}

static void test_compression_deflate_with_level() {
    const auto v = eval(R"(
        "hello world"
        |> Compression.deflate_with(1)
        |> Result.unwrap()
        |> Compression.inflate()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello world");
}

static void test_compression_deflate_with_max_level() {
    const auto v = eval(R"(
        "hello world"
        |> Compression.deflate_with(9)
        |> Result.unwrap()
        |> Compression.inflate()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello world");
}

static void test_compression_gzip_with_level() {
    const auto v = eval(R"(
        "test data 12345"
        |> Compression.gzip_with(5)
        |> Result.unwrap()
        |> Compression.gunzip()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "test data 12345");
}

static void test_compression_deflate_with_invalid_level() {
    ASSERT_EVAL_FAILURE(R"(Compression.deflate_with("hello", 0))");
}

static void test_compression_gzip_with_invalid_level() {
    ASSERT_EVAL_FAILURE(R"(Compression.gzip_with("hello", 10))");
}

// ─────────────────────────────────────────────────────────────────────
// Additional VM-level (eval) coverage: empty input, RLE chunking and
// digit-safety, malformed decoder input, and level-bound validation.
// ─────────────────────────────────────────────────────────────────────

static void test_compression_deflate_empty() {
    const auto v = eval(R"(Compression.deflate(""))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "");
}

static void test_compression_gzip_empty_roundtrip() {
    const auto v = eval(R"(
        ""
        |> Compression.gzip()
        |> Compression.gunzip()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "");
}

static void test_compression_rle_encode_chunking() {
    // A run of 12 identical bytes must be emitted as single-digit chunks.
    const auto v = eval(R"(Compression.encode_rle("aaaaaaaaaaaa"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "9a3a");
}

static void test_compression_rle_digit_safety() {
    // Digit characters in the payload must round-trip unambiguously.
    const auto v = eval(R"(
        "11122"
        |> Compression.encode_rle()
        |> Compression.decode_rle()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "11122");
}

static void test_compression_compressed_size_smaller_than_input() {
    const auto v = eval(
        R"(Compression.compressed_size("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") < 50)");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_compression_gunzip_malformed() {
    ASSERT_EVAL_FAILURE(R"(Compression.gunzip("not a valid gzip stream!!"))");
}

static void test_compression_decode_rle_malformed() {
    ASSERT_EVAL_FAILURE(R"(Compression.decode_rle("0a"))");
}

static void test_compression_gzip_with_min_level() {
    const auto v = eval(R"(
        "test data 12345"
        |> Compression.gzip_with(1)
        |> Result.unwrap()
        |> Compression.gunzip()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "test data 12345");
}

static void test_compression_deflate_with_invalid_level_high() {
    ASSERT_EVAL_FAILURE(R"(Compression.deflate_with("hello", 10))");
}

static void test_compression_gzip_with_invalid_level_zero() {
    ASSERT_EVAL_FAILURE(R"(Compression.gzip_with("hello", 0))");
}

// ─────────────────────────────────────────────────────────────────────
// Compression.Format + generic compress/decompress dispatch.
// ─────────────────────────────────────────────────────────────────────

static void test_compression_module_has_generic_functions() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Compression.compress"));
    ASSERT_TRUE(env->has("Compression.decompress"));
}

static void test_compression_compress_deflate_matches_direct() {
    const auto v = eval(R"(
        "hello world" |> Compression.compress(Compression.Format.Deflate)
    )");
    const auto direct = eval(R"("hello world" |> Compression.deflate())");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), direct.as_string());
}

static void test_compression_compress_gzip_matches_direct() {
    const auto v = eval(R"(
        "hello world" |> Compression.compress(Compression.Format.Gzip)
    )");
    const auto direct = eval(R"("hello world" |> Compression.gzip())");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), direct.as_string());
}

static void test_compression_compress_rle_matches_direct() {
    const auto v = eval(R"(
        "aaabbbcc" |> Compression.compress(Compression.Format.Rle)
    )");
    const auto direct = eval(R"("aaabbbcc" |> Compression.encode_rle())");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), direct.as_string());
}

static void test_compression_decompress_deflate_roundtrip() {
    const auto v = eval(R"(
        "hello world"
        |> Compression.compress(Compression.Format.Deflate)
        |> Compression.decompress(Compression.Format.Deflate)
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello world");
}

static void test_compression_decompress_gzip_roundtrip() {
    const auto v = eval(R"(
        "hello world"
        |> Compression.compress(Compression.Format.Gzip)
        |> Compression.decompress(Compression.Format.Gzip)
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello world");
}

static void test_compression_decompress_rle_roundtrip() {
    const auto v = eval(R"(
        "aaabbbcc"
        |> Compression.compress(Compression.Format.Rle)
        |> Compression.decompress(Compression.Format.Rle)
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "aaabbbcc");
}

static void test_compression_decompress_malformed_deflate_fails() {
    ASSERT_EVAL_FAILURE(
        R"(Compression.decompress("not valid deflate", Compression.Format.Deflate))");
}

static void test_compression_decompress_malformed_gzip_fails() {
    ASSERT_EVAL_FAILURE(
        R"(Compression.decompress("not a valid gzip stream!!", Compression.Format.Gzip))");
}

static void test_compression_decompress_malformed_rle_fails() {
    ASSERT_EVAL_FAILURE(R"(Compression.decompress("0a", Compression.Format.Rle))");
}

// ─────────────────────────────────────────────────────────────────────
// Direct codec-layer coverage (luma::compression).  These bypass the VM
// so they can exercise binary payloads — every byte value, long runs,
// and deliberately corrupted streams — that are awkward to express in
// Luma source literals.
// ─────────────────────────────────────────────────────────────────────

static std::string all_byte_values() {
    std::string out{};
    out.reserve(256);

    for (int b = 0; b < 256; ++b) {
        out.push_back(static_cast<char>(b));
    }

    return out;
}

static void test_codec_deflate_inflate_all_bytes() {
    const auto data = all_byte_values();
    const auto restored =
        luma::compression::deflate_decompress(luma::compression::deflate_compress(data));

    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(*restored, data);
}

static void test_codec_gzip_gunzip_all_bytes() {
    const auto data = all_byte_values();
    const auto restored =
        luma::compression::gzip_decompress(luma::compression::gzip_compress(data));

    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(*restored, data);
}

static void test_codec_rle_roundtrip_binary() {
    std::string data = all_byte_values();
    data += std::string(25, 'a');           // run longer than the nine-char chunk limit
    data += "1112223334445556667778889990"; // digit-heavy payload

    const auto decoded = luma::compression::rle_decode(luma::compression::rle_encode(data));

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(*decoded, data);
}

static void test_codec_empty_inputs() {
    ASSERT_TRUE(luma::compression::deflate_compress("").empty());
    ASSERT_TRUE(luma::compression::rle_encode("").empty());

    const auto inflated = luma::compression::deflate_decompress("");
    ASSERT_TRUE(inflated.has_value());
    ASSERT_TRUE(inflated->empty());

    const auto unrle = luma::compression::rle_decode("");
    ASSERT_TRUE(unrle.has_value());
    ASSERT_TRUE(unrle->empty());

    const auto gunzipped = luma::compression::gzip_decompress(luma::compression::gzip_compress(""));
    ASSERT_TRUE(gunzipped.has_value());
    ASSERT_TRUE(gunzipped->empty());
}

static void test_codec_rle_long_run_chunking() {
    const auto encoded = luma::compression::rle_encode(std::string(20, 'a'));
    ASSERT_EQ(encoded, "9a9a2a");

    const auto decoded = luma::compression::rle_decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(*decoded, std::string(20, 'a'));
}

static void test_codec_gzip_structure() {
    const auto gz = luma::compression::gzip_compress("hello world");

    ASSERT_GE(gz.size(), static_cast<std::size_t>(18));
    ASSERT_EQ(static_cast<int>(static_cast<unsigned char>(gz[0])), 0x1f);
    ASSERT_EQ(static_cast<int>(static_cast<unsigned char>(gz[1])), 0x8b);
    ASSERT_EQ(static_cast<int>(static_cast<unsigned char>(gz[2])), 0x08);
}

static void test_codec_repetitive_compresses_smaller() {
    const std::string data(4096, 'A');
    const auto compressed = luma::compression::deflate_compress(data);

    ASSERT_LT(compressed.size(), data.size());

    const auto restored = luma::compression::deflate_decompress(compressed);
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(*restored, data);
}

static void test_codec_gunzip_bad_magic() {
    std::string bad(20, '\0');
    bad[0] = 'P';
    bad[1] = 'K';

    ASSERT_FALSE(luma::compression::gzip_decompress(bad).has_value());
}

static void test_codec_gunzip_too_short() {
    ASSERT_FALSE(luma::compression::gzip_decompress(std::string("\x1f\x8b\x08", 3)).has_value());
}

static void test_codec_gunzip_crc_mismatch() {
    auto gz = luma::compression::gzip_compress("payload data here");
    gz[gz.size() - 8] = static_cast<char>(gz[gz.size() - 8] ^ 0xFF); // corrupt CRC32

    ASSERT_FALSE(luma::compression::gzip_decompress(gz).has_value());
}

static void test_codec_gunzip_size_mismatch() {
    auto gz = luma::compression::gzip_compress("payload data here");
    gz[gz.size() - 4] = static_cast<char>(gz[gz.size() - 4] ^ 0xFF); // corrupt ISIZE

    ASSERT_FALSE(luma::compression::gzip_decompress(gz).has_value());
}

static void test_codec_inflate_invalid_block() {
    // BTYPE == 11 is a reserved deflate block type and fails immediately.
    ASSERT_FALSE(luma::compression::deflate_decompress(std::string("\xff\xff\xff", 3)).has_value());
}

static void test_codec_rle_decode_zero_count() {
    ASSERT_FALSE(luma::compression::rle_decode("0a").has_value());
}

static void test_codec_rle_decode_missing_char() {
    ASSERT_FALSE(luma::compression::rle_decode("3").has_value());
}

static void test_codec_rle_decode_trailing_count() {
    ASSERT_FALSE(luma::compression::rle_decode("3a2").has_value());
}

// ─────────────────────────────────────────────────────────────────────
// Compression.Error typed-error slice (decompress_typed / inflate_typed /
// gunzip_typed → result<string, Compression.Error>).  Codec-level *_checked
// classification plus VM-level choice inspection.
// ─────────────────────────────────────────────────────────────────────

static void test_codec_deflate_checked_success() {
    const auto r = luma::compression::deflate_decompress_checked(
        luma::compression::deflate_compress("hello world"));

    ASSERT_TRUE(r.is_ok());
    ASSERT_EQ(r.value(), "hello world");
}

static void test_codec_deflate_checked_corrupt() {
    // BTYPE == 11 is a reserved deflate block type — a genuine data error.
    const auto r = luma::compression::deflate_decompress_checked(std::string("\xff\xff\xff", 3));

    ASSERT_TRUE(r.is_err());
    ASSERT_TRUE(r.error() == luma::compression::DecodeError::Corrupt);
}

static void test_codec_gzip_checked_truncated_short() {
    const auto r = luma::compression::gzip_decompress_checked(std::string("\x1f\x8b\x08", 3));

    ASSERT_TRUE(r.is_err());
    ASSERT_TRUE(r.error() == luma::compression::DecodeError::Truncated);
}

static void test_codec_gzip_checked_unsupported_magic() {
    std::string bad(20, '\0');
    bad[0] = 'P';
    bad[1] = 'K';

    const auto r = luma::compression::gzip_decompress_checked(bad);

    ASSERT_TRUE(r.is_err());
    ASSERT_TRUE(r.error() == luma::compression::DecodeError::UnsupportedFormat);
}

static void test_codec_gzip_checked_unsupported_method() {
    // Valid gzip magic but a compression method other than deflate (CM != 8).
    auto gz = luma::compression::gzip_compress("payload data here");
    gz[2] = static_cast<char>(0x07);

    const auto r = luma::compression::gzip_decompress_checked(gz);

    ASSERT_TRUE(r.is_err());
    ASSERT_TRUE(r.error() == luma::compression::DecodeError::UnsupportedFormat);
}

static void test_codec_gzip_checked_corrupt_crc() {
    auto gz = luma::compression::gzip_compress("payload data here");
    gz[gz.size() - 8] = static_cast<char>(gz[gz.size() - 8] ^ 0xFF); // corrupt CRC32

    const auto r = luma::compression::gzip_decompress_checked(gz);

    ASSERT_TRUE(r.is_err());
    ASSERT_TRUE(r.error() == luma::compression::DecodeError::Corrupt);
}

static void test_codec_rle_checked_corrupt() {
    const auto r = luma::compression::rle_decode_checked("0a");

    ASSERT_TRUE(r.is_err());
    ASSERT_TRUE(r.error() == luma::compression::DecodeError::Corrupt);
}

static void test_codec_rle_checked_truncated() {
    const auto r = luma::compression::rle_decode_checked("3");

    ASSERT_TRUE(r.is_err());
    ASSERT_TRUE(r.error() == luma::compression::DecodeError::Truncated);
}

static void test_compression_gunzip_typed_success() {
    const auto v = eval(R"(
        "hello world"
        |> Compression.gzip()
        |> Compression.gunzip_typed()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello world");
}

static void test_compression_gunzip_typed_unsupported_format() {
    // 20 bytes with the wrong magic → a typed UnsupportedFormat choice.
    const auto v = eval(R"(Compression.gunzip_typed("not a gzip stream!!!"))");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "Error");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "UnsupportedFormat");
}

static void test_compression_inflate_typed_success() {
    const auto v = eval(R"(
        "typed inflate"
        |> Compression.deflate()
        |> Compression.inflate_typed()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "typed inflate");
}

static void test_compression_inflate_typed_corrupt() {
    const auto v = eval(R"(Compression.inflate_typed("not valid deflate at all"))");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "Error");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "Corrupt");
}

static void test_compression_decompress_typed_rle_corrupt() {
    const auto v = eval(R"(Compression.decompress_typed("0a", Compression.Format.Rle))");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_choice());
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->type_name, "Error");
    ASSERT_EQ(v.as_result()->owned_inner->as_choice()->variant, "Corrupt");
}

static void test_compression_decompress_typed_gzip_roundtrip() {
    const auto v = eval(R"(
        "roundtrip"
        |> Compression.compress(Compression.Format.Gzip)
        |> Compression.decompress_typed(Compression.Format.Gzip)
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "roundtrip");
}

static void test_compression_module_has_typed_functions() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Compression.decompress_typed"));
    ASSERT_TRUE(env->has("Compression.inflate_typed"));
    ASSERT_TRUE(env->has("Compression.gunzip_typed"));
}

static void test_compression_gzip_file_with_non_string_path_is_catchable() {
    // Regression: Compression.gzip_file_with called as_string() on its path
    // arguments with no type guard.  When an `any`-typed non-string flows in
    // (here an element of a bare `array`), that raised std::bad_variant_access,
    // which the VM's RuntimeError-only dispatch cannot catch — an uncatchable
    // abort rather than an error a Luma try/catch can handle.  The call must
    // now raise a catchable RuntimeError so the catch block runs.
    const auto* const src =
        "array box = [42]\n"
        "mutable boolean caught = false\n"
        "try {\n"
        "    result<string> _r = Compression.gzip_file_with(box[0], \"x.gz\", 5)\n"
        "} catch (_e) {\n"
        "    caught = true\n"
        "}\n"
        "caught";

    ASSERT_TRUE(eval(src).as_bool());
}

int main() {
    RUN(test_compression_compressed_size);
    RUN(test_compression_deflate_inflate);
    RUN(test_compression_deflate_with_invalid_level);
    RUN(test_compression_deflate_with_level);
    RUN(test_compression_deflate_with_max_level);
    RUN(test_compression_gzip_gunzip);
    RUN(test_compression_gzip_with_invalid_level);
    RUN(test_compression_gzip_with_level);
    RUN(test_compression_inflate_malformed);
    RUN(test_compression_module);
    RUN(test_compression_rle_decode);
    RUN(test_compression_rle_encode);
    RUN(test_compression_rle_roundtrip);

    RUN(test_compression_deflate_empty);
    RUN(test_compression_gzip_empty_roundtrip);
    RUN(test_compression_rle_encode_chunking);
    RUN(test_compression_rle_digit_safety);
    RUN(test_compression_compressed_size_smaller_than_input);
    RUN(test_compression_gunzip_malformed);
    RUN(test_compression_decode_rle_malformed);
    RUN(test_compression_gzip_with_min_level);
    RUN(test_compression_deflate_with_invalid_level_high);
    RUN(test_compression_gzip_with_invalid_level_zero);
    RUN(test_compression_gzip_file_with_non_string_path_is_catchable);

    RUN(test_compression_module_has_generic_functions);
    RUN(test_compression_compress_deflate_matches_direct);
    RUN(test_compression_compress_gzip_matches_direct);
    RUN(test_compression_compress_rle_matches_direct);
    RUN(test_compression_decompress_deflate_roundtrip);
    RUN(test_compression_decompress_gzip_roundtrip);
    RUN(test_compression_decompress_rle_roundtrip);
    RUN(test_compression_decompress_malformed_deflate_fails);
    RUN(test_compression_decompress_malformed_gzip_fails);
    RUN(test_compression_decompress_malformed_rle_fails);

    RUN(test_codec_deflate_inflate_all_bytes);
    RUN(test_codec_gzip_gunzip_all_bytes);
    RUN(test_codec_rle_roundtrip_binary);
    RUN(test_codec_empty_inputs);
    RUN(test_codec_rle_long_run_chunking);
    RUN(test_codec_gzip_structure);
    RUN(test_codec_repetitive_compresses_smaller);
    RUN(test_codec_gunzip_bad_magic);
    RUN(test_codec_gunzip_too_short);
    RUN(test_codec_gunzip_crc_mismatch);
    RUN(test_codec_gunzip_size_mismatch);
    RUN(test_codec_inflate_invalid_block);
    RUN(test_codec_rle_decode_zero_count);
    RUN(test_codec_rle_decode_missing_char);
    RUN(test_codec_rle_decode_trailing_count);

    RUN(test_codec_deflate_checked_success);
    RUN(test_codec_deflate_checked_corrupt);
    RUN(test_codec_gzip_checked_truncated_short);
    RUN(test_codec_gzip_checked_unsupported_magic);
    RUN(test_codec_gzip_checked_unsupported_method);
    RUN(test_codec_gzip_checked_corrupt_crc);
    RUN(test_codec_rle_checked_corrupt);
    RUN(test_codec_rle_checked_truncated);
    RUN(test_compression_gunzip_typed_success);
    RUN(test_compression_gunzip_typed_unsupported_format);
    RUN(test_compression_inflate_typed_success);
    RUN(test_compression_inflate_typed_corrupt);
    RUN(test_compression_decompress_typed_rle_corrupt);
    RUN(test_compression_decompress_typed_gzip_roundtrip);
    RUN(test_compression_module_has_typed_functions);
    return SUMMARY();
}
