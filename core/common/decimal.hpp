// Exact base-10 arbitrary-precision decimal arithmetic.
//
// `luma::Decimal` represents a number as sign + integer coefficient + scale,
// where value = (-1)^sign * coefficient * 10^(-scale). Unlike IEEE-754 `number`
// (binary floating point), a Decimal stores base-10 digits exactly, so
// `0.1 + 0.2` is exactly `0.3` and currency arithmetic is correct.
//
// The type is self-contained (no third-party big-number library) and portable:
// the coefficient is a little-endian vector of decimal digits (0-9) and all
// algorithms are grade-school, so nothing depends on compiler `__int128`
// support. Values are immutable — every operation returns a new Decimal.
//
// Fallible operations (parsing, division by zero, converting a non-finite
// double, and multiplication whose product would exceed the digit cap) return
// `std::optional`; the stdlib layer maps `std::nullopt` to a Luma `result`
// failure or a runtime error. Infallible operations (add, subtract, round,
// compare, negate) return a Decimal directly.

#ifndef LUMA_COMMON_DECIMAL_HPP
#define LUMA_COMMON_DECIMAL_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luma {

// Rounding strategies used by `round()` and `divide()`. The half-* modes only
// differ when the discarded part is exactly one half of the last kept unit.
enum class RoundingMode : std::uint8_t {
    HalfUp,   // Nearest; ties away from zero (2.5 -> 3, -2.5 -> -3). "Commercial".
    HalfEven, // Nearest; ties to the even digit (2.5 -> 2, 3.5 -> 4). "Banker's".
    HalfDown, // Nearest; ties toward zero (2.5 -> 2, -2.5 -> -2).
    Up,       // Away from zero (any non-zero remainder rounds up in magnitude).
    Down,     // Toward zero (truncate; drop the remainder).
    Ceiling,  // Toward +infinity (up for positives, truncate for negatives).
    Floor,    // Toward -infinity (down for positives, away for negatives).
};

// Maps a lowercase rounding-mode name to its enum value. Recognised names:
// "half_up", "half_even", "half_down", "up", "down", "ceiling", "floor".
// Returns std::nullopt for an unknown name.
[[nodiscard]] std::optional<RoundingMode> parse_rounding_mode(std::string_view name);

class Decimal {
public:
    // Constructs zero (coefficient 0, scale 0).
    Decimal() = default;

    // Constructs an exact Decimal from a 64-bit integer (scale 0).
    explicit Decimal(std::int64_t value);

    // Parses a decimal literal: optional sign, integer and/or fraction digits,
    // and an optional exponent, e.g. "-123.45", ".5", "10.", "1e3", "2.5E-4".
    // Whitespace is not permitted. Returns std::nullopt for malformed input or
    // for a magnitude so large it would exceed the internal digit cap.
    [[nodiscard]] static std::optional<Decimal> parse(std::string_view text);

    // Converts a finite double using its shortest round-tripping base-10 form,
    // so `from_double(0.1)` is exactly `0.1` (not the raw binary expansion).
    // Returns std::nullopt for NaN or infinity.
    [[nodiscard]] static std::optional<Decimal> from_double(double value);

    // Renders the value in plain (non-exponential) notation, e.g. "1.50",
    // "-0.001", "0", "42". Negative zero is never produced.
    [[nodiscard]] std::string to_string() const;

    // Converts to the nearest double. Very large magnitudes yield +/-infinity.
    [[nodiscard]] double to_double() const;

    [[nodiscard]] Decimal add(const Decimal& other) const;
    [[nodiscard]] Decimal subtract(const Decimal& other) const;

    // Multiplies two values exactly. The product's coefficient has at most the
    // sum of the operand digit counts and its scale is the sum of the operand
    // scales, so a chain of multiplications can grow without bound. Returns
    // std::nullopt when the product would exceed the internal digit cap — which
    // also keeps the scale sum from overflowing the `int` scale field.
    [[nodiscard]] std::optional<Decimal> multiply(const Decimal& other) const;

    // Divides by `divisor` producing a result with exactly `result_scale`
    // fractional digits, rounding the final digit with `mode`. Returns
    // std::nullopt if `divisor` is zero or `result_scale` is negative.
    [[nodiscard]] std::optional<Decimal> divide(const Decimal& divisor, int result_scale,
                                                RoundingMode mode) const;

    // Returns the value rounded to exactly `places` fractional digits using
    // `mode`. `places` is clamped to >= 0 (a negative request rounds to the
    // integer, i.e. 0 places).
    [[nodiscard]] Decimal round(int places, RoundingMode mode) const;

    // Three-way value comparison (scale-insensitive): 1.5 and 1.50 compare
    // equal. Returns -1, 0, or 1.
    [[nodiscard]] int compare(const Decimal& other) const;

    [[nodiscard]] bool equals(const Decimal& other) const {
        return compare(other) == 0;
    }

    [[nodiscard]] Decimal negate() const;
    [[nodiscard]] Decimal absolute() const;

    [[nodiscard]] bool is_zero() const {
        return digits_.empty();
    }

    [[nodiscard]] bool is_negative() const {
        return negative_ && !is_zero();
    }

    // Sign of the value: -1, 0, or 1.
    [[nodiscard]] int sign() const;

    // Number of fractional digits currently stored (the scale of this value).
    [[nodiscard]] int scale() const {
        return scale_;
    }

    // Value-based hash: equal values (regardless of scale) hash identically,
    // preserving the hash/equality invariant for use as dictionary/set keys.
    [[nodiscard]] std::size_t hash() const;

    // Canonical form with insignificant trailing fractional zeros removed
    // (1.50 -> 1.5, 100 -> 100). Exposed mainly for testing.
    [[nodiscard]] Decimal canonical() const;

    // Upper bound on the number of coefficient digits any single value may hold.
    // Bounds memory use when parsing exponents or multiplying.
    static constexpr std::size_t k_max_digits = 1'000'000;

private:
    // Coefficient digits, least-significant first, each in [0,9], with no
    // most-significant trailing zeros. An empty vector represents the value 0.
    std::vector<std::uint8_t> digits_;
    // Number of fractional digits; always >= 0. value = coeff * 10^(-scale_).
    int scale_ = 0;
    // True when the value is strictly negative. Zero is stored as non-negative.
    bool negative_ = false;

    // Removes most-significant trailing zeros and normalises zero to the
    // canonical empty/non-negative representation.
    void trim();
};

} // namespace luma

#endif // LUMA_COMMON_DECIMAL_HPP
