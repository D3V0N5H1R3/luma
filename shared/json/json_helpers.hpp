#ifndef LUMA_JSON_HELPERS_HPP
#define LUMA_JSON_HELPERS_HPP

#include <optional>
#include <string_view>

#include "json/json.hpp"

namespace luma::json {

// ─── JSON field extraction ───

// Canonical entry point for extracting a typed field from a JSON object in
// external code (the LSP and DAP servers use this spelling exclusively).
// Returns std::nullopt when the field is absent or has the wrong JSON type —
// use it when the caller needs to distinguish "field missing" from "field
// present with a value".
//
// This is a thin free-function facade over the JsonValue::try_get<T>() member,
// which owns the JSON-type → C++-type mapping (see json.hpp).  Prefer this
// spelling; the member primitive remains for code that already holds a
// JsonValue and wants member-call syntax.
template <JsonExtractible T>
[[nodiscard]] inline std::optional<T> try_extract_field(const JsonValue& value,
                                                        std::string_view name) {
    return value.try_get<T>(name);
}

} // namespace luma::json

#endif // LUMA_JSON_HELPERS_HPP
