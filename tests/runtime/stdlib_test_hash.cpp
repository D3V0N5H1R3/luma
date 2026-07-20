// Standard library tests: Hash.

#include "common/resource_limits.hpp"
#include "stdlib_test_helpers.hpp"

static void test_hash_algorithms() {
    const auto v = eval(R"(Hash.algorithms())");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), std::size_t{5});

    const auto& els = *v.as_array()->elements;
    ASSERT_EQ(els[0].as_string(), "md5");
    ASSERT_EQ(els[1].as_string(), "sha1");
    ASSERT_EQ(els[2].as_string(), "sha256");
    ASSERT_EQ(els[3].as_string(), "sha512");
    ASSERT_EQ(els[4].as_string(), "crc32");
}

static void test_hash_crc32() {
    const auto v = eval(R"(Hash.crc32("hello"))");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), int64_t{907060870});
}

static void test_hash_crc32_known_vectors() {
    // CRC-32/ISO-HDLC check value: crc32("123456789") == 0xCBF43926.
    ASSERT_EQ(eval(R"(Hash.crc32("123456789"))").as_integer(), int64_t{3421780262});

    // The empty input maps to the algorithm's zeroed final register.
    ASSERT_EQ(eval(R"(Hash.crc32(""))").as_integer(), int64_t{0});

    // A second well-known vector (0x414FA339).
    ASSERT_EQ(eval(R"(Hash.crc32("The quick brown fox jumps over the lazy dog"))").as_integer(),
              int64_t{1095738169});
}

static void test_hash_hmac_sha256() {
    const auto v = eval(R"(Hash.hmac_sha256("key", "message"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string().size(), std::size_t{64});
}

static void test_hash_hmac_sha256_known_vectors() {
    // Canonical HMAC-SHA256 example (Wikipedia): key "key", the pangram message.
    ASSERT_EQ(eval(R"(Hash.hmac_sha256("key", "The quick brown fox jumps over the lazy dog"))")
                  .as_string(),
              "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");

    // RFC 4231 Test Case 2.
    ASSERT_EQ(eval(R"(Hash.hmac_sha256("Jefe", "what do ya want for nothing?"))").as_string(),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

static void test_hash_hmac_sha512() {
    const auto v = eval(R"(Hash.hmac_sha512("key", "message"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string().size(), std::size_t{128});
}

static void test_hash_hmac_sha512_known_vector() {
    // RFC 4231 Test Case 2 for HMAC-SHA512.
    ASSERT_EQ(
        eval(R"(Hash.hmac_sha512("Jefe", "what do ya want for nothing?"))").as_string(),
        "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f"
        "65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
}

static void test_hash_md5() {
    // MD5("") = d41d8cd98f00b204e9800998ecf8427e
    const auto v = eval(R"(Hash.md5(""))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "d41d8cd98f00b204e9800998ecf8427e");
}

static void test_hash_md5_hello() {
    // MD5("hello") = 5d41402abc4b2a76b9719d911017c592
    const auto v = eval(R"(Hash.md5("hello"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "5d41402abc4b2a76b9719d911017c592");
}

static void test_hash_md5_abc() {
    // RFC 1321 test vector: MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
    const auto v = eval(R"(Hash.md5("abc"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "900150983cd24fb0d6963f7d28e17f72");
}

static void test_hash_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Hash.md5"));
    ASSERT_TRUE(env->has("Hash.sha1"));
    ASSERT_TRUE(env->has("Hash.sha256"));
    ASSERT_TRUE(env->has("Hash.sha512"));
    ASSERT_TRUE(env->has("Hash.crc32"));
    ASSERT_TRUE(env->has("Hash.hmac_sha256"));
    ASSERT_TRUE(env->has("Hash.hmac_sha512"));
    ASSERT_TRUE(env->has("Hash.sha256_file"));
    ASSERT_TRUE(env->has("Hash.sha512_file"));
    ASSERT_TRUE(env->has("Hash.verify"));
    ASSERT_TRUE(env->has("Hash.algorithms"));
}

static void test_hash_sha1() {
    // SHA1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
    const auto v = eval(R"(Hash.sha1(""))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

static void test_hash_sha1_abc() {
    // FIPS 180 test vector: SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
    const auto v = eval(R"(Hash.sha1("abc"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

static void test_hash_sha256() {
    // SHA256("hello") = 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
    const auto v = eval(R"(Hash.sha256("hello"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

static void test_hash_sha256_abc() {
    // FIPS 180 test vector.
    const auto v = eval(R"(Hash.sha256("abc"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void test_hash_sha256_empty() {
    // SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    const auto v = eval(R"(Hash.sha256(""))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

static void test_hash_sha512() {
    const auto v = eval(R"(Hash.sha512(""))");

    ASSERT_TRUE(v.is_string());
    // Full SHA-512 of the empty string.
    ASSERT_EQ(
        v.as_string(),
        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318"
        "d2877eec2f63b931bd47417a81a538327af927da3e");
    ASSERT_EQ(v.as_string().size(), std::size_t{128});
}

static void test_hash_sha512_abc() {
    // FIPS 180 test vector for SHA-512.
    const auto v = eval(R"(Hash.sha512("abc"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(
        v.as_string(),
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c"
        "23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
}

static void test_hash_digest_lengths() {
    // Each digest is a fixed-width lowercase hex string regardless of input.
    ASSERT_EQ(eval(R"(Hash.md5("anything"))").as_string().size(), std::size_t{32});
    ASSERT_EQ(eval(R"(Hash.sha1("anything"))").as_string().size(), std::size_t{40});
    ASSERT_EQ(eval(R"(Hash.sha256("anything"))").as_string().size(), std::size_t{64});
    ASSERT_EQ(eval(R"(Hash.sha512("anything"))").as_string().size(), std::size_t{128});
}

static void test_hash_deterministic_and_distinct() {
    // The same input always yields the same digest; distinct inputs differ.
    ASSERT_EQ(eval(R"(Hash.sha256("repeatable"))").as_string(),
              eval(R"(Hash.sha256("repeatable"))").as_string());
    ASSERT_NE(eval(R"(Hash.sha256("input-a"))").as_string(),
              eval(R"(Hash.sha256("input-b"))").as_string());

    // A single-bit-style change (one character) avalanches the whole digest.
    ASSERT_NE(eval(R"(Hash.md5("hello"))").as_string(), eval(R"(Hash.md5("hellp"))").as_string());
}

static void test_hash_verify() {
    const auto v = eval(R"(Hash.verify("sha256", "hello",
        Hash.sha256("hello")))");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_hash_verify_all_algorithms() {
    // verify round-trips against every digest algorithm it accepts.
    ASSERT_TRUE(eval(R"(Hash.verify("md5", "hello", Hash.md5("hello")))").as_bool());
    ASSERT_TRUE(eval(R"(Hash.verify("sha1", "hello", Hash.sha1("hello")))").as_bool());
    ASSERT_TRUE(eval(R"(Hash.verify("sha256", "hello", Hash.sha256("hello")))").as_bool());
    ASSERT_TRUE(eval(R"(Hash.verify("sha512", "hello", Hash.sha512("hello")))").as_bool());
}

static void test_hash_verify_false() {
    const auto v = eval(R"(Hash.verify("sha256", "hello", "wrong"))");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

static void test_hash_verify_length_mismatch() {
    // The constant-time compare returns false on a length mismatch without
    // reading past either buffer.
    const auto v = eval(R"(Hash.verify("sha256", "hello", "abc"))");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

static void test_hash_verify_wrong_algorithm_for_digest() {
    // A valid digest for one algorithm must not verify under another.
    const auto v = eval(R"(Hash.verify("sha256", "hello", Hash.md5("hello")))");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

static void test_hash_sha256_file_success() {
    // Hashing a file's contents agrees with hashing the same bytes directly.
    const LumaTempFile file{"_test_hash_sha256_file.bin", "hash me please"};

    const auto v = eval(R"(Hash.sha256_file("_test_hash_sha256_file.bin"))");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_string(),
              eval(R"(Hash.sha256("hash me please"))").as_string());
}

static void test_hash_sha512_file_success() {
    const LumaTempFile file{"_test_hash_sha512_file.bin", "hash me please"};

    const auto v = eval(R"(Hash.sha512_file("_test_hash_sha512_file.bin"))");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_string(),
              eval(R"(Hash.sha512("hash me please"))").as_string());
}

static void test_hash_file_missing_fails() {
    // A missing file yields a failure result, not a thrown error.
    ASSERT_EVAL_FAILURE(R"(Hash.sha256_file("_nonexistent_hash_target.bin"))");
    ASSERT_EVAL_FAILURE(R"(Hash.sha512_file("_nonexistent_hash_target.bin"))");
}

static void test_hash_file_rejects_oversized_file() {
    // A file larger than the interpreter's maximum string size must be rejected
    // before it is slurped into memory, yielding a failure result rather than
    // exhausting memory. The file is created with the default cap in force, then
    // the cap is lowered so the test need not materialise a 256 MB file.
    const LumaTempFile file{"_test_hash_oversize.bin", std::string(64, 'x')};
    const LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(16)};

    const auto sha256 = eval(R"(Hash.sha256_file("_test_hash_oversize.bin"))");
    ASSERT_RESULT_FAILURE(sha256);

    const auto sha512 = eval(R"(Hash.sha512_file("_test_hash_oversize.bin"))");
    ASSERT_RESULT_FAILURE(sha512);
}

// ── Negative tests ──
// eval() runs the pipeline unchecked (no type checker), so these exercise the
// runtime argument guards in hash_digest.cpp directly.

static void test_hash_digest_rejects_non_string() {
    ASSERT_TRUE(luma::test::eval_throws("Hash.md5(42)"));
    ASSERT_TRUE(luma::test::eval_throws("Hash.sha1(true)"));
    ASSERT_TRUE(luma::test::eval_throws("Hash.sha256(3.14)"));
    ASSERT_TRUE(luma::test::eval_throws("Hash.sha512([1, 2, 3])"));
    ASSERT_TRUE(luma::test::eval_throws("Hash.crc32(42)"));
}

static void test_hash_hmac_rejects_non_string() {
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.hmac_sha256(42, "message"))"));
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.hmac_sha256("key", 42))"));
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.hmac_sha512(42, "message"))"));
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.hmac_sha512("key", true))"));
}

static void test_hash_verify_rejects_non_string() {
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.verify(42, "input", "expected"))"));
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.verify("sha256", 42, "expected"))"));
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.verify("sha256", "input", 42))"));
}

static void test_hash_verify_unknown_algorithm_throws() {
    // An unrecognised algorithm name is a runtime error (md_type_from_name → NONE).
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.verify("sha999", "input", "expected"))"));
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.verify("", "input", "expected"))"));
    ASSERT_TRUE(luma::test::eval_throws(R"(Hash.verify("crc32", "input", "expected"))"));
}

static void test_hash_verify_unknown_algorithm_message() {
    // The error names the offending algorithm and lists the supported set.
    ASSERT_THROWS_WITH_MESSAGE(eval(R"(Hash.verify("bogus", "input", "expected"))"),
                               "unknown algorithm");
}

int main() {
    RUN(test_hash_algorithms);
    RUN(test_hash_crc32);
    RUN(test_hash_crc32_known_vectors);
    RUN(test_hash_hmac_sha256);
    RUN(test_hash_hmac_sha256_known_vectors);
    RUN(test_hash_hmac_sha512);
    RUN(test_hash_hmac_sha512_known_vector);
    RUN(test_hash_md5);
    RUN(test_hash_md5_hello);
    RUN(test_hash_md5_abc);
    RUN(test_hash_module);
    RUN(test_hash_sha1);
    RUN(test_hash_sha1_abc);
    RUN(test_hash_sha256);
    RUN(test_hash_sha256_abc);
    RUN(test_hash_sha256_empty);
    RUN(test_hash_sha512);
    RUN(test_hash_sha512_abc);
    RUN(test_hash_digest_lengths);
    RUN(test_hash_deterministic_and_distinct);
    RUN(test_hash_verify);
    RUN(test_hash_verify_all_algorithms);
    RUN(test_hash_verify_false);
    RUN(test_hash_verify_length_mismatch);
    RUN(test_hash_verify_wrong_algorithm_for_digest);
    RUN(test_hash_sha256_file_success);
    RUN(test_hash_sha512_file_success);
    RUN(test_hash_file_missing_fails);
    RUN(test_hash_file_rejects_oversized_file);
    RUN(test_hash_digest_rejects_non_string);
    RUN(test_hash_hmac_rejects_non_string);
    RUN(test_hash_verify_rejects_non_string);
    RUN(test_hash_verify_unknown_algorithm_throws);
    RUN(test_hash_verify_unknown_algorithm_message);
    return SUMMARY();
}
