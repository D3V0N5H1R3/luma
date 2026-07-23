#include "decimal.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace luma {

namespace {

using Mag = std::vector<std::uint8_t>; // little-endian base-10 magnitude, digits 0-9

// Removes most-significant (back) zero digits so a magnitude has a unique form.
void mag_trim(Mag& v) {
    while (!v.empty() && v.back() == 0) {
        v.pop_back();
    }
}

// Compares two trimmed magnitudes. Returns -1, 0, or 1.
int mag_compare(const Mag& a, const Mag& b) {
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    for (std::size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

// Returns a + b.
Mag mag_add(const Mag& a, const Mag& b) {
    Mag result;
    const std::size_t n = std::max(a.size(), b.size());
    result.reserve(n + 1);
    int carry = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const int sum = carry + (i < a.size() ? a[i] : 0) + (i < b.size() ? b[i] : 0);
        result.push_back(static_cast<std::uint8_t>(sum % 10));
        carry = sum / 10;
    }
    if (carry != 0) {
        result.push_back(static_cast<std::uint8_t>(carry));
    }
    return result;
}

// Returns a - b, assuming a >= b.
Mag mag_sub(const Mag& a, const Mag& b) {
    Mag result;
    result.reserve(a.size());
    int borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        int digit = a[i] - borrow - (i < b.size() ? b[i] : 0);
        if (digit < 0) {
            digit += 10;
            borrow = 1;
        } else {
            borrow = 0;
        }
        result.push_back(static_cast<std::uint8_t>(digit));
    }
    mag_trim(result);
    return result;
}

// Returns a * b using schoolbook multiplication.
Mag mag_mul(const Mag& a, const Mag& b) {
    if (a.empty() || b.empty()) {
        return {};
    }
    Mag result(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        int carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const std::size_t idx = i + j;
            const int current = result[idx] + a[i] * b[j] + carry;
            result[idx] = static_cast<std::uint8_t>(current % 10);
            carry = current / 10;
        }
        std::size_t idx = i + b.size();
        while (carry != 0) {
            const int current = result[idx] + carry;
            result[idx] = static_cast<std::uint8_t>(current % 10);
            carry = current / 10;
            ++idx;
        }
    }
    mag_trim(result);
    return result;
}

// Returns a * d for a single factor d in [0, 9].
Mag mag_mul_small(const Mag& a, int factor) {
    if (factor == 0 || a.empty()) {
        return {};
    }
    Mag result;
    result.reserve(a.size() + 1);
    int carry = 0;
    for (const std::uint8_t digit : a) {
        const int current = digit * factor + carry;
        result.push_back(static_cast<std::uint8_t>(current % 10));
        carry = current / 10;
    }
    while (carry != 0) {
        result.push_back(static_cast<std::uint8_t>(carry % 10));
        carry /= 10;
    }
    return result;
}

// Multiplies a magnitude by 10^shift (appends `shift` zeros at the low end).
Mag mag_shift_left(Mag value, long long shift) {
    if (value.empty() || shift <= 0) {
        return value;
    }
    value.insert(value.begin(), static_cast<std::size_t>(shift), 0);
    return value;
}

// Long division: computes quotient and remainder of numerator / divisor.
// `divisor` must be non-zero.
void mag_divmod(const Mag& numerator, const Mag& divisor, Mag& quotient, Mag& remainder) {
    quotient.assign(numerator.size(), 0);
    Mag rem;
    for (std::size_t i = numerator.size(); i-- > 0;) {
        rem.insert(rem.begin(), numerator[i]); // rem = rem * 10 + next digit
        mag_trim(rem);
        int low = 0;
        int high = 9;
        int best = 0;
        while (low <= high) {
            const int mid = (low + high) / 2;
            if (mag_compare(mag_mul_small(divisor, mid), rem) <= 0) {
                best = mid;
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        quotient[i] = static_cast<std::uint8_t>(best);
        rem = mag_sub(rem, mag_mul_small(divisor, best));
    }
    mag_trim(quotient);
    remainder = rem;
}

// Decides whether the kept coefficient should be incremented by one, given the
// discarded part. `cmp_half` compares the discarded part against exactly one
// half (-1 below, 0 exactly, 1 above); `any_remainder` is true when anything
// was discarded; `last_kept_odd` is the parity of the last surviving digit.
bool apply_rounding(bool any_remainder, int cmp_half, bool last_kept_odd, bool negative,
                    RoundingMode mode) {
    switch (mode) {
        case RoundingMode::Down:
            return false;
        case RoundingMode::Up:
            return any_remainder;
        case RoundingMode::Ceiling:
            return any_remainder && !negative;
        case RoundingMode::Floor:
            return any_remainder && negative;
        case RoundingMode::HalfUp:
            return cmp_half >= 0;
        case RoundingMode::HalfDown:
            return cmp_half > 0;
        case RoundingMode::HalfEven:
            if (cmp_half > 0) {
                return true;
            }
            if (cmp_half == 0) {
                return last_kept_odd;
            }
            return false;
    }
    return false;
}

} // namespace

std::optional<RoundingMode> parse_rounding_mode(std::string_view name) {
    if (name == "half_up") {
        return RoundingMode::HalfUp;
    }
    if (name == "half_even") {
        return RoundingMode::HalfEven;
    }
    if (name == "half_down") {
        return RoundingMode::HalfDown;
    }
    if (name == "up") {
        return RoundingMode::Up;
    }
    if (name == "down") {
        return RoundingMode::Down;
    }
    if (name == "ceiling") {
        return RoundingMode::Ceiling;
    }
    if (name == "floor") {
        return RoundingMode::Floor;
    }
    return std::nullopt;
}

std::optional<RoundingMode> rounding_mode_from_variant(std::string_view variant) {
    if (variant == "HalfUp") {
        return RoundingMode::HalfUp;
    }
    if (variant == "HalfEven") {
        return RoundingMode::HalfEven;
    }
    if (variant == "HalfDown") {
        return RoundingMode::HalfDown;
    }
    if (variant == "Up") {
        return RoundingMode::Up;
    }
    if (variant == "Down") {
        return RoundingMode::Down;
    }
    if (variant == "Ceiling") {
        return RoundingMode::Ceiling;
    }
    if (variant == "Floor") {
        return RoundingMode::Floor;
    }
    return std::nullopt;
}

Decimal::Decimal(std::int64_t value) {
    if (value == 0) {
        return;
    }
    negative_ = value < 0;
    // Compute magnitude via unsigned arithmetic so INT64_MIN does not overflow.
    std::uint64_t magnitude = negative_ ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                                        : static_cast<std::uint64_t>(value);
    while (magnitude > 0) {
        digits_.push_back(static_cast<std::uint8_t>(magnitude % 10));
        magnitude /= 10;
    }
}

void Decimal::trim() {
    while (!digits_.empty() && digits_.back() == 0) {
        digits_.pop_back();
    }
    if (digits_.empty()) {
        negative_ = false;
    }
}

std::optional<Decimal> Decimal::parse(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::size_t i = 0;
    bool negative = false;
    if (text[i] == '+' || text[i] == '-') {
        negative = text[i] == '-';
        ++i;
    }

    std::string integer_part;
    std::string fraction_part;
    bool any_digit = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        integer_part.push_back(text[i]);
        ++i;
        any_digit = true;
    }
    int fraction_scale = 0;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            fraction_part.push_back(text[i]);
            ++i;
            any_digit = true;
            ++fraction_scale;
        }
    }
    if (!any_digit) {
        return std::nullopt;
    }

    long long exponent = 0;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        bool exp_negative = false;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            exp_negative = text[i] == '-';
            ++i;
        }
        if (i >= text.size() || text[i] < '0' || text[i] > '9') {
            return std::nullopt;
        }
        long long magnitude = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            magnitude = magnitude * 10 + (text[i] - '0');
            ++i;
            if (magnitude > 2'000'000'000LL) {
                return std::nullopt; // absurd exponent
            }
        }
        exponent = exp_negative ? -magnitude : magnitude;
    }
    if (i != text.size()) {
        return std::nullopt; // trailing garbage
    }

    // Coefficient digits, most-significant first, then apply the exponent to the
    // fractional scale: multiplying by 10^exponent lowers the scale.
    std::string combined = integer_part + fraction_part;
    // Bound the coefficient length so a pathologically long literal cannot build
    // an over-cap value. This covers the plain-mantissa path; the negative-scale
    // branch below applies its own check once it knows how many zeros to append.
    if (combined.size() > k_max_digits) {
        return std::nullopt;
    }
    long long scale = static_cast<long long>(fraction_scale) - exponent;

    Decimal result;
    result.digits_.reserve(combined.size());
    for (auto it = combined.rbegin(); it != combined.rend(); ++it) {
        result.digits_.push_back(static_cast<std::uint8_t>(*it - '0'));
    }
    if (scale < 0) {
        // Negative scale means an integer value with trailing zeros: fold the
        // factor of 10^(-scale) into the coefficient and normalise scale to 0.
        const long long extra = -scale;
        if (result.digits_.size() + static_cast<std::size_t>(extra) > k_max_digits) {
            return std::nullopt;
        }
        result.digits_.insert(result.digits_.begin(), static_cast<std::size_t>(extra), 0);
        scale = 0;
    } else if (scale > static_cast<long long>(k_max_digits)) {
        return std::nullopt; // scale too large to represent sensibly
    }
    result.scale_ = static_cast<int>(scale);
    result.negative_ = negative;
    result.trim();
    return result;
}

std::optional<Decimal> Decimal::from_double(double value) {
    if (std::isnan(value) || std::isinf(value)) {
        return std::nullopt;
    }
    constexpr std::size_t buffer_size = 64;
    char buffer[buffer_size];
    const auto conversion = std::to_chars(buffer, buffer + buffer_size, value);
    if (conversion.ec != std::errc()) {
        return std::nullopt;
    }
    return parse(std::string_view(buffer, static_cast<std::size_t>(conversion.ptr - buffer)));
}

std::string Decimal::to_string() const {
    if (is_zero()) {
        if (scale_ <= 0) {
            return "0";
        }
        std::string zero = "0.";
        zero.append(static_cast<std::size_t>(scale_), '0');
        return zero;
    }

    std::string magnitude;
    magnitude.reserve(digits_.size());
    for (auto it = digits_.rbegin(); it != digits_.rend(); ++it) {
        magnitude.push_back(static_cast<char>('0' + *it));
    }

    std::string out;
    if (negative_) {
        out.push_back('-');
    }
    if (scale_ == 0) {
        out += magnitude;
        return out;
    }
    if (static_cast<int>(magnitude.size()) <= scale_) {
        magnitude.insert(magnitude.begin(), static_cast<std::size_t>(scale_) - magnitude.size() + 1,
                         '0');
    }
    const std::size_t point = magnitude.size() - static_cast<std::size_t>(scale_);
    out.append(magnitude, 0, point);
    out.push_back('.');
    out.append(magnitude, point, std::string::npos);
    return out;
}

double Decimal::to_double() const {
    const std::string text = to_string();
    return std::strtod(text.c_str(), nullptr);
}

Decimal Decimal::add(const Decimal& other) const {
    const int common_scale = std::max(scale_, other.scale_);
    Mag left = mag_shift_left(digits_, common_scale - scale_);
    Mag right = mag_shift_left(other.digits_, common_scale - other.scale_);

    Decimal result;
    if (negative_ == other.negative_) {
        result.digits_ = mag_add(left, right);
        result.negative_ = negative_;
    } else {
        const int cmp = mag_compare(left, right);
        if (cmp >= 0) {
            result.digits_ = mag_sub(left, right);
            result.negative_ = negative_;
        } else {
            result.digits_ = mag_sub(right, left);
            result.negative_ = other.negative_;
        }
    }
    result.scale_ = common_scale;
    result.trim();
    return result;
}

Decimal Decimal::subtract(const Decimal& other) const {
    return add(other.negate());
}

std::optional<Decimal> Decimal::multiply(const Decimal& other) const {
    // The product has at most len(a)+len(b) coefficient digits and a scale equal
    // to the sum of the operand scales. Bound both by k_max_digits: this caps
    // memory use (schoolbook mag_mul is O(n*m)) and, crucially, keeps the scale
    // sum from overflowing the `int` scale field. Repeated squaring of a tiny-
    // coefficient, huge-scale value (e.g. 1e-1000000, scale 1'000'000) would
    // otherwise double the scale each step until it wraps negative — undefined
    // behaviour that corrupts every downstream operation.
    if (digits_.size() + other.digits_.size() > k_max_digits) {
        return std::nullopt;
    }
    const std::size_t product_scale =
        static_cast<std::size_t>(scale_) + static_cast<std::size_t>(other.scale_);
    if (product_scale > k_max_digits) {
        return std::nullopt;
    }

    Decimal result;
    result.digits_ = mag_mul(digits_, other.digits_);
    result.scale_ = static_cast<int>(product_scale);
    result.negative_ = negative_ != other.negative_;
    result.trim();
    return result;
}

std::optional<Decimal> Decimal::divide(const Decimal& divisor, int result_scale,
                                       RoundingMode mode) const {
    if (divisor.is_zero() || result_scale < 0) {
        return std::nullopt;
    }
    const bool result_negative = negative_ != divisor.negative_;

    // value = (numerator / divisor); express the quotient with `result_scale`
    // fractional digits by scaling the numerator (or divisor) by 10^|e|.
    const long long exp = static_cast<long long>(result_scale) + divisor.scale_ - scale_;
    Mag numerator = digits_;
    Mag denominator = divisor.digits_;
    if (exp >= 0) {
        if (numerator.size() + static_cast<std::size_t>(exp) > k_max_digits) {
            return std::nullopt;
        }
        numerator = mag_shift_left(numerator, exp);
    } else {
        const long long shift = -exp;
        if (denominator.size() + static_cast<std::size_t>(shift) > k_max_digits) {
            return std::nullopt;
        }
        denominator = mag_shift_left(denominator, shift);
    }

    Mag quotient;
    Mag remainder;
    mag_divmod(numerator, denominator, quotient, remainder);

    int cmp_half = -1;
    bool any_remainder = false;
    if (!remainder.empty()) {
        any_remainder = true;
        Mag doubled = mag_mul_small(remainder, 2);
        cmp_half = mag_compare(doubled, denominator);
    }
    const bool last_odd = !quotient.empty() && (quotient.front() % 2 == 1);
    if (apply_rounding(any_remainder, cmp_half, last_odd, result_negative, mode)) {
        quotient = mag_add(quotient, Mag{1});
    }

    Decimal result;
    result.digits_ = std::move(quotient);
    result.scale_ = result_scale;
    result.negative_ = result_negative;
    result.trim();
    return result;
}

Decimal Decimal::round(int places, RoundingMode mode) const {
    if (places < 0) {
        places = 0;
    }
    if (places > static_cast<int>(k_max_digits)) {
        places = static_cast<int>(k_max_digits);
    }

    if (places >= scale_) {
        Decimal result = *this;
        if (places > scale_ && !result.digits_.empty()) {
            result.digits_ = mag_shift_left(result.digits_, places - scale_);
        }
        result.scale_ = places;
        result.trim();
        return result;
    }

    const int drop = scale_ - places;
    const std::size_t size = digits_.size();
    const std::size_t drop_count = static_cast<std::size_t>(drop);

    // Digits at index >= size are implicit high-order zeros, so when `drop`
    // exceeds the stored digit count every stored digit is discarded and the
    // kept part is empty (the value rounds toward zero at this coarser scale).
    Mag kept;
    if (drop_count < size) {
        kept.assign(digits_.begin() + drop, digits_.end());
    }

    // Inspect the discarded low-order digits to classify the remainder. The
    // digit just below the rounding point sits at index drop-1 (drop >= 1 here);
    // any index at or beyond the stored digits is an implicit zero.
    const int first_dropped = (drop_count - 1 < size) ? digits_[drop_count - 1] : 0;
    bool rest_nonzero = false;
    for (std::size_t k = 0; k + 1 < drop_count && k < size; ++k) {
        if (digits_[k] != 0) {
            rest_nonzero = true;
            break;
        }
    }
    const bool any_remainder = first_dropped != 0 || rest_nonzero;
    int cmp_half = -1;
    if (first_dropped > 5 || (first_dropped == 5 && rest_nonzero)) {
        cmp_half = 1;
    } else if (first_dropped == 5) {
        cmp_half = 0;
    }
    const bool last_odd = !kept.empty() && (kept.front() % 2 == 1);
    if (apply_rounding(any_remainder, cmp_half, last_odd, negative_, mode)) {
        kept = mag_add(kept, Mag{1});
    }

    Decimal result;
    result.digits_ = std::move(kept);
    result.scale_ = places;
    result.negative_ = negative_;
    result.trim();
    return result;
}

int Decimal::compare(const Decimal& other) const {
    const int sign_a = sign();
    const int sign_b = other.sign();
    if (sign_a != sign_b) {
        return sign_a < sign_b ? -1 : 1;
    }
    if (sign_a == 0) {
        return 0;
    }
    const int common_scale = std::max(scale_, other.scale_);
    Mag left = mag_shift_left(digits_, common_scale - scale_);
    Mag right = mag_shift_left(other.digits_, common_scale - other.scale_);
    const int cmp = mag_compare(left, right);
    return sign_a > 0 ? cmp : -cmp;
}

Decimal Decimal::negate() const {
    Decimal result = *this;
    if (!result.is_zero()) {
        result.negative_ = !result.negative_;
    }
    return result;
}

Decimal Decimal::absolute() const {
    Decimal result = *this;
    result.negative_ = false;
    return result;
}

int Decimal::sign() const {
    if (is_zero()) {
        return 0;
    }
    return negative_ ? -1 : 1;
}

Decimal Decimal::canonical() const {
    Decimal result = *this;
    while (result.scale_ > 0 && !result.digits_.empty() && result.digits_.front() == 0) {
        result.digits_.erase(result.digits_.begin());
        --result.scale_;
    }
    if (result.digits_.empty()) {
        result.scale_ = 0;
        result.negative_ = false;
    }
    return result;
}

std::size_t Decimal::hash() const {
    return std::hash<std::string>{}(canonical().to_string());
}

} // namespace luma
