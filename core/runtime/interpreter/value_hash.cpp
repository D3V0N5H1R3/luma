// Structural hash implementation for Value.
//
// This translation unit includes value_collections.hpp to access the full
// definitions of ArrayValue, DictionaryValue, and TupleValue, which are
// incomplete (forward-declared) at the point where ValueHash is declared in
// value_type.hpp.  The split avoids a circular-include dependency:
//
//   value_fwd.hpp   ← forward declarations (ArrayValue, etc.)
//   value_type.hpp  ← Value class + ValueHash declaration (includes value_fwd.hpp)
//   value_collections.hpp ← ArrayValue/DictionaryValue/TupleValue (includes value_type.hpp)
//   value_hash.cpp  ← implementation (includes value_collections.hpp for full types)
//
// ValueHash::operator() in value_type.hpp delegates to hash_value_structural()
// declared here.

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>

#include "common/resource_limits.hpp"
#include "runtime/interpreter/value_collections.hpp"
#include "runtime/interpreter/value_dispatch.hpp"

namespace luma::detail {

namespace {

// ── Hash parameters ──────────────────────────────────────────────────────
// Constants for the boost::hash_combine algorithm (see
// boost.org/libs/container_hash).  Kept as file-local constants rather
// than wrapped in a nested namespace to avoid qualifying every usage with
// a prefix (hash_params::k_mix vs k_hash_mix — no readability gain).

// Golden ratio constant — minimises hash collisions for sequential keys.
constexpr std::size_t k_hash_mix = (sizeof(std::size_t) == 8)
                                       ? std::size_t{0x9e3779b97f4a7c15ULL} // 64-bit golden ratio
                                       : std::size_t{0x9e3779b9UL};         // 32-bit golden ratio
// Bit-shift amounts for the hash_combine mixing function.
constexpr int k_hash_shift_left = 6;
constexpr int k_hash_shift_right = 2;

// ── Depth limit ──────────────────────────────────────────────────────────
// Prevents O(n^depth) cost for pathological inputs in recursive hashing.
constexpr int k_max_hash_depth = CompileTimeLimits::max_hash_depth;

[[nodiscard]] std::size_t hash_combine(std::size_t seed, std::size_t v) noexcept {
    return seed ^ (v + k_hash_mix + (seed << k_hash_shift_left) + (seed >> k_hash_shift_right));
}

} // namespace

// Implemented separately (not as a free function template) because
// it must be recursive and value_type.hpp cannot include this file.
// Every variant access below is guarded by an is_*() check, so the
// bad_variant_access path the analyzer traces is never taken; this function is
// noexcept by contract (the only residual escape is fatal STL bad_alloc).
// NOLINTNEXTLINE(bugprone-exception-escape)
std::size_t hash_value_structural(const Value& v, int depth) noexcept {
    if (v.is_null()) {
        return 0;
    }
    if (v.is_bool()) {
        return std::hash<bool>{}(v.as_bool());
    }
    if (v.is_integer()) {
        // ValueEqual compares an integer against a number by promoting the
        // integer to double via Value::to_numeric() (see equals_numeric in
        // value_equality.cpp), so integer(i) can equal number(double(i)) for
        // ANY i — including |i| >= 2^53, where that promotion is lossy
        // (integer(2^53) and integer(2^53 + 1) both promote to 2^53.0). To keep
        // the hash/equality contract, every integer must therefore hash exactly
        // as its promoted double. Distinct integers that share a promoted value
        // simply collide into the same bucket, which is permitted.
        return std::hash<double>{}(static_cast<double>(v.as_integer()));
    }
    if (v.is_number()) {
        const double d = v.as_number();
        // All NaN values compare equal under ValueEqual (see value_equality.cpp),
        // so they must hash identically to preserve the hash/equality contract.
        // std::hash<double> may distinguish NaN bit patterns, so canonicalise
        // every NaN to a single representative before hashing.
        if (std::isnan(d)) {
            return std::hash<double>{}(std::numeric_limits<double>::quiet_NaN());
        }
        return std::hash<double>{}(d);
    }
    if (v.is_string()) {
        return std::hash<std::string>{}(v.as_string());
    }
    if (v.is_decimal()) {
        // Hash the canonical (trailing-zero-stripped) value so equal decimals of
        // different scale (1.5 and 1.50) hash identically, matching Value::equals.
        return v.as_decimal()->value.hash();
    }

    // Seed with the type tag so that, e.g., empty arrays and empty dicts never collide.
    const auto type_seed = std::hash<int>{}(static_cast<int>(v.value_type()));

    // Depth limit: prevents O(n^k_max_hash_depth) cost for pathological inputs.
    // Values nested beyond this depth are hashed by type tag only.
    if (depth >= k_max_hash_depth) {
        return type_seed;
    }

    if (v.is_tuple() || v.is_array()) {
        std::size_t h =
            hash_combine(type_seed, std::hash<std::size_t>{}(sequence_element_count(v)));
        apply_to_elements(v, [&](const Value& elem) {
            h = hash_combine(h, hash_value_structural(elem, depth + 1));
        });
        return h;
    }

    if (v.is_dictionary()) {
        const auto& entries = v.as_dictionary()->entries;
        // Order-independent: XOR individual entry hashes.
        // This matches the order-independent equality check in value_equality.cpp.
        std::size_t entry_xor = 0;
        for (const auto& [key, val] : entries) {
            auto entry_hash =
                hash_combine(std::hash<std::string>{}(key), hash_value_structural(val, depth + 1));
            entry_xor ^= entry_hash;
        }
        return hash_combine(type_seed,
                            hash_combine(std::hash<std::size_t>{}(entries.size()), entry_xor));
    }

    if (v.is_result()) {
        // Mirror equals_result (value_equality.cpp): only is_success and the
        // wrapped value participate in equality, so the error_code /
        // source_function metadata must NOT be hashed.
        const auto& r = *v.as_result();
        std::size_t h = hash_combine(type_seed, std::hash<bool>{}(r.is_success));
        if (r.owned_inner) {
            h = hash_combine(h, hash_value_structural(*r.owned_inner, depth + 1));
        }
        return h;
    }

    if (v.is_record()) {
        // Mirror equals_record: same type name and positionally-equal fields.
        const auto& rec = *v.as_record();
        std::size_t h = hash_combine(type_seed, std::hash<std::string>{}(rec.type_name));
        for (const auto& [name, val] : rec.fields) {
            h = hash_combine(h, std::hash<std::string>{}(name));
            h = hash_combine(h, hash_value_structural(val, depth + 1));
        }
        return h;
    }

    if (v.is_range()) {
        // Mirror equals_range: start, end, and inclusivity.
        const auto& r = *v.as_range();
        std::size_t h = hash_combine(type_seed, std::hash<std::int64_t>{}(r.start));
        h = hash_combine(h, std::hash<std::int64_t>{}(r.end));
        h = hash_combine(h, std::hash<bool>{}(r.inclusive));
        return h;
    }

    if (v.is_choice()) {
        // Mirror ChoiceValue::operator==: type name, variant, and positional payload.
        const auto& c = *v.as_choice();
        std::size_t h = hash_combine(type_seed, std::hash<std::string>{}(c.type_name));
        h = hash_combine(h, std::hash<std::string>{}(c.variant));
        for (const auto& field : c.fields) {
            h = hash_combine(h, hash_value_structural(field, depth + 1));
        }
        return h;
    }

    // Remaining structured types — collection subtypes (Set/Queue/Stack and the
    // by-reference Xml/KeyValueStore/BinaryTree), plus functions, channels, tasks,
    // sockets, and references.  These are rarely used
    // as Set elements or dictionary keys; return the type tag to place all values
    // of the same type in one bucket, and let ValueEqual distinguish them through
    // structural (or identity) comparison.
    return type_seed;
}

} // namespace luma::detail
