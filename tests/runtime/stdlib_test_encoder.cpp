// Standard library tests: Encoder.

#include "stdlib_test_helpers.hpp"

static void test_encoder_base64_decode() {
    ASSERT_EVAL_STR("Encoder.decode_base64(\"aGVsbG8=\")", "hello");
}

static void test_encoder_base64_encode() {
    ASSERT_EVAL_STR("Encoder.encode_base64(\"hello\")", "aGVsbG8=");

    ASSERT_EVAL_STR("Encoder.encode_base64(\"\")", "");
}

static void test_encoder_decode_base64url() {
    ASSERT_EVAL_STR("Encoder.decode_base64url(\"aGVsbG8\")", "hello");
}

static void test_encoder_decode_base64url_invalid() {
    ASSERT_EVAL_FAILURE("Encoder.decode_base64url(\"!!invalid!!\")");
}

static void test_encoder_decode_url() {
    ASSERT_EVAL_STR("Encoder.decode_url(\"hello%20world\")", "hello world");
}

static void test_encoder_decode_url_invalid() {
    ASSERT_EVAL_FAILURE("Encoder.decode_url(\"hello%GG\")");
}

static void test_encoder_encode_base64url() {
    // base64url uses - and _ instead of + and /, no padding.
    ASSERT_EVAL_STR("Encoder.encode_base64url(\"hello\")", "aGVsbG8");

    ASSERT_EVAL_STR("Encoder.encode_base64url(\"\")", "");
}

static void test_encoder_encode_url() {
    ASSERT_EVAL_STR("Encoder.encode_url(\"hello world\")", "hello%20world");

    ASSERT_EVAL_STR("Encoder.encode_url(\"a=b&c=d\")", "a%3Db%26c%3Dd");
}

static void test_encoder_base64_padding_variants() {
    // Standard base64 pads 1- and 2-byte tails to a 4-character group.
    ASSERT_EVAL_STR("Encoder.encode_base64(\"a\")", "YQ==");
    ASSERT_EVAL_STR("Encoder.encode_base64(\"ab\")", "YWI=");
    ASSERT_EVAL_STR("Encoder.encode_base64(\"abc\")", "YWJj");
    ASSERT_EVAL_STR("Encoder.encode_base64(\"abcd\")", "YWJjZA==");
}

static void test_encoder_base64_special_alphabet() {
    // Bytes that map to the final two alphabet slots (62, 63): standard uses
    // '+' and '/', URL-safe uses '-' and '_'.
    ASSERT_EVAL_STR("Encoder.encode_base64(\">>>\")", "Pj4+");
    ASSERT_EVAL_STR("Encoder.encode_base64(\"???\")", "Pz8/");
    ASSERT_EVAL_STR("Encoder.encode_base64url(\">>>\")", "Pj4-");
    ASSERT_EVAL_STR("Encoder.encode_base64url(\"???\")", "Pz8_");
}

static void test_encoder_base64_roundtrip() {
    // End-to-end identity through the Luma-level functions, including the
    // base64 special characters and a multi-byte UTF-8 sequence.
    ASSERT_EVAL_STR("Encoder.decode_base64(Result.unwrap("
                    "Encoder.encode_base64(\"Mixed >>> ??? text! 🎉\")))",
                    "Mixed >>> ??? text! 🎉");
}

static void test_encoder_base64_decode_unpadded() {
    // The standard decoder also accepts input whose padding has been stripped.
    ASSERT_EVAL_STR("Encoder.decode_base64(\"YQ\")", "a");
    ASSERT_EVAL_STR("Encoder.decode_base64(\"YWI\")", "ab");
}

static void test_encoder_base64_decode_invalid() {
    // A stray non-alphabet character fails, as does input too short to form
    // even a single decoded byte.
    ASSERT_EVAL_FAILURE("Encoder.decode_base64(\"!!!\")");
    ASSERT_EVAL_FAILURE("Encoder.decode_base64(\"ab*d\")");
    ASSERT_EVAL_FAILURE("Encoder.decode_base64(\"Q\")");

    // A padding character in the third position requires the fourth to also be
    // padding; "AA=A" is malformed and must be rejected rather than decoded as
    // if it were "AA==".
    ASSERT_EVAL_FAILURE("Encoder.decode_base64(\"AA=A\")");
    ASSERT_EVAL_FAILURE("Encoder.decode_base64url(\"AA=A\")");
}

static void test_encoder_base64url_padding_variants() {
    // URL-safe encoding never emits padding.
    ASSERT_EVAL_STR("Encoder.encode_base64url(\"ab\")", "YWI");
    ASSERT_EVAL_STR("Encoder.encode_base64url(\"abc\")", "YWJj");
}

static void test_encoder_base64url_roundtrip() {
    ASSERT_EVAL_STR("Encoder.decode_base64url(Result.unwrap("
                    "Encoder.encode_base64url(\"data?>>> with _-_ bytes\")))",
                    "data?>>> with _-_ bytes");
}

static void test_encoder_url_unreserved_passthrough() {
    // RFC 3986 unreserved characters are never percent-encoded.
    ASSERT_EVAL_STR("Encoder.encode_url(\"ABCabc012-._~\")", "ABCabc012-._~");
}

static void test_encoder_url_decode_plus_as_space() {
    ASSERT_EVAL_STR("Encoder.decode_url(\"a+b+c\")", "a b c");
}

static void test_encoder_url_roundtrip() {
    ASSERT_EVAL_STR("Encoder.decode_url(Result.unwrap("
                    "Encoder.encode_url(\"key=value & name=A+B/C\")))",
                    "key=value & name=A+B/C");
}

static void test_encoder_url_decode_invalid() {
    // Truncated escapes (fewer than two hex digits remaining) and a bad hex
    // digit in either nibble must fail.
    ASSERT_EVAL_FAILURE("Encoder.decode_url(\"%\")");
    ASSERT_EVAL_FAILURE("Encoder.decode_url(\"%2\")");
    ASSERT_EVAL_FAILURE("Encoder.decode_url(\"%G0\")");
    ASSERT_EVAL_FAILURE("Encoder.decode_url(\"%2G\")");
}

static void test_encoder_rejects_non_string() {
    // Every Encoder function expects a string argument.
    ASSERT_TRUE(luma::test::eval_throws("Encoder.encode_base64(123)"));
    ASSERT_TRUE(luma::test::eval_throws("Encoder.decode_url(true)"));
}

static void test_encoder_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Encoder.encode_base64"));
    ASSERT_TRUE(env->has("Encoder.decode_base64"));
    ASSERT_TRUE(env->has("Encoder.encode_base64url"));
    ASSERT_TRUE(env->has("Encoder.decode_base64url"));
    ASSERT_TRUE(env->has("Encoder.encode_url"));
    ASSERT_TRUE(env->has("Encoder.decode_url"));
}

int main() {
    RUN(test_encoder_base64_decode);
    RUN(test_encoder_base64_encode);
    RUN(test_encoder_base64_padding_variants);
    RUN(test_encoder_base64_special_alphabet);
    RUN(test_encoder_base64_roundtrip);
    RUN(test_encoder_base64_decode_unpadded);
    RUN(test_encoder_base64_decode_invalid);
    RUN(test_encoder_decode_base64url);
    RUN(test_encoder_decode_base64url_invalid);
    RUN(test_encoder_decode_url);
    RUN(test_encoder_decode_url_invalid);
    RUN(test_encoder_encode_base64url);
    RUN(test_encoder_base64url_padding_variants);
    RUN(test_encoder_base64url_roundtrip);
    RUN(test_encoder_encode_url);
    RUN(test_encoder_url_unreserved_passthrough);
    RUN(test_encoder_url_decode_plus_as_space);
    RUN(test_encoder_url_roundtrip);
    RUN(test_encoder_url_decode_invalid);
    RUN(test_encoder_rejects_non_string);
    RUN(test_encoder_module);
    return SUMMARY();
}
