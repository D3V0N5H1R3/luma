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

        add_record(st, "Http.Response", field("integer", "status"), field("string", "reason"),
                   field("string", "body"), field_of(dict_ann("string"), "headers"));

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
