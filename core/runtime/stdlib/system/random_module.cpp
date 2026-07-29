#include "runtime/stdlib/system/random_module.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/system/random_bounded.hpp"

#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#endif

namespace luma {

namespace {

namespace uuid_constants {
// UUID v4 per RFC 4122: set version (4) in bits 4-7 of byte 6.
constexpr unsigned char version_mask = 0x0F;
constexpr unsigned char version_4 = 0x40;
// Variant RFC 4122: set bits 6-7 of byte 8.
constexpr unsigned char variant_mask = 0x3F;
constexpr unsigned char variant_rfc4122 = 0x80;
// Hex lookup table for UUID formatting.
constexpr std::array<char, 16> hex_chars = {'0', '1', '2', '3', '4', '5', '6', '7',
                                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
// Byte positions where dashes appear in UUID string.
constexpr std::array<int, 4> dash_positions = {4, 6, 8, 10};
} // namespace uuid_constants

constexpr std::string_view alphanumeric_chars{
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"};

// Validate a requested string length, shared by generate_string and
// secure_string. Returns the failure Value on violation, or nullopt when valid.
[[nodiscard]] std::optional<Value> check_string_length(std::int64_t len, std::string_view fn) {
    if (len < 0) {
        return make_failure_value(error_msg("Random", fn, "length must be non-negative"));
    }

    if (static_cast<std::size_t>(len) > ResourceLimits::max_string_size) {
        return make_failure_value(error_msg("Random", fn, "length exceeds maximum string size"));
    }

    return std::nullopt;
}

[[nodiscard]] std::string format_uuid_bytes(std::array<unsigned char, 16>& bytes) {
    // Set version 4 and variant RFC 4122.
    bytes[6] = static_cast<unsigned char>((bytes[6] & uuid_constants::version_mask) |
                                          uuid_constants::version_4);
    bytes[8] = static_cast<unsigned char>((bytes[8] & uuid_constants::variant_mask) |
                                          uuid_constants::variant_rfc4122);

    std::string uuid;
    uuid.reserve(36);

    for (int i{0}; i < 16; ++i) {
        if (std::ranges::find(uuid_constants::dash_positions, i) !=
            uuid_constants::dash_positions.end()) {
            uuid += '-';
        }
        const auto octet = bytes[static_cast<std::size_t>(i)];
        uuid += uuid_constants::hex_chars[static_cast<std::size_t>(octet >> 4)];
        uuid += uuid_constants::hex_chars[static_cast<std::size_t>(octet & 0x0F)];
    }

    return uuid;
}

// Validates a canonical 8-4-4-4-12 UUID string (36 chars, hyphens at positions
// 8/13/18/23, lowercase-or-uppercase hex elsewhere) and, on success, returns the
// lower-cased canonical form.  std::nullopt marks any deviation.  Backs
// Random.parse_uuid, giving a UUID the same parse-on-the-way-in guarantee a
// plain string cannot offer.
[[nodiscard]] std::optional<std::string> canonicalize_uuid(std::string_view text) {
    if (text.size() != 36) {
        return std::nullopt;
    }

    std::string canonical;
    canonical.reserve(36);

    for (std::size_t i{0}; i < text.size(); ++i) {
        const bool is_dash_position = (i == 8 || i == 13 || i == 18 || i == 23);
        const char c = text[i];

        if (is_dash_position) {
            if (c != '-') {
                return std::nullopt;
            }
            canonical += '-';
            continue;
        }

        if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
            return std::nullopt;
        }

        canonical += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return canonical;
}

// Wraps a canonical UUID string in a Random.Uuid record.  The runtime short name
// "Uuid" matches how the type checker registers the record from
// stdlib_type_arities.cpp; the single field name must match that declaration.
[[nodiscard]] Value make_uuid_record(std::string value) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Uuid";
    rec->fields.emplace_back("value", Value{std::move(value)});

    return Value{std::move(rec)};
}

[[nodiscard]] std::mt19937& thread_local_generator() {
    static thread_local std::mt19937 gen{std::random_device{}()};
    return gen;
}

// Fill a buffer with (non-secure) mt19937 bytes.
void fill_bytes_prng(std::span<unsigned char> bytes) {
    auto& gen = thread_local_generator();
    std::uniform_int_distribution<unsigned> byte_dist{0, 255};

    for (auto& b : bytes) {
        b = static_cast<unsigned char>(byte_dist(gen));
    }
}

// Draw a uniform index in [0, n) from the thread-local mt19937. Requires n > 0.
[[nodiscard]] std::size_t random_index(std::size_t n) {
    std::uniform_int_distribution<std::size_t> dist{0, n - 1};

    return dist(thread_local_generator());
}

// Draw a uniform value in the half-open (0, 1] interval, i.e. excluding 0.
// Box–Muller and the exponential inverse-transform both take a log() of a
// draw, so the draw must never be exactly 0; std::uniform_real_distribution's
// [0, 1) range is flipped via `1.0 - dist(gen)` to move the excluded endpoint
// from 0 to 1 instead.
[[nodiscard]] double draw_open_unit_interval() {
    std::uniform_real_distribution<double> dist{0.0, 1.0};

    return 1.0 - dist(thread_local_generator());
}

#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS

// Thread-local CSPRNG context backed by Mbed TLS AES-CTR-DRBG.
struct CsprngContext {
    mbedtls_ctr_drbg_context ctr_drbg{};
    mbedtls_entropy_context entropy{};
    bool initialised{false};

    CsprngContext() {
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_entropy_init(&entropy);
    }

    ~CsprngContext() {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }

    CsprngContext(const CsprngContext&) = delete;
    CsprngContext& operator=(const CsprngContext&) = delete;

    [[nodiscard]] bool ensure_seeded() {
        if (initialised) {
            return true;
        }

        static constexpr unsigned char pers[] = "luma_csprng";

        if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, pers,
                                  sizeof(pers) - 1) != 0) {
            return false;
        }

        initialised = true;

        return true;
    }

    // Fill a buffer with cryptographically secure random bytes.
    [[nodiscard]] bool generate(unsigned char* buf, std::size_t len) {
        if (!ensure_seeded()) {
            return false;
        }

        return mbedtls_ctr_drbg_random(&ctr_drbg, buf, len) == 0;
    }
};

[[nodiscard]] CsprngContext& thread_local_csprng() {
    static thread_local CsprngContext ctx;
    return ctx;
}

#endif // LUMA_HAS_TLS

// ── Secure-RNG seam ────────────────────────────────────────
// Each helper owns the single TLS #if so the secure_* bodies stay free of
// conditional compilation. secure_generate and secure_failure are defined in
// both builds: when TLS is disabled the former always fails and the latter
// reports the unavailability failure.

[[nodiscard]] constexpr bool secure_available() {
#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS
    return true;
#else
    return false;
#endif
}

// Fill a buffer with cryptographically secure bytes; returns false when the
// CSPRNG fails or is unavailable (TLS-off build).
[[nodiscard]] bool secure_generate(unsigned char* buf, std::size_t len) {
#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS
    return thread_local_csprng().generate(buf, len);
#else
    (void)buf;
    (void)len;

    return false;
#endif
}

// Build the failure Value for a secure_* function: a CSPRNG runtime failure
// when TLS is available, or a "requires TLS support" message when it is not.
[[nodiscard]] Value secure_failure(std::string_view fn) {
#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS
    return make_failure_value(error_msg("Random", fn, "CSPRNG failure"));
#else
    return make_failure_value(
        error_msg("Random", fn, "requires TLS support (build with LUMA_FEATURE_TLS=ON)"));
#endif
}

} // namespace

void register_random_ns(const EnvPtr& env) {
    ModuleBuilder{"Random", env}
        .func("generate_number", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            std::uniform_real_distribution<double> dist{0.0, 1.0};

            return Value{dist(thread_local_generator())};
        })
        .func("generate_integer", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto lo = expect_integer(args[0], "Random.generate_integer", loc);
            const auto hi = expect_integer(args[1], "Random.generate_integer", loc);

            if (lo > hi) {
                return make_failure_value(
                    error_msg("Random", "generate_integer", "lo must be <= hi"));
            }

            auto& gen = thread_local_generator();

            std::int64_t value{};

            const bool ok = bounded_uniform(
                lo, hi,
                [&gen](std::uint64_t& raw) {
                    // Compose a full 64-bit draw from two 32-bit mt19937 outputs.
                    const std::uint64_t high = gen();
                    const std::uint64_t low = gen();
                    raw = (high << 32) | low;

                    return true;
                },
                value);

            if (!ok) {
                return make_failure_value(
                    error_msg("Random", "generate_integer", "generation failed"));
            }

            return make_success_value(Value{value});
        })
        // Random.set_seed(seed) -> none
        // Seeds the (already thread-local) non-secure PRNG so subsequent
        // generate_* / shuffle / sample draws are reproducible.  The secure_*
        // family stays entropy-seeded and is unaffected.
        .func("set_seed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto seed = expect_integer(args[0], "Random.set_seed", loc);

            thread_local_generator().seed(
                static_cast<std::mt19937::result_type>(static_cast<std::uint64_t>(seed)));

            return Value{NullValue{}};
        })
        // Random.generate_bytes(n) -> result<array<integer>>
        // n fast, non-secure bytes, each in [0, 255].  Fails on a negative count.
        .func("generate_bytes", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "Random.generate_bytes", loc);

            if (n < 0) {
                return make_failure_value(
                    error_msg("Random", "generate_bytes", "count must be >= 0"));
            }

            if (static_cast<std::uint64_t>(n) > ResourceLimits::max_array_size) {
                return make_failure_value(
                    error_msg("Random", "generate_bytes", "count exceeds maximum array size"));
            }

            auto& gen = thread_local_generator();
            std::uniform_int_distribution<int> dist{0, 255};

            auto arr = std::make_shared<ArrayValue>();
            arr->elements->reserve(static_cast<std::size_t>(n));

            for (std::int64_t i{0}; i < n; ++i) {
                arr->elements->emplace_back(static_cast<std::int64_t>(dist(gen)));
            }

            return make_success_value(Value{std::move(arr)});
        })
        .func("generate_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto len_int = expect_integer(args[0], "Random.generate_string", loc);

            if (auto fail = check_string_length(len_int, "generate_string")) {
                return *std::move(fail);
            }

            const auto len = static_cast<std::size_t>(len_int);

            std::string result{};
            result.reserve(len);

            std::generate_n(std::back_inserter(result), len, []() {
                return alphanumeric_chars[random_index(alphanumeric_chars.size())];
            });

            return make_success_value(Value{std::move(result)});
        })
        .func("choice", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Random.choice", loc)->elements;

            if (elems.empty()) {
                return make_failure_value(error_msg("Random", "choice", "empty array"));
            }

            return make_success_value(elems[random_index(elems.size())]);
        })
        .func("shuffle", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto arr = std::make_shared<ArrayValue>();
            *arr->elements = *expect_array(args[0], "Random.shuffle", loc)->elements;

            std::ranges::shuffle(*arr->elements, thread_local_generator());

            return Value{std::move(arr)};
        })
        .func("generate_boolean", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            std::uniform_int_distribution<int> dist{0, 1};

            return Value{dist(thread_local_generator()) == 1};
        })
        .func("sample", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& src_arr = expect_array(args[0], "Random.sample", loc);
            const auto n_raw = expect_integer(args[1], "Random.sample", loc);

            if (n_raw < 0) {
                return make_failure_value(
                    error_msg("Random", "sample", "count must be non-negative"));
            }

            const auto& elems = *src_arr->elements;

            auto n = static_cast<std::size_t>(n_raw);

            if (elems.empty() || n == 0) {
                return make_success_value(Value{std::make_shared<ArrayValue>()});
            }

            if (n > elems.size()) {
                return make_failure_value(
                    error_msg("Random", "sample",
                              std::format("count {} exceeds array length {}", n, elems.size())));
            }

            // Fisher-Yates partial shuffle on a copy.
            auto arr = std::make_shared<ArrayValue>();
            auto copy = elems;

            for (std::size_t i{0}; i < n; ++i) {
                std::swap(copy[i], copy[i + random_index(copy.size() - i)]);

                arr->elements->push_back(copy[i]);
            }

            return make_success_value(Value{std::move(arr)});
        })
        .func("sample_from", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_choice()) {
                throw RuntimeError{"Random.sample_from: expected a Random.Distribution", loc,
                                   "pass Random.Distribution.Uniform/Normal/Exponential"};
            }

            const auto& choice = *args[0].as_choice();

            if (choice.variant == "Uniform") {
                const auto low = expect_numeric(choice.fields[0], "Random.sample_from", loc);
                const auto high = expect_numeric(choice.fields[1], "Random.sample_from", loc);

                if (high < low) {
                    return make_failure_value(
                        error_msg("Random", "sample_from", "Uniform requires high >= low"));
                }

                std::uniform_real_distribution<double> dist{low, high};

                return make_success_value(Value{dist(thread_local_generator())});
            }

            if (choice.variant == "Normal") {
                const auto mean = expect_numeric(choice.fields[0], "Random.sample_from", loc);
                const auto standard_deviation =
                    expect_numeric(choice.fields[1], "Random.sample_from", loc);

                if (standard_deviation <= 0.0) {
                    return make_failure_value(error_msg("Random", "sample_from",
                                                        "Normal requires standard_deviation > 0"));
                }

                // Box–Muller transform: two independent uniform draws on (0, 1]
                // produce a standard-normal deviate, which is then scaled and
                // shifted to the requested mean / standard deviation.
                const auto u1 = draw_open_unit_interval();
                const auto u2 = draw_open_unit_interval();
                const auto z0 =
                    std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2);

                return make_success_value(Value{mean + z0 * standard_deviation});
            }

            if (choice.variant == "Exponential") {
                const auto rate = expect_numeric(choice.fields[0], "Random.sample_from", loc);

                if (rate <= 0.0) {
                    return make_failure_value(
                        error_msg("Random", "sample_from", "Exponential requires rate > 0"));
                }

                // Inverse-transform sampling: -ln(U) / rate, U uniform on (0, 1].
                const auto u = draw_open_unit_interval();

                return make_success_value(Value{-std::log(u) / rate});
            }

            throw RuntimeError{"Random.sample_from: unknown Random.Distribution variant", loc,
                               "use Random.Distribution.Uniform/Normal/Exponential"};
        })
        .func("generate_uuid", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            std::array<unsigned char, 16> bytes{};

            // Prefer the CSPRNG so identifiers aren't predictable from RNG
            // state; fall back to mt19937 when it is unavailable or fails.
            if (!secure_generate(bytes.data(), bytes.size())) {
                fill_bytes_prng(bytes);
            }

            return Value{format_uuid_bytes(bytes)};
        })
        // Random.uuid_typed() -> Random.Uuid
        // Typed counterpart of generate_uuid: returns the canonical UUID string
        // wrapped in a Random.Uuid record so a UUID is a distinct, validated type
        // rather than a bare string.  Like generate_uuid it never fails (it falls
        // back to the PRNG when the CSPRNG is unavailable), so it returns the
        // record directly rather than a result.
        .func("uuid_typed", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            std::array<unsigned char, 16> bytes{};

            if (!secure_generate(bytes.data(), bytes.size())) {
                fill_bytes_prng(bytes);
            }

            return make_uuid_record(format_uuid_bytes(bytes));
        })
        // Random.parse_uuid(uuid) -> result<Random.Uuid>
        // Validates a canonical 8-4-4-4-12 UUID string and wraps it in a
        // Random.Uuid record, failing for any non-canonical input.  The stored
        // value is lower-cased so equal UUIDs compare equal regardless of the
        // input's letter case.
        .func("parse_uuid", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& text = expect_string(args[0], "Random.parse_uuid", loc);

            auto canonical = canonicalize_uuid(text);

            if (!canonical) {
                return make_failure_value(
                    error_msg("Random", "parse_uuid", "not a canonical 8-4-4-4-12 UUID string"),
                    std::string{"parse_error"}, "Random.parse_uuid");
            }

            return make_success_value(make_uuid_record(std::move(*canonical)));
        })
        // Random.uuid_to_string(uuid) -> string
        // Reads the canonical string back out of a Random.Uuid record.
        .func("uuid_to_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record() || args[0].as_record()->type_name != "Uuid") {
                throw RuntimeError{"Random.uuid_to_string: expected a Random.Uuid record", loc,
                                   "build one with Random.uuid_typed() or Random.parse_uuid(s)"};
            }

            const Value* value = args[0].as_record()->find_field("value");

            if (value == nullptr || !value->is_string()) {
                throw RuntimeError{"Random.uuid_to_string: expected a Random.Uuid record", loc,
                                   "build one with Random.uuid_typed() or Random.parse_uuid(s)"};
            }

            return Value{value->as_string()};
        })
        // ── Cryptographically secure variants ──────────────
        .func("secure_boolean", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            unsigned char byte{};

            if (!secure_generate(&byte, 1)) {
                return secure_failure("secure_boolean");
            }

            return make_success_value(Value{(byte & 1) == 1});
        })
        .func("secure_integer", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto lo = expect_integer(args[0], "Random.secure_integer", loc);
            const auto hi = expect_integer(args[1], "Random.secure_integer", loc);

            if (lo > hi) {
                return make_failure_value(
                    error_msg("Random", "secure_integer", "lo must be <= hi"));
            }

            // bounded_uniform short-circuits a singleton range (lo == hi)
            // without consulting the generator, so guard explicitly to keep
            // secure_integer failing when the CSPRNG is unavailable.
            if (!secure_available()) {
                return secure_failure("secure_integer");
            }

            std::int64_t value{};

            const bool ok = bounded_uniform(
                lo, hi,
                [](std::uint64_t& raw) {
                    return secure_generate(reinterpret_cast<unsigned char*>(&raw), sizeof(raw));
                },
                value);

            if (!ok) {
                return secure_failure("secure_integer");
            }

            return make_success_value(Value{value});
        })
        .func("secure_number", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            std::uint64_t raw{};

            if (!secure_generate(reinterpret_cast<unsigned char*>(&raw), sizeof(raw))) {
                return secure_failure("secure_number");
            }

            // Map 64-bit integer to [0, 1) using the standard
            // divide-by-(max+1) approach.
            constexpr auto divisor = static_cast<double>(UINT64_MAX) + 1.0;
            const auto value = static_cast<double>(raw) / divisor;

            return make_success_value(Value{value});
        })
        .func("secure_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto len_int = expect_integer(args[0], "Random.secure_string", loc);

            if (auto fail = check_string_length(len_int, "secure_string")) {
                return *std::move(fail);
            }

            // The empty-string case (len == 0) never consults the generator, so
            // guard explicitly to keep secure_string failing when the CSPRNG is
            // unavailable.
            if (!secure_available()) {
                return secure_failure("secure_string");
            }

            const auto len = static_cast<std::size_t>(len_int);

            // Rejection sampling: 248 is the largest multiple of 62 (charset size) that fits in a byte.
            constexpr unsigned int rejection_sample_max = 248;

            // Generate random bytes in batches and use rejection
            // sampling to avoid modulo bias (62 chars, 256-byte space).
            std::string result;
            result.reserve(len);

            // Over-allocate to account for rejection (~3.2% of bytes
            // are rejected, so ~4% overhead is safe).
            while (result.size() < len) {
                const auto remaining = len - result.size();
                const auto batch_size = remaining + remaining / 16 + 16;

                std::vector<unsigned char> buf(batch_size);

                if (!secure_generate(buf.data(), buf.size())) {
                    return secure_failure("secure_string");
                }

                for (std::size_t i{0}; i < buf.size() && result.size() < len; ++i) {
                    // Reject values >= rejection_sample_max to eliminate modulo bias
                    // (rejection_sample_max is the largest multiple of 62 <= 256).
                    if (buf[i] < rejection_sample_max) {
                        result += alphanumeric_chars[buf[i] % alphanumeric_chars.size()];
                    }
                }
            }

            return make_success_value(Value{std::move(result)});
        })
        .func("secure_uuid", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            std::array<unsigned char, 16> bytes{};

            if (!secure_generate(bytes.data(), bytes.size())) {
                return secure_failure("secure_uuid");
            }

            return make_success_value(Value{format_uuid_bytes(bytes)});
        })
        // Random.secure_bytes(n) -> result<array<integer>>
        // n cryptographically-secure bytes, each in [0, 255].  The correct source
        // for salts, keys, and nonces.  Fails on a negative count or when the
        // CSPRNG is unavailable.
        .func("secure_bytes", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "Random.secure_bytes", loc);

            if (n < 0) {
                return make_failure_value(
                    error_msg("Random", "secure_bytes", "count must be >= 0"));
            }

            if (static_cast<std::uint64_t>(n) > ResourceLimits::max_array_size) {
                return make_failure_value(
                    error_msg("Random", "secure_bytes", "count exceeds maximum array size"));
            }

            if (!secure_available()) {
                return secure_failure("secure_bytes");
            }

            const auto len = static_cast<std::size_t>(n);
            std::vector<unsigned char> buf(len);

            if (len > 0 && !secure_generate(buf.data(), buf.size())) {
                return secure_failure("secure_bytes");
            }

            auto arr = std::make_shared<ArrayValue>();
            arr->elements->reserve(len);

            for (const auto byte : buf) {
                arr->elements->emplace_back(static_cast<std::int64_t>(byte));
            }

            return make_success_value(Value{std::move(arr)});
        });
}

} // namespace luma
