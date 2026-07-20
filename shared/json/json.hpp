#ifndef LUMA_JSON_HPP
#define LUMA_JSON_HPP

// All includes below are required in the header:
//   <cstdint>      — int64_t in variant
//   <concepts>     — std::constructible_from / std::same_as in JsonBuilder
//   <map>          — ObjectType = std::map<...>
//   <string>       — key type and string variant alternative
//   <string_view>  — operator[] and has() parameter types
//   <variant>      — JsonValue::data_
//   <vector>       — ArrayType = std::vector<JsonValue>
#include <concepts>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "common/narrow_int.hpp"

namespace luma::json {

// Concept: primitive C++ types that can be extracted from a JsonValue via
// try_as<T>() / try_get<T>() / get_or<T>().
template <typename T>
concept JsonExtractible =
    std::same_as<T, bool> || std::same_as<T, int> || std::same_as<T, int64_t> ||
    std::same_as<T, double> || std::same_as<T, std::string>;

// Minimal JSON value type supporting the six JSON types.
// Objects use std::map for deterministic key ordering in serialised output.
class JsonValue {
public:
    using ArrayType = std::vector<JsonValue>;
    using ObjectType = std::map<std::string, JsonValue, std::less<>>;

    // Construction — null.
    JsonValue();

    // Construction — typed.
    explicit JsonValue(bool value);
    explicit JsonValue(int value);
    explicit JsonValue(int64_t value);
    explicit JsonValue(double value);
    explicit JsonValue(std::string value) noexcept;
    explicit JsonValue(const char* value);
    explicit JsonValue(ArrayType value) noexcept;
    explicit JsonValue(ObjectType value) noexcept;

    // Type queries.
    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] bool is_bool() const noexcept;
    [[nodiscard]] bool is_integer() const noexcept;
    [[nodiscard]] bool is_number() const noexcept;
    [[nodiscard]] bool is_string() const noexcept;
    [[nodiscard]] bool is_array() const noexcept;
    [[nodiscard]] bool is_object() const noexcept;

    // Value accessors (throw std::runtime_error on type mismatch).
    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] int64_t as_integer() const;
    [[nodiscard]] double as_number() const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const ArrayType& as_array() const;
    [[nodiscard]] const ObjectType& as_object() const;

    // Mutable value accessors (throw std::runtime_error on type mismatch).
    [[nodiscard]] ArrayType& as_array();
    [[nodiscard]] ObjectType& as_object();

    // Object member access.
    [[nodiscard]] const JsonValue& operator[](std::string_view key) const;
    [[nodiscard]] bool has(std::string_view key) const noexcept;

    // Safe object member access — returns default_val if not an object or
    // key is absent.  Keeps operator[] for fields that MUST exist.
    [[nodiscard]] const JsonValue& get(std::string_view key, const JsonValue& default_val) const;

    // Reject temporaries for the default: the returned reference would dangle.
    const JsonValue& get(std::string_view key, JsonValue&& default_val) const = delete;

    // Overload with no default — returns a static null sentinel.
    [[nodiscard]] const JsonValue& get(std::string_view key) const;

    // Safe scalar extraction — returns the stored value if this JsonValue
    // holds a JSON type compatible with T, otherwise std::nullopt.  This is
    // the single source of truth for the JSON-type → C++-type mapping shared
    // by try_get(), get_or() and the json_helpers field extractors.
    //
    // For T == int the value is range-checked via luma::try_narrow_int and
    // yields std::nullopt when the stored integer does not fit in an int, so the
    // get_or()/try_get() contract (default/nullopt on an incompatible value) is
    // never violated by a throw.
    template <JsonExtractible T> [[nodiscard]] std::optional<T> try_as() const {
        if constexpr (std::same_as<T, bool>) {
            return is_bool() ? std::optional<T>{as_bool()} : std::nullopt;
        } else if constexpr (std::same_as<T, int>) {
            return is_integer() ? luma::try_narrow_int(as_integer()) : std::nullopt;
        } else if constexpr (std::same_as<T, int64_t>) {
            return is_integer() ? std::optional<T>{as_integer()} : std::nullopt;
        } else if constexpr (std::same_as<T, double>) {
            return is_number() ? std::optional<T>{as_number()} : std::nullopt;
        } else {
            static_assert(std::same_as<T, std::string>);
            return is_string() ? std::optional<T>{as_string()} : std::nullopt;
        }
    }

    // Safe object-field extraction — returns the value of `key` if this is an
    // object containing it with a type compatible with T, else std::nullopt.
    // External callers use the json_helpers try_extract_field() free-function
    // facade; this member is the underlying primitive it delegates to.
    template <JsonExtractible T>
    [[nodiscard]] std::optional<T> try_get(std::string_view key) const {
        if (const auto* obj = std::get_if<ObjectType>(&data_)) {
            auto it = obj->find(key);
            if (it != obj->end()) {
                return it->second.try_as<T>();
            }
        }
        return std::nullopt;
    }

    // Safe accessor returning by value — useful for extracting non-reference
    // primitive types with a fallback default.  Returns default_val when the
    // field is absent or has an incompatible JSON type.
    template <JsonExtractible T>
    [[nodiscard]] T get_or(std::string_view key, const T& default_val) const {
        return try_get<T>(key).value_or(default_val);
    }

    // Serialisation to JSON string.
    [[nodiscard]] std::string to_string() const;

    // Parse a JSON string into a JsonValue.
    // max_depth controls the maximum nesting depth for arrays/objects.
    [[nodiscard]] static JsonValue parse(std::string_view input, std::size_t max_depth = 128);

private:
    void serialise(std::string& out, int depth = 0) const;

    // Type-checked accessor helper. Returns the stored value of type T or
    // throws std::runtime_error with the given type_name in the message.
    template <typename T> [[nodiscard]] const T& get_as(const char* type_name) const;

    // Mutable type-checked accessor helper.
    template <typename T> [[nodiscard]] T& get_as_mut(const char* type_name);

    // Static null sentinel for get() overload with no default.
    [[nodiscard]] static const JsonValue& null_sentinel();

    std::variant<std::monostate, // null
                 bool, int64_t, double, std::string, ArrayType, ObjectType>
        data_;
};

// ═══════════════════════════════════════════════════════════════════
// Free parse function — decouples parsing from the value type
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline JsonValue parse(std::string_view input, std::size_t max_depth = 128) {
    return JsonValue::parse(input, max_depth);
}

// ═══════════════════════════════════════════════════════════════════
// JsonBuilder — fluent API for constructing JSON objects
// ═══════════════════════════════════════════════════════════════════
//
// Usage:
//   auto obj = JsonBuilder()
//       .set("name", JsonValue("luma"))
//       .set("version", 1)
//       .set_if(has_debug, "debug", JsonValue(true))
//       .build();

class JsonBuilder {
public:
    // Accepts any type that JsonValue can be constructed from, including
    // JsonValue itself (which is simply forwarded via the move constructor).
    template <typename T>
        requires std::constructible_from<JsonValue, T&&>
    JsonBuilder& set(std::string key, T&& value) {
        if constexpr (std::same_as<std::decay_t<T>, JsonValue>) {
            obj_.emplace(std::move(key), std::forward<T>(value));
        } else {
            obj_.emplace(std::move(key), JsonValue(std::forward<T>(value)));
        }

        return *this;
    }

    template <typename T>
        requires std::constructible_from<JsonValue, T&&>
    JsonBuilder& set_if(bool condition, std::string key, T&& value) {
        if (condition) {
            set(std::move(key), std::forward<T>(value));
        }

        return *this;
    }

    // Build from lvalue — copies the map so the builder remains reusable.
    [[nodiscard]] JsonValue build() & {
        return JsonValue(JsonValue::ObjectType(obj_));
    }

    // Build from rvalue — moves the map for efficiency.
    [[nodiscard]] JsonValue build() && {
        return JsonValue(std::move(obj_));
    }

private:
    JsonValue::ObjectType obj_;
};

// ═══════════════════════════════════════════════════════════════════
// ArrayBuilder — fluent API for constructing JSON arrays
// ═══════════════════════════════════════════════════════════════════
//
// Usage:
//   auto arr = ArrayBuilder()
//       .add(JsonValue(1))
//       .add(JsonValue("hello"))
//       .build();

class ArrayBuilder {
public:
    template <typename T>
        requires std::constructible_from<JsonValue, T&&>
    ArrayBuilder& add(T&& value) {
        if constexpr (std::same_as<std::decay_t<T>, JsonValue>) {
            arr_.push_back(std::forward<T>(value));
        } else {
            arr_.push_back(JsonValue(std::forward<T>(value)));
        }

        return *this;
    }

    template <typename T>
        requires std::constructible_from<JsonValue, T&&>
    ArrayBuilder& add_if(bool condition, T&& value) {
        if (condition) {
            add(std::forward<T>(value));
        }

        return *this;
    }

    // Build from lvalue — copies the array so the builder remains reusable.
    [[nodiscard]] JsonValue build() & {
        return JsonValue(JsonValue::ArrayType(arr_));
    }

    // Build from rvalue — moves the array for efficiency.
    [[nodiscard]] JsonValue build() && {
        return JsonValue(std::move(arr_));
    }

private:
    JsonValue::ArrayType arr_;
};

} // namespace luma::json

#endif // LUMA_JSON_HPP
