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

        // DateTime.interval() constructs these range records (type_name "Interval").
        // Timestamps stay plain `number` seconds — the record only pairs a start and
        // end so contains/overlap/duration take one typed range instead of two loose
        // numbers.  The validating constructor guarantees end >= start.
        add_record(st, "DateTime.Interval", field("number", "start"), field("number", "end"));

        // DateTime.zoned() constructs these offset-aware timestamp records (type_name
        // "Zoned").  It bundles an instant (plain `number` Unix seconds, so every
        // point helper still applies) with the UTC offset it should be rendered in
        // (integer minutes in [-720, +840]).  The validating constructor guarantees a
        // legal offset, so a Zoned never formats with an impossible zone.
        add_record(st, "DateTime.Zoned", field("number", "timestamp"),
                   field("integer", "offset_minutes"));

        add_record(st, "FileSystem.FileInfo", field("integer", "size"),
                   field("number", "modified_time"), field("boolean", "is_directory"),
                   field("boolean", "is_file"), field("boolean", "is_symlink"),
                   field("FileSystem.FileKind", "kind"));

        add_record(st, "FileSystem.PathParts", field("string", "parent"), field("string", "name"),
                   field("string", "stem"), field("string", "extension"));

        add_record(st, "Math.Summary", field("integer", "count"), field("number", "minimum"),
                   field("number", "maximum"), field("number", "mean"), field("number", "median"),
                   field("number", "standard_deviation"));

        // Math.fraction() constructs these exact-rational records (type_name
        // "Fraction").  Always stored in lowest terms with a positive denominator,
        // so 1/3 + 1/6 is exactly 1/2 — no floating-point loss.  The numerator and
        // denominator are integers (exact whole values), matching Decimal's exact
        // arithmetic sibling.
        add_record(st, "Math.Fraction", field("integer", "numerator"),
                   field("integer", "denominator"));

        // Math.complex() constructs these complex-number records (type_name
        // "Complex").  Real and imaginary parts are measurements, so both are
        // `number`.  Data + free functions with no operator overloading, mirroring
        // Decimal and Math.Fraction.
        add_record(st, "Math.Complex", field("number", "real"), field("number", "imaginary"));

        // Math.vector2() / vec2_* take and return these 2D geometry records
        // (type_name "Vector2").  Named .x/.y components — measurements, so both
        // number — make 2D work far more teachable than array<number> index
        // arithmetic.  Data + free functions, mirroring Math.Complex.
        add_record(st, "Math.Vector2", field("number", "x"), field("number", "y"));

        // Math.vector3() / vec3_* take and return these 3D geometry records
        // (type_name "Vector3").
        add_record(st, "Math.Vector3", field("number", "x"), field("number", "y"),
                   field("number", "z"));

        // Math.matrix2() / mat2_* take and return these 2×2 transform matrices
        // (type_name "Matrix2").  Named .m00/.m01/.m10/.m11 components make small
        // linear transforms teachable, mirroring Math.Vector2 over LinearAlgebra's
        // array<array<number>>.  Every entry is a measurement, so `number`.
        add_record(st, "Math.Matrix2", field("number", "m00"), field("number", "m01"),
                   field("number", "m10"), field("number", "m11"));

        // Math.matrix3() / mat3_* take and return these 3×3 transform matrices
        // (type_name "Matrix3").  Row-major named components m00..m22.
        add_record(st, "Math.Matrix3", field("number", "m00"), field("number", "m01"),
                   field("number", "m02"), field("number", "m10"), field("number", "m11"),
                   field("number", "m12"), field("number", "m20"), field("number", "m21"),
                   field("number", "m22"));

        // Math.interval() constructs these closed numeric-range records (type_name
        // "Interval").  min/max are plain measurements (`number`); the validating
        // constructor guarantees max >= min so contains/clamp/length/overlap take one
        // well-formed range.  Mirrors DateTime.Interval (which shares the short
        // "Interval" type_name — harmless, like the shared "ParseError" records).
        add_record(st, "Math.Interval", field("number", "min"), field("number", "max"));

        // Math.five_number_summary() returns this box-plot record (type_name
        // "FiveNumberSummary"): the five order statistics needed to draw a box
        // plot.  Mirrors Math.Summary; every field is a number.
        add_record(st, "Math.FiveNumberSummary", field("number", "minimum"), field("number", "q1"),
                   field("number", "median"), field("number", "q3"), field("number", "maximum"));

        // Math.linear_fit() returns this least-squares regression record (type_name
        // "LineFit") — the trend line y = slope*x + intercept plus its r_squared
        // goodness-of-fit.  A plain returned record, mirroring Math.Summary.
        add_record(st, "Math.LineFit", field("number", "slope"), field("number", "intercept"),
                   field("number", "r_squared"));

        add_record(st, "Socket.Address", field("string", "host"), field("integer", "port"));

        add_record(st, "Csv.Dialect", field("string", "delimiter"), field("string", "quote"));

        // Csv.deserialize_detailed() surfaces this located parse failure (type_name
        // "ParseError") as the error type of its
        // result<array<array<string>>, Csv.ParseError>.  line and column are
        // 1-based indices into the source text, so both are integer; message is
        // the human-readable reason.  Mirrors Json.ParseError exactly.
        add_record(st, "Csv.ParseError", field("string", "message"), field("integer", "line"),
                   field("integer", "column"));

        // Xml.deserialize_detailed() surfaces this located parse failure (type_name
        // "ParseError") as the error type of its result<Xml.Node, Xml.ParseError>.
        // Same shape and rationale as Json.ParseError / Csv.ParseError.
        add_record(st, "Xml.ParseError", field("string", "message"), field("integer", "line"),
                   field("integer", "column"));

        // Json.parse_detailed() surfaces this located parse failure (type_name
        // "ParseError") as the error type of its result<Json.Value, Json.ParseError>.
        // line and column are 1-based indices into the source text, so both are
        // integer; message is the human-readable reason.
        add_record(st, "Json.ParseError", field("string", "message"), field("integer", "line"),
                   field("integer", "column"));

        // Color.rgb / rgba / from_hex / mix / lighten / darken construct these RGBA
        // colour records (type_name "Color").  Channels are 0–255 integers; alpha is
        // a 0–1 number.  A typed colour that serialises to the CSS strings the
        // GraphicalUi web-view already consumes.
        add_record(st, "Color.Color", field("integer", "red"), field("integer", "green"),
                   field("integer", "blue"), field("number", "alpha"));

        // Color.to_hsl / from_hsl / rotate_hue pivot through this hue/saturation/
        // lightness record (type_name "Hsl").  Hue is an angle in degrees [0, 360)
        // and saturation/lightness are 0–1 ratios — measurements, so every field is
        // a number.  Mirrors Color.Color: pure data plus free-function converters.
        add_record(st, "Color.Hsl", field("number", "hue"), field("number", "saturation"),
                   field("number", "lightness"));

        // Color.to_hsv / from_hsv pivot through this hue/saturation/value record
        // (type_name "Hsv") — the HSB model colour pickers use.  Hue is an angle in
        // degrees [0, 360) and saturation/value are 0–1 ratios — measurements, so
        // every field is a number.  Sibling of Color.Hsl.
        add_record(st, "Color.Hsv", field("number", "hue"), field("number", "saturation"),
                   field("number", "value"));

        // Dictionary.to_array emits these key/value pairs at runtime (each a
        // record with type_name "KeyValue").  The `value` field carries the
        // dictionary's value type V, so it has no single concrete type here — a
        // `.value` access resolves permissively, exactly like a field access on
        // any other stdlib record returned by a module call.
        add_record(st, "Dictionary.KeyValue", field("string", "key"), field("V", "value"));

        // Graph.edges emits these edge records at runtime (each a record with
        // type_name "Edge").  A structured, deterministic enumeration of a graph's
        // edges — the typed "list the edges" answer that otherwise means iterating
        // vertices × neighbours and re-querying edge_weight.  Mirrors the
        // Dictionary.KeyValue + Dictionary.to_array enumeration pattern.
        add_record(st, "Graph.Edge", field("string", "from"), field("string", "to"),
                   field("number", "weight"));

        // Graph.shortest_path_detailed() returns this route record (type_name
        // "Path"): the ordered vertices plus the path's total weight, so a caller
        // gets both answers from one call instead of re-walking edge_weight.
        // Additive sibling of Graph.Edge; vertices is array<string>, cost a number.
        add_record(st, "Graph.Path", field_of(array_ann("string"), "vertices"),
                   field("number", "cost"));

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

        // Http.parse_cookie decodes a Set-Cookie header into this flat cookie
        // record (type_name "Cookie"); Http.cookie_header formats it back.  Keeping
        // the record flat (no nested attribute map) and the parser lenient matches
        // Http.UrlParts — a structured decode of a header-ish string.
        add_record(st, "Http.Cookie", field("string", "name"), field("string", "value"),
                   field("string", "domain"), field("string", "path"), field("string", "expires"),
                   field("boolean", "secure"), field("boolean", "http_only"));

        add_record(st, "Terminal.Size", field("integer", "columns"), field("integer", "rows"));

        add_record(st, "Socket.UdpPacket", field("string", "data"), field("string", "host"),
                   field("integer", "port"));

        add_record(st, "RegularExpression.Match", field("string", "text"),
                   field("integer", "position"), field("integer", "length"),
                   field_of(array_ann("Match"), "groups"));

        add_record(st, "Process.ProcessResult", field("integer", "exit_code"),
                   field("string", "output"));

        // Richer sibling of ProcessResult returned by Process.execute: captures
        // stdout and stderr separately (which run/ProcessResult merges into one
        // stream) plus a derived success flag.  Field names match the record
        // built in core/runtime/stdlib/system/process_module.cpp exactly.
        add_record(st, "Process.CommandOutput", field("integer", "exit_code"),
                   field("string", "standard_output"), field("string", "standard_error"),
                   field("boolean", "success"));

        // Process.command() builds this shell-free command record (type_name
        // "Command"): an explicit program plus an argument vector.  Process.run_command
        // executes it directly (execvp / CreateProcess, no shell), so metacharacters
        // (; && | $(...)) are inert — the safe alternative to Process.run's shell
        // string.  program is a string; arguments an array<string>.
        add_record(st, "Process.Command", field("string", "program"),
                   field_of(array_ann("string"), "arguments"));

        add_record(st, "Terminal.CursorPosition", field("integer", "row"),
                   field("integer", "column"));

        add_record(st, "Terminal.InputEvent", field("string", "key"), field("boolean", "shift"),
                   field("boolean", "ctrl"), field("boolean", "alt"));

        // Typed decode of a "mouse:<kind>:<row>:<col>" event string (as produced
        // by Terminal.get_input / read_key in mouse mode), returned by
        // Terminal.parse_mouse_event.  The `kind` field carries the
        // Terminal.MouseEventKind choice (fully-qualified so the annotation
        // resolves to the choice, like FileSystem.FileInfo.kind); row and column
        // are 1-based, mirroring Terminal.CursorPosition.
        add_record(st, "Terminal.MouseEvent", field("Terminal.MouseEventKind", "kind"),
                   field("integer", "row"), field("integer", "column"));

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

        // ── Log.Output ──────────────────────────────────
        // Where Log writes formatted lines.  Stderr / Stdout are the two standard
        // streams; File(path) carries its destination path as a payload.  Log.set_output
        // accepts this choice or the equivalent string ("stderr" / "stdout" / a path),
        // mirroring the Log.Level dual-form — the typed form makes a stream-vs-path typo a
        // compile error instead of silently creating a file named "stdout".  Variant names
        // must match the set_output variant handling in
        // core/runtime/stdlib/system/log_module.cpp exactly (PascalCase).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Output");

            // File carries a path payload, so it is a payload-bearing variant (like
            // Json.Value's) rather than a bare unit variant; Stderr / Stdout are unit
            // variants.  A Parameter holds a move-only default_value, so build the payload
            // variant by moving the Parameter into the fields vector.
            ChoiceVariant file_variant;
            file_variant.name = "File";
            file_variant.fields.push_back(Parameter{.type = ann("string"), .name = "path"});

            ch->variants.push_back(ChoiceVariant{.name = "Stderr", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Stdout", .fields = {}});
            ch->variants.push_back(std::move(file_variant));

            st.choice_map["Log.Output"] = ch.get();
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

        // ── Encoder.Encoding ────────────────────────────
        // Text encoding selector consumed by Encoder.encode_text / decode_text.
        // Variant names must match encoding_from_variant() in
        // core/runtime/stdlib/system/encoder_module.cpp exactly (PascalCase).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Encoding");
            ch->variants.push_back(ChoiceVariant{.name = "Utf8", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Ascii", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Latin1", .fields = {}});

            st.choice_map["Encoder.Encoding"] = ch.get();
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

        // ── Terminal.MouseEventKind ─────────────────────
        // Variant names must match mouse_event_kind_variant() in
        // core/runtime/stdlib/io/terminal_input_common.hpp exactly (PascalCase).
        // Total over every kind format_mouse_event emits, so a match over a
        // Terminal.MouseEvent.kind is exhaustive and typo-proof — the typed
        // replacement for hand-splitting a "mouse:<kind>:<row>:<col>" string.
        // Button events pair each of left/middle/right with press/release/drag;
        // wheel events cover the four scroll directions.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "MouseEventKind");
            ch->variants.push_back(ChoiceVariant{.name = "LeftPress", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "LeftRelease", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "LeftDrag", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "MiddlePress", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "MiddleRelease", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "MiddleDrag", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "RightPress", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "RightRelease", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "RightDrag", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "WheelUp", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "WheelDown", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "WheelLeft", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "WheelRight", .fields = {}});

            st.choice_map["Terminal.MouseEventKind"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Terminal.Key ────────────────────────────────
        // Total decode of a Terminal.InputEvent.key string into an exhaustive,
        // typo-proof choice, so key handling is a match instead of a chain of
        // string equality tests ("enter", "f1", …).  Variant names/shapes must
        // match parse_key_value() in core/runtime/stdlib/io/terminal_module.cpp.
        // Character carries the literal text of a printable key (a single grapheme
        // in practice); Function carries the F-key number (F1 → Function(1)).
        // Unknown covers the decoder's "unknown" fallback and any unrecognised
        // token, keeping parse_key total.  Recursive-free, so no self-reference.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Key");

            // Parameter is move-only (see Json.Value), so build payload variants
            // by moving the Parameter into the fields vector.
            auto payload_variant = [](std::string variant_name, TypeAnnotation type,
                                      std::string field_name) {
                ChoiceVariant variant;
                variant.name = std::move(variant_name);
                variant.fields.push_back(
                    Parameter{.type = std::move(type), .name = std::move(field_name)});

                return variant;
            };

            ch->variants.push_back(payload_variant("Character", ann("string"), "value"));
            ch->variants.push_back(payload_variant("Function", ann("integer"), "number"));
            ch->variants.push_back(ChoiceVariant{.name = "Enter", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Escape", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Tab", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Backspace", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Space", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Up", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Down", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Left", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Right", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Home", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "End", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "PageUp", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "PageDown", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Insert", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Delete", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Unknown", .fields = {}});

            st.choice_map["Terminal.Key"] = ch.get();
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

        // ── Xml.Node ────────────────────────────────────
        // Recursive XML ADT mirroring Json.Value — the typed view over the opaque
        // `xml` runtime type, so a parsed document can be walked with an exhaustive
        // `match` instead of the untyped tag/children/text accessors.  Element
        // carries its tag, attribute dictionary, and ordered child nodes (every
        // node type, not just elements); Text / Comment / CData carry their raw
        // content string.  Variant names/shapes must match xml_to_node() in
        // core/runtime/stdlib/text/xml_module.cpp exactly.  The recursive children
        // annotation MUST use the qualified name "Xml.Node" so the resolver finds
        // this choice in choices_ (a bare "Node" would not resolve).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Node");

            // Parameter is move-only (see Json.Value), so payload variants are
            // built by moving each Parameter into the fields vector.
            auto payload_variant = [](std::string variant_name, TypeAnnotation type,
                                      std::string field_name) {
                ChoiceVariant variant;
                variant.name = std::move(variant_name);
                variant.fields.push_back(
                    Parameter{.type = std::move(type), .name = std::move(field_name)});

                return variant;
            };

            // Element has three payload fields, so it is built directly rather
            // than via the single-field payload_variant helper above.
            ChoiceVariant element;
            element.name = "Element";
            element.fields.push_back(Parameter{.type = ann("string"), .name = "tag"});
            element.fields.push_back(Parameter{.type = dict_ann("string"), .name = "attributes"});
            element.fields.push_back(Parameter{.type = array_ann("Xml.Node"), .name = "children"});
            ch->variants.push_back(std::move(element));

            ch->variants.push_back(payload_variant("Text", ann("string"), "content"));
            ch->variants.push_back(payload_variant("Comment", ann("string"), "content"));
            ch->variants.push_back(payload_variant("CData", ann("string"), "content"));

            st.choice_map["Xml.Node"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── FileSystem.FileKind ─────────────────────────
        // Variant names must match make_file_kind_choice() in
        // core/runtime/stdlib/io/filesystem_internal.hpp exactly (PascalCase).
        // The single, mutually-exclusive answer to "what kind of thing is this
        // path?", complementing the FileSystem.FileInfo is_directory / is_file /
        // is_symlink booleans with an exhaustive, match-able choice.  Classified
        // symlink-first (like lstat): a symbolic link is reported as Symlink even
        // when its target is a directory or file.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "FileKind");
            ch->variants.push_back(ChoiceVariant{.name = "File", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Directory", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Symlink", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Other", .fields = {}});

            st.choice_map["FileSystem.FileKind"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── FileSystem.IoError ──────────────────────────
        // Opt-in typed error surfaced via result<T, FileSystem.IoError> on the
        // read_file_typed slice of FileSystem (leaving string-error read_file
        // untouched).  Variant names must match make_io_error_choice() in
        // core/runtime/stdlib/io/filesystem_internal.hpp exactly (PascalCase).
        // A closed, match-able set of the common failure categories, replacing
        // brittle substring matching on an opaque error string.  Prototype scope
        // pending a maintainer decision on generalising typed I/O errors.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "IoError");
            ch->variants.push_back(ChoiceVariant{.name = "NotFound", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "PermissionDenied", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "AlreadyExists", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "InvalidInput", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Other", .fields = {}});

            st.choice_map["FileSystem.IoError"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Sign ────────────────────────────────────────
        // Top-level choice (no namespace) mirroring Rust's num::signum / the sign
        // of Ordering.  Variant names must match make_sign_choice() in
        // core/runtime/stdlib/math/math_module.cpp exactly (Negative / Zero /
        // Positive).  Math.sign_of returns it — a self-documenting alternative to
        // the magic -1 / 0 / 1 of Math.sign ("does -1 mean negative or error?").
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Sign");
            ch->variants.push_back(ChoiceVariant{.name = "Negative", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Zero", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Positive", .fields = {}});

            st.choice_map["Sign"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Http.StatusClass ────────────────────────────
        // Variant names must match make_status_class_choice() in
        // core/runtime/stdlib/io/http_module.cpp exactly (PascalCase, one per HTTP
        // status family per RFC 9110: 1xx-5xx).  Http.status_class classifies a raw
        // Http.Response.status integer into an exhaustive, match-able family instead
        // of hand-written `status >= 200 && status < 300` magic ranges.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "StatusClass");
            ch->variants.push_back(ChoiceVariant{.name = "Informational", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Success", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Redirection", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "ClientError", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "ServerError", .fields = {}});

            st.choice_map["Http.StatusClass"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Hash.Algorithm ──────────────────────────────
        // Variant names must match algorithm_name_from_variant() in
        // core/runtime/stdlib/system/hash_digest.cpp exactly (PascalCase →
        // lowercase: Sha256 → "sha256").  Mirrors the set reported by
        // Hash.algorithms().  Hash.verify / Hash.digest accept this choice or the
        // equivalent string (the Terminal.Color | string dual-form precedent), so a
        // typo like "sha-256" becomes a compile error instead of a runtime failure.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Algorithm");
            ch->variants.push_back(ChoiceVariant{.name = "Md5", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Sha1", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Sha256", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Sha512", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Crc32", .fields = {}});

            st.choice_map["Hash.Algorithm"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Socket.IpAddress ────────────────────────────
        // A parsed, validated IP literal, split into its two families so the
        // V4/V6 distinction is match-exhaustive and autocompleted (Socket.Address
        // only carries an unvalidated host string).  Both variants carry the
        // canonical address text as a payload.  Built by Socket.parse_ip (which
        // returns result<Socket.IpAddress>) and rendered by Socket.ip_to_string.
        // The payload field name "address" must match make_ip_address() in
        // core/runtime/stdlib/io/socket_module.cpp.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "IpAddress");

            // Parameter is move-only (holds a default_value unique_ptr), so build
            // each payload variant by moving the Parameter into the fields vector.
            auto v4 = ChoiceVariant{};
            v4.name = "V4";
            v4.fields.push_back(Parameter{.type = ann("string"), .name = "address"});

            auto v6 = ChoiceVariant{};
            v6.name = "V6";
            v6.fields.push_back(Parameter{.type = ann("string"), .name = "address"});

            ch->variants.push_back(std::move(v4));
            ch->variants.push_back(std::move(v6));

            st.choice_map["Socket.IpAddress"] = ch.get();
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
