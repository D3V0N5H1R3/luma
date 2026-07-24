#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/types/stdlib_type_handler.hpp"
#include "analysis/types/stdlib_types.hpp"
#include "analysis/types/type_checker.hpp"
#include "common/string_hash.hpp"
#include "stdlib/stdlib_catalog.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// Stdlib arity registry — populated from the shared catalog.
// ═══════════════════════════════════════════════════════════

void StdlibTypeHandler::init_arities() {
    // Arities are derived exclusively from the shared stdlib catalog —
    // no manual entries.  Constants and variadic functions with min-arity 0
    // are excluded since they are not subject to arity validation.
    for (const auto& [name, spec] : stdlib::catalog()) {
        if (spec.is_constant) {
            continue;
        }

        const int min_arity = spec.get_min_arity();
        const bool is_variadic = spec.is_variadic();

        // Skip variadic-with-min-0 — no arity constraint to check.
        if (is_variadic && min_arity == 0) {
            continue;
        }

        functions_[name].arity = ArityInfo{.min_arity = min_arity, .is_variadic = is_variadic};
    }
}

// ═══════════════════════════════════════════════════════════
// Stdlib record and choice type definitions
// ═══════════════════════════════════════════════════════════

namespace {

// ─── Helper: build a TypeAnnotation for a primitive type ───

[[nodiscard]] TypeAnnotation ann(const std::string& name) {
    return TypeAnnotation{name};
}

// ─── Helper: build a TypeAnnotation for dictionary<V> ───

[[nodiscard]] TypeAnnotation dict_ann(const std::string& value_type) {
    TypeAnnotation ta{"dictionary"};
    ta.type_params().push_back(ann(value_type));

    return ta;
}

// ─── Helper: build a TypeAnnotation for array<T> ───

[[nodiscard]] TypeAnnotation array_ann(const std::string& element_type) {
    TypeAnnotation ta{"array"};
    ta.type_params().push_back(ann(element_type));

    return ta;
}

// ─── Helper: build a RecordField ───

[[nodiscard]] RecordField field(const std::string& type_name, const std::string& field_name) {
    return RecordField{.type = ann(type_name), .name = field_name, .default_value = nullptr};
}

// ─── Helper: build a RecordField from a pre-built TypeAnnotation ───
// For fields whose type is not a bare primitive (e.g. dictionary<string> or
// array<Match>).

[[nodiscard]] RecordField field_of(TypeAnnotation type, const std::string& field_name) {
    return RecordField{.type = std::move(type), .name = field_name, .default_value = nullptr};
}

// ─── Static storage for synthetic declarations ───
//
// These must outlive every TypeChecker and Interpreter instance.
// We use a static local inside a function to guarantee safe
// initialisation order and lifetime.

// The implicitly-generated special members only touch standard containers; the
// sole escape path the analyzer traces is MSVC STL bad_alloc, which is fatal and
// cannot be handled here.
struct StdlibTypeStorage { // NOLINT(bugprone-exception-escape)
    // Owned declarations (unique_ptr keeps them alive).
    std::vector<std::unique_ptr<RecordDeclaration>> records;
    std::vector<std::unique_ptr<ChoiceDeclaration>> choices;

    // Lookup tables (raw pointers into the vectors above).
    StringMap<const RecordDeclaration*> record_map;
    StringMap<const ChoiceDeclaration*> choice_map;
};

// ─── Helper: register a synthetic record ───
//
// Derives the short record name from the qualified key (the substring after the
// final '.'), moves each RecordField into the declaration, and wires up the
// owning vector and lookup map — reducing every record definition to a single
// declarative call.
template <typename... Fields>
void add_record(StdlibTypeStorage& st, const std::string& qualified_name, Fields&&... fields) {
    auto rec = std::make_unique<RecordDeclaration>(SourceLocation{},
                                                   std::string{qualified_member(qualified_name)});

    (rec->fields.push_back(std::forward<Fields>(fields)), ...);

    st.record_map[qualified_name] = rec.get();
    st.records.push_back(std::move(rec));
}

[[nodiscard]] StdlibTypeStorage& storage() {
    static StdlibTypeStorage s = []() {
        StdlibTypeStorage st;

        add_record(st, "DateTime.TimeParts", field("integer", "year"), field("integer", "month"),
                   field("integer", "day"), field("integer", "hour"), field("integer", "minute"),
                   field("integer", "second"));

        add_record(st, "DateTime.Duration", field("integer", "days"), field("integer", "hours"),
                   field("integer", "minutes"), field("integer", "seconds"),
                   field("integer", "milliseconds"), field("boolean", "negative"));

        add_record(st, "FileSystem.FileInfo", field("integer", "size"),
                   field("number", "modified_time"), field("boolean", "is_directory"),
                   field("boolean", "is_file"), field("boolean", "is_symlink"));

        add_record(st, "FileSystem.PathParts", field("string", "parent"), field("string", "name"),
                   field("string", "stem"), field("string", "extension"));

        add_record(st, "Math.Summary", field("integer", "count"), field("number", "minimum"),
                   field("number", "maximum"), field("number", "mean"), field("number", "median"),
                   field("number", "standard_deviation"));

        add_record(st, "Socket.Address", field("string", "host"), field("integer", "port"));

        add_record(st, "Csv.Dialect", field("string", "delimiter"), field("string", "quote"));

        // Dictionary.to_array emits these key/value pairs at runtime (each a
        // record with type_name "KeyValue").  The `value` field carries the
        // dictionary's value type V, so it has no single concrete type here — a
        // `.value` access resolves permissively, exactly like a field access on
        // any other stdlib record returned by a module call.
        add_record(st, "Dictionary.KeyValue", field("string", "key"), field("V", "value"));

        add_record(st, "Http.Response", field("integer", "status"), field("string", "reason"),
                   field("string", "body"), field_of(dict_ann("string"), "headers"));

        // Http.Request carries the Http.Method choice natively (rather than a
        // stringified verb in an options dictionary), so a request is typed,
        // discoverable, and symmetrical with Http.Response.  Built by
        // Http.request_of / Http.request_with and consumed by Http.send.
        add_record(st, "Http.Request", field("Http.Method", "method"), field("string", "url"),
                   field_of(dict_ann("string"), "headers"), field("string", "body"),
                   field("integer", "timeout_ms"));

        add_record(st, "Http.UrlParts", field("string", "scheme"), field("string", "host"),
                   field("string", "port"), field("string", "path"), field("string", "query"));

        add_record(st, "Terminal.Size", field("integer", "columns"), field("integer", "rows"));

        add_record(st, "Socket.UdpPacket", field("string", "data"), field("string", "host"),
                   field("integer", "port"));

        add_record(st, "RegularExpression.Match", field("string", "text"),
                   field("integer", "position"), field("integer", "length"),
                   field_of(array_ann("Match"), "groups"));

        add_record(st, "Process.ProcessResult", field("integer", "exit_code"),
                   field("string", "output"));

        add_record(st, "Terminal.CursorPosition", field("integer", "row"),
                   field("integer", "column"));

        add_record(st, "Terminal.InputEvent", field("string", "key"), field("boolean", "shift"),
                   field("boolean", "ctrl"), field("boolean", "alt"));

        // ── DateTime.Weekday ────────────────────────────
        // Variant names must match k_weekday_names in
        // core/runtime/stdlib/system/datetime_module.cpp exactly (PascalCase,
        // ISO-8601 order: Monday = 1 … Sunday = 7).  Complements the existing
        // integer DateTime.day_of_week accessor with an exhaustive, type-safe form.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Weekday");
            ch->variants.push_back(ChoiceVariant{.name = "Monday", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Tuesday", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Wednesday", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Thursday", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Friday", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Saturday", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Sunday", .fields = {}});

            st.choice_map["DateTime.Weekday"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── DateTime.Month ──────────────────────────────
        // Variant names must match k_month_names in
        // core/runtime/stdlib/system/datetime_module.cpp exactly (PascalCase,
        // calendar order: January = 1 … December = 12).  Complements the existing
        // integer DateTime.month accessor with an exhaustive, type-safe form.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Month");
            ch->variants.push_back(ChoiceVariant{.name = "January", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "February", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "March", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "April", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "May", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "June", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "July", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "August", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "September", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "October", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "November", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "December", .fields = {}});

            st.choice_map["DateTime.Month"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Log.Level ───────────────────────────────────
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Level");
            ch->variants.push_back(ChoiceVariant{.name = "Debug", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Info", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Warn", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Error", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Off", .fields = {}});

            st.choice_map["Log.Level"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Decimal.RoundingMode ────────────────────────
        // Variant names must match rounding_mode_from_variant() in
        // core/common/decimal.cpp exactly (PascalCase, one per RoundingMode).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "RoundingMode");
            ch->variants.push_back(ChoiceVariant{.name = "HalfUp", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "HalfDown", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "HalfEven", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Up", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Down", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Ceiling", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Floor", .fields = {}});

            st.choice_map["Decimal.RoundingMode"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Http.Method ─────────────────────────────────
        // Variant names must match http_verb_from_method() in
        // core/runtime/stdlib/io/http_module.cpp exactly (PascalCase, one per
        // HTTP verb).  Converted to a verb string via Http.method_to_string,
        // which a program then passes under Http.request's "method" option key
        // (the options dictionary is homogeneous, so it cannot hold the choice
        // directly).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Method");
            ch->variants.push_back(ChoiceVariant{.name = "Get", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Post", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Put", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Patch", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Delete", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Head", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Options", .fields = {}});

            st.choice_map["Http.Method"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Terminal.Color ──────────────────────────────
        // Variant names map to the lowercase colour names in color_map() in
        // core/runtime/stdlib/io/terminal_module.cpp (PascalCase → snake_case:
        // BrightBlack → "bright_black").  Terminal.color / Terminal.background_color
        // accept this choice or the equivalent string; the choice form is total, so
        // a typo becomes a compile error instead of a runtime failure.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Color");
            ch->variants.push_back(ChoiceVariant{.name = "Black", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Red", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Green", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Yellow", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Blue", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Magenta", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Cyan", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "White", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BrightBlack", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BrightRed", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BrightGreen", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BrightYellow", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BrightBlue", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BrightMagenta", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BrightCyan", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BrightWhite", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Default", .fields = {}});

            st.choice_map["Terminal.Color"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Ordering ────────────────────────────────────
        // Top-level choice (no namespace) mirroring Rust's std::cmp::Ordering.
        // Variant names must match ordering_from_sign() in
        // core/runtime/stdlib/types/order_module.cpp exactly (Less / Equal /
        // Greater).  A comparison expressed as an exhaustive match over Ordering
        // is self-documenting — no "does negative mean first?".  The Order module
        // bridges to/from the existing numeric comparator via to_number /
        // from_number, so both the choice and the numeric convention interoperate.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Ordering");
            ch->variants.push_back(ChoiceVariant{.name = "Less", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Equal", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Greater", .fields = {}});

            st.choice_map["Ordering"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Json.Value ──────────────────────────────────
        // Recursive JSON ADT backed by shared/json/JsonValue — the first stdlib
        // choice with payload-carrying variants.  It brings untrusted JSON under
        // static, exhaustive typing: a match over Json.Value is the safe,
        // teachable way to walk parsed data, closing Luma's "no any" gap while the
        // permissive dynamic Json API (deserialize/get/serialize) stays intact.
        // Variant names must match the constructors in
        // core/runtime/stdlib/text/json_value_module.cpp exactly.  Recursive
        // payload type annotations MUST use the qualified name "Json.Value" so the
        // type resolver finds this choice in choices_ (a bare "Value" would not
        // resolve).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Value");

            // Parameter holds a unique_ptr (default_value), so it is move-only
            // and cannot be placed in a braced std::initializer_list (which
            // copies).  Build each single-payload variant by moving the
            // Parameter into the fields vector instead.
            auto payload_variant = [](std::string variant_name, TypeAnnotation type,
                                      std::string field_name) {
                ChoiceVariant variant;
                variant.name = std::move(variant_name);
                variant.fields.push_back(
                    Parameter{.type = std::move(type), .name = std::move(field_name)});

                return variant;
            };

            ch->variants.push_back(payload_variant("JsonObject", dict_ann("Json.Value"), "fields"));
            ch->variants.push_back(payload_variant("JsonArray", array_ann("Json.Value"), "items"));
            ch->variants.push_back(payload_variant("JsonString", ann("string"), "value"));
            ch->variants.push_back(payload_variant("JsonNumber", ann("number"), "value"));
            ch->variants.push_back(payload_variant("JsonBool", ann("boolean"), "value"));
            ch->variants.push_back(ChoiceVariant{.name = "JsonNull", .fields = {}});

            st.choice_map["Json.Value"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        return st;
    }();

    return s;
}

} // namespace

const StringMap<const RecordDeclaration*>& stdlib_record_types() {
    return storage().record_map;
}

const StringMap<const ChoiceDeclaration*>& stdlib_choice_types() {
    return storage().choice_map;
}

} // namespace luma
