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

// ─── Helper: build a TypeAnnotation for optional<T> ───

[[nodiscard]] TypeAnnotation optional_ann(const std::string& value_type) {
    TypeAnnotation ta{"optional"};
    ta.type_params().push_back(ann(value_type));

    return ta;
}

// ─── Helper: build a TypeAnnotation for array<T> ───

[[nodiscard]] TypeAnnotation array_ann(const std::string& element_type) {
    TypeAnnotation ta{"array"};
    ta.type_params().push_back(ann(element_type));

    return ta;
}

// ─── Helper: build a TypeAnnotation for array<array<T>> ───

[[nodiscard]] TypeAnnotation array2_ann(const std::string& element_type) {
    TypeAnnotation inner{"array"};
    inner.type_params().push_back(ann(element_type));

    TypeAnnotation outer{"array"};
    outer.type_params().push_back(std::move(inner));

    return outer;
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

        // Calendar-only date (no time-of-day) and wall-clock-only time (no date),
        // the partial counterparts to the full DateTime.TimeParts breakdown.  A
        // Date models a value that is genuinely only a calendar date (a birthday,
        // a due date) and a Time only a wall-clock time (an alarm, opening hours),
        // so neither carries the fields it should not.  Built by the validating
        // constructors DateTime.date / DateTime.time (which guarantee a
        // well-formed value), extracted from an instant by DateTime.date_of /
        // DateTime.time_of, and recombined into a timestamp by DateTime.combine.
        add_record(st, "DateTime.Date", field("integer", "year"), field("integer", "month"),
                   field("integer", "day"));
        add_record(st, "DateTime.Time", field("integer", "hour"), field("integer", "minute"),
                   field("integer", "second"));

        add_record(st, "DateTime.Duration", field("integer", "days"), field("integer", "hours"),
                   field("integer", "minutes"), field("integer", "seconds"),
                   field("integer", "milliseconds"), field("boolean", "negative"));

        // DateTime.period() constructs these calendar-span records (type_name
        // "Period").  Where DateTime.Duration models a wall-clock span (a fixed
        // number of seconds), a Period models a *calendar* span — "1 year, 2
        // months, 3 days" — whose real length depends on which month/year it is
        // added to.  All three components are whole counts (integer) and may be
        // negative.  Built by DateTime.period, applied to an instant by
        // DateTime.add_period, and measured between two instants by
        // DateTime.between_dates, reusing the existing add_months/add_years
        // calendar arithmetic.
        add_record(st, "DateTime.Period", field("integer", "years"), field("integer", "months"),
                   field("integer", "days"));

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

        // FileSystem.permissions() constructs these access-right records (type_name
        // "Permissions").  The three booleans are the beginner-facing answer to
        // "what may I do with this file?" (readable / writable / executable), while
        // mode is the POSIX mode bits escape hatch for advanced users.  Built
        // cross-platform from std::filesystem::perms — on Windows the bits are
        // synthesised from the read-only attribute — so the record is never null.
        add_record(st, "FileSystem.Permissions", field("boolean", "readable"),
                   field("boolean", "writable"), field("boolean", "executable"),
                   field("integer", "mode"));

        add_record(st, "FileSystem.PathParts", field("string", "parent"), field("string", "name"),
                   field("string", "stem"), field("string", "extension"));

        // Math.Vector2 records — still used by circle and rect_center functions.
        // Named .x/.y components are measurements, so both are `number`.
        add_record(st, "Math.Vector2", field("number", "x"), field("number", "y"));

        // A unit-rotation record (type_name "Quaternion").  w is the scalar part
        // and x/y/z the vector part — all measurements, so `number`.  Retained as
        // a type for annotations; the constructor/operator functions were removed.
        add_record(st, "Math.Quaternion", field("number", "w"), field("number", "x"),
                   field("number", "y"), field("number", "z"));

        // Math.interval() constructs these closed numeric-range records (type_name
        // "Interval").  min/max are plain measurements (`number`); the validating
        // constructor guarantees max >= min so contains/clamp/length/overlap take one
        // well-formed range.  Mirrors DateTime.Interval (which shares the short
        // "Interval" type_name — harmless, like the shared "ParseError" records).
        add_record(st, "Math.Interval", field("number", "min"), field("number", "max"));

        // Math.rect() constructs these axis-aligned 2D rectangle records (type_name
        // "Rect").  x/y are the top-left corner and width/height the extent — all
        // measurements, so `number`.  Named .x/.y/.width/.height make layout,
        // hit-testing, collision, and cropping far more teachable than four loose
        // numbers and hand-written overlap arithmetic (the 2D analogue of
        // Math.Interval).  Data + free functions, mirroring Math.Vector2; the one
        // fallible operation (rect_intersection) returns optional<Math.Rect>.
        add_record(st, "Math.Rect", field("number", "x"), field("number", "y"),
                   field("number", "width"), field("number", "height"));

        // Math.circle() constructs these 2D circle records (type_name "Circle"):
        // a Math.Vector2 centre plus a non-negative radius.  Data reusing
        // Math.Vector2, with total boolean predicates (contains / intersects /
        // circle_rect_intersects) so a beginner writing a simple game gets the
        // second-most-common collision test after rectangles without hand-writing
        // the distance-squared comparison — mirroring Math.Rect.
        add_record(st, "Math.Circle", field("Math.Vector2", "center"), field("number", "radius"));

        add_record(st, "Socket.Address", field("string", "host"), field("integer", "port"));

        add_record(st, "Csv.Dialect", field("string", "delimiter"), field("string", "quote"));

        // Csv.deserialize_table() returns this header+rows table shape (type_name
        // "Table") as result<Csv.Table, Csv.ParseError>.  headers carries the
        // column names once and rows the positional cells, preserving column
        // order (which the array<dictionary<string>> shape of deserialize_records
        // loses) and keeping the header even when there are zero data rows.
        // Csv.serialize_table round-trips it and Csv.column extracts a column by
        // name.
        add_record(st, "Csv.Table", field_of(array_ann("string"), "headers"),
                   field_of(array2_ann("string"), "rows"));

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

        // Color.to_cmyk / from_cmyk pivot through this cyan/magenta/yellow/key
        // (black) record (type_name "Cmyk") — the subtractive model used by print
        // production. Every channel is a 0–1 ratio, so every field is a number.
        // Sibling of Color.Hsl / Color.Hsv.
        add_record(st, "Color.Cmyk", field("number", "cyan"), field("number", "magenta"),
                   field("number", "yellow"), field("number", "key"));

        // ── Color.Name ──────────────────────────────────
        // A curated palette of common named colours (a subset of the CSS named
        // colours, not all 140), giving beginners a typo-proof, autocompleted
        // alternative to remembering hex strings — the Color analogue of the
        // exhaustive Terminal.Color palette.  Color.from_name(Color.Name) maps a
        // variant to its Color.Color RGB value.  Variant names must match
        // rgb_for_color_name() in core/runtime/stdlib/io/color_module.cpp exactly
        // (PascalCase); CSS-canonical values (so Green is 0,128,0 and Lime is
        // 0,255,0, matching the web platform).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Name");
            for (const char* variant :
                 {"Black", "White", "Red", "Green", "Lime", "Blue", "Yellow", "Cyan", "Magenta",
                  "Gray", "Silver", "Orange", "Purple", "Pink", "Brown"}) {
                ch->variants.push_back(ChoiceVariant{.name = variant, .fields = {}});
            }

            st.choice_map["Color.Name"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

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

        // Http.parse_media_type decodes a Content-Type header value into this
        // record (type_name "MediaType").  `type`/`subtype` are the two halves of
        // "type/subtype" (lower-cased, RFC 9110 case-insensitive), and `parameters`
        // is the trailing "; key=value" pairs as a dictionary (keys lower-cased).
        // Structured, so "is this JSON?" or reading the charset no longer needs
        // manual string splitting, mirroring Http.UrlParts/Http.parse_url.
        add_record(st, "Http.MediaType", field("string", "type"), field("string", "subtype"),
                   field_of(dict_ann("string"), "parameters"));

        // Http.parse_cookie decodes a Set-Cookie header into this flat cookie
        // record (type_name "Cookie"); Http.cookie_header formats it back.  Keeping
        // the record flat (no nested attribute map) and the parser lenient matches
        // Http.UrlParts — a structured decode of a header-ish string.
        add_record(st, "Http.Cookie", field("string", "name"), field("string", "value"),
                   field("string", "domain"), field("string", "path"), field("string", "expires"),
                   field("boolean", "secure"), field("boolean", "http_only"));

        add_record(st, "Terminal.Size", field("integer", "columns"), field("integer", "rows"));

        // A single named capture group, e.g. the "year" in (?<year>\d{4}) or
        // (?P<year>\d{4}); an unmatched-but-named group (an optional branch of
        // an alternation that did not participate) still has name set, with
        // text = "" -- name is only "" if a Capture were built for a positional
        // group that has no name, which never happens in named_groups below.
        // std::regex's ECMAScript engine has no native named-group support (see
        // regularexpression_module.cpp), so `name` is extracted client-side from
        // the pattern text and mapped to the group's ordinary positional index;
        // `text`/`position`/`length` mirror that same positional group's fields.
        add_record(st, "RegularExpression.Capture", field("string", "name"),
                   field("string", "text"), field("integer", "position"),
                   field("integer", "length"));

        // groups keeps its original array<Match> shape for backward
        // compatibility; named_groups is purely additive -- a name -> Capture
        // lookup over the very same submatches (an unnamed group is simply
        // absent from this dictionary, never present with name = "").
        add_record(st, "RegularExpression.Match", field("string", "text"),
                   field("integer", "position"), field("integer", "length"),
                   field_of(array_ann("Match"), "groups"),
                   field_of(dict_ann("Capture"), "named_groups"));

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

        // ── Process.ExitStatus ───────────────────────────
        // Classifies the exit_code sign convention shared by every Process
        // record (ProcessResult, CommandOutput): 0 = clean exit, a positive
        // code = the process ran and exited non-zero, a negative code = the
        // process never ran at all (spawn/launch failure — see
        // platform_process::execute_command_captured).  Process.exit_status
        // turns that magic-sign convention into an exhaustive, match-able
        // type, mirroring Http.StatusClass and Sign above.  Variant names
        // must match make_exit_status_choice() in
        // core/runtime/stdlib/system/process_module.cpp exactly (PascalCase).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "ExitStatus");

            // Failed carries the positive exit code payload, so it is a
            // payload-bearing variant (like Log.Output.File) rather than a
            // bare unit variant; Success / LaunchFailed are unit variants.
            ChoiceVariant failed_variant;
            failed_variant.name = "Failed";
            failed_variant.fields.push_back(Parameter{.type = ann("integer"), .name = "code"});

            ch->variants.push_back(ChoiceVariant{.name = "Success", .fields = {}});
            ch->variants.push_back(std::move(failed_variant));
            ch->variants.push_back(ChoiceVariant{.name = "LaunchFailed", .fields = {}});

            st.choice_map["Process.ExitStatus"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Process.Error ────────────────────────────────
        // Opt-in typed launch error surfaced via result<Process.CommandOutput,
        // Process.Error> on the run_command_typed slice of Process (leaving the
        // string-error run_command untouched).  Where Process.ExitStatus
        // classifies a command that *ran* (its exit code), Process.Error
        // classifies why a launch *failed* — the two axes are otherwise conflated
        // in the exit_code sign convention.  Variant names must match
        // process_error_variant() in
        // core/runtime/stdlib/system/process_module.cpp exactly (PascalCase).
        // Distinguishes "git isn't installed" (NotFound) from "git ran and exited
        // 1" (ExitStatus.Failed).  Mirrors FileSystem.IoError.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Error");
            ch->variants.push_back(ChoiceVariant{.name = "NotFound", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "PermissionDenied", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "InvalidCommand", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "LaunchFailed", .fields = {}});

            st.choice_map["Process.Error"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Process.Signal ───────────────────────────────
        // Portable termination request consumed by Process.signal(pid, signal).
        // On POSIX the four variants map to SIGTERM / SIGKILL / SIGINT / SIGHUP;
        // on Windows the mapping is lossy (no POSIX signals): Terminate/Kill call
        // TerminateProcess, Interrupt sends a CTRL_C_EVENT, and Hangup degrades to
        // TerminateProcess.  Variant names must match signal_kind_from_variant()
        // in core/runtime/stdlib/system/process_module.cpp exactly (PascalCase).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Signal");
            ch->variants.push_back(ChoiceVariant{.name = "Terminate", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Kill", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Interrupt", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Hangup", .fields = {}});

            st.choice_map["Process.Signal"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

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

        // Terminal.plain_style() constructs these declarative text-style records
        // (type_name "Style"), which Terminal.styled(text, style) renders into one
        // combined ANSI sequence with a single reset.  foreground / background reuse
        // the Terminal.Color choice (its Default variant means "leave unchanged");
        // the six booleans are the standard SGR attributes.  A style is declared
        // once and reused, replacing a nest of bold(color(...)) wrapper calls — the
        // per-attribute Terminal.bold / italic / color functions stay for one-offs.
        add_record(st, "Terminal.Style", field("Terminal.Color", "foreground"),
                   field("Terminal.Color", "background"), field("boolean", "bold"),
                   field("boolean", "dim"), field("boolean", "italic"),
                   field("boolean", "underline"), field("boolean", "inverse"),
                   field("boolean", "strikethrough"));

        // Typed payload delivered to a GraphicalUi.on_mouse_typed callback — the
        // record replacement for the bare {x, y, button, ctrl, shift, alt}
        // dictionary that GraphicalUi.on_mouse hands out.  x / y are `number`
        // (device-pixel measurements, not indices, mirroring the DOM MouseEvent);
        // `button` carries the GraphicalUi.MouseButton choice (fully-qualified so
        // the annotation resolves to the choice, like Terminal.MouseEvent.kind);
        // the three modifier flags are booleans.  Constructed by
        // build_mouse_event_record() in core/runtime/stdlib/io/graphicalui_events.cpp.
        add_record(st, "GraphicalUi.MouseEvent", field("number", "x"), field("number", "y"),
                   field("GraphicalUi.MouseButton", "button"), field("boolean", "ctrl"),
                   field("boolean", "shift"), field("boolean", "alt"));

        // Typed payload delivered to a GraphicalUi.on_scroll_typed callback — the
        // record replacement for the bare {x, y} scroll-position dictionary that
        // GraphicalUi.on_scroll hands out.  x / y are `number` (device-pixel
        // scroll offsets, mirroring the DOM scrollX / scrollY).  Constructed by
        // build_scroll_position_record() in graphicalui_events.cpp.
        add_record(st, "GraphicalUi.ScrollPosition", field("number", "x"), field("number", "y"));

        // Typed payload delivered to a GraphicalUi.on_key_typed callback — the
        // record replacement for the bare key string that GraphicalUi.on_key
        // hands out.  `key` is the pressed key's name (e.g. "s", "Enter",
        // "ArrowUp", mirroring the DOM KeyboardEvent.key); ctrl / shift / alt /
        // meta are booleans reflecting the modifier state the browser already
        // computes for filter matching, so a beginner can branch on Ctrl+S
        // without re-parsing the key text.  Constructed by build_key_event_record()
        // in core/runtime/stdlib/io/graphicalui_events.cpp.
        add_record(st, "GraphicalUi.KeyEvent", field("string", "key"), field("boolean", "ctrl"),
                   field("boolean", "shift"), field("boolean", "alt"), field("boolean", "meta"));

        // Typed payload delivered to a GraphicalUi.on_resize_typed callback — the
        // aggregable record replacement for the two loose integer arguments
        // GraphicalUi.on_resize hands out.  width / height are `integer` (discrete
        // pixel counts); named fields remove the width/height ordering trap and let
        // the last size be stored/passed as one value.  Constructed by
        // build_window_size_record() in graphicalui_events.cpp.
        add_record(st, "GraphicalUi.WindowSize", field("integer", "width"),
                   field("integer", "height"));

        // Typed HTTP result delivered (inside a result<...>) to the callback of
        // GraphicalUi.http_get_full / http_post_full — the structured replacement
        // for the body-only result<string> that http_get / http_post deliver, so a
        // beginner can branch on the status code and read response headers without
        // a second Http-module code path.  Modelled on Http.Response: `status` is
        // an `integer` (discrete status code), `headers` a dictionary<string>, and
        // `body` the response text.  Built by build_http_response_record_gui() in
        // graphicalui_commands.cpp.
        add_record(st, "GraphicalUi.HttpResponse", field("integer", "status"),
                   field_of(dict_ann("string"), "headers"), field("string", "body"));

        // Typed payload delivered to a GraphicalUi.on_drag_typed callback — the
        // record replacement for the untyped position dictionary GraphicalUi.on_drag
        // hands out.  x / y are `number` (device-pixel pointer coordinates,
        // mirroring MouseEvent); `data` is the dragged payload string; `phase`
        // carries the GraphicalUi.DragPhase choice (fully-qualified so the
        // annotation resolves to the choice).  Constructed by
        // build_drag_event_record() in graphicalui_events.cpp.
        add_record(st, "GraphicalUi.DragEvent", field("number", "x"), field("number", "y"),
                   field("string", "data"), field("GraphicalUi.DragPhase", "phase"));

        // Typed payload delivered to a GraphicalUi.drop_target_typed callback — the
        // record replacement for the bare data string GraphicalUi.drop_target hands
        // its on_drop callback, adding the drop location so a beginner can build
        // reordering / drop-to-position interactions.  Symmetric with
        // GraphicalUi.DragEvent: `data` is the dragged payload string; x / y are
        // `number` (device-pixel drop coordinates, mirroring DragEvent).
        // Constructed by build_drop_event_record() in graphicalui_events.cpp.
        add_record(st, "GraphicalUi.DropEvent", field("string", "data"), field("number", "x"),
                   field("number", "y"));

        // Typed payload delivered to a GraphicalUi.on_storage_change_typed callback
        // — the record replacement for the bare new-value string
        // GraphicalUi.on_storage_change hands out when another tab rewrites a
        // localStorage key.  `key` is the changed key; `old_value` / `new_value`
        // are optional<string> because a key can be added (no old value) or
        // cleared (no new value) — honouring no-null.  Modelled on the web
        // StorageEvent.  Constructed by build_storage_event_record() in
        // graphicalui_events.cpp.
        add_record(st, "GraphicalUi.StorageEvent", field("string", "key"),
                   field_of(optional_ann("string"), "old_value"),
                   field_of(optional_ann("string"), "new_value"));

        // Typed payload delivered to a GraphicalUi.on_wheel_typed callback — the
        // scroll-wheel delta the untyped mouse "scroll" event never exposes.
        // delta_x / delta_y are `number` (device-pixel wheel deltas, mirroring the
        // DOM WheelEvent.deltaX / deltaY) so a beginner can build custom zoom /
        // horizontal-scroll / carousel interactions.  Constructed by
        // build_wheel_delta_record() in graphicalui_events.cpp.
        add_record(st, "GraphicalUi.WheelDelta", field("number", "delta_x"),
                   field("number", "delta_y"));

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

        // ── DateTime.ParseError ─────────────────────────
        // Opt-in typed parse error surfaced via result<number, DateTime.ParseError>
        // on the from_iso_string_typed slice of DateTime (leaving the string-error
        // from_iso_string untouched).  Lets a program distinguish empty input from
        // a malformed shape from an impossible date (month 13) without substring-
        // matching the message.  Variant names must match parse_error_variant() in
        // core/runtime/stdlib/system/datetime_module.cpp exactly (PascalCase, one
        // per IsoParseErrorKind).  Mirrors FileSystem.IoError / Http.Error.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "ParseError");
            ch->variants.push_back(ChoiceVariant{.name = "Empty", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "InvalidFormat", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "OutOfRange", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "UnsupportedPrecision", .fields = {}});

            st.choice_map["DateTime.ParseError"] = ch.get();
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

        // ── Random.Distribution ─────────────────────────
        // Consumed by Random.sample_from(distribution) -> result<number> — a
        // closed set of probability distributions to draw from, so the caller
        // states intent ("draw from a Normal(0, 1)") instead of composing raw
        // uniform draws by hand. Uniform carries its inclusive [low, high]
        // bounds; Normal carries mean and standard_deviation for a Box–Muller
        // draw; Exponential carries its rate (lambda) for an inverse-transform
        // draw. Bernoulli/Binomial/Poisson are discrete (sample_from returns an
        // integer-valued number); Gamma/LogNormal are continuous, skewed and
        // positive. Variant names/fields must match the match in
        // core/runtime/stdlib/system/random_module.cpp exactly (PascalCase).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Distribution");

            // Parameter is move-only, so payload variants are built by moving
            // each Parameter into the fields vector (mirrors Xml.Node above).
            ChoiceVariant uniform;
            uniform.name = "Uniform";
            uniform.fields.push_back(Parameter{.type = ann("number"), .name = "low"});
            uniform.fields.push_back(Parameter{.type = ann("number"), .name = "high"});
            ch->variants.push_back(std::move(uniform));

            ChoiceVariant normal;
            normal.name = "Normal";
            normal.fields.push_back(Parameter{.type = ann("number"), .name = "mean"});
            normal.fields.push_back(Parameter{.type = ann("number"), .name = "standard_deviation"});
            ch->variants.push_back(std::move(normal));

            ChoiceVariant exponential;
            exponential.name = "Exponential";
            exponential.fields.push_back(Parameter{.type = ann("number"), .name = "rate"});
            ch->variants.push_back(std::move(exponential));

            // Discrete: a weighted coin flip.  sample_from draws 1.0 with
            // probability p and 0.0 otherwise (an integer-valued number).
            ChoiceVariant bernoulli;
            bernoulli.name = "Bernoulli";
            bernoulli.fields.push_back(Parameter{.type = ann("number"), .name = "probability"});
            ch->variants.push_back(std::move(bernoulli));

            // Discrete: the number of successes in `trials` independent
            // probability-`p` Bernoulli trials.  trials is an integer count;
            // sample_from draws an integer-valued number in [0, trials].
            ChoiceVariant binomial;
            binomial.name = "Binomial";
            binomial.fields.push_back(Parameter{.type = ann("integer"), .name = "trials"});
            binomial.fields.push_back(Parameter{.type = ann("number"), .name = "probability"});
            ch->variants.push_back(std::move(binomial));

            // Discrete: the number of events in a unit interval given a mean
            // event rate.  sample_from draws a non-negative integer-valued number.
            ChoiceVariant poisson;
            poisson.name = "Poisson";
            poisson.fields.push_back(Parameter{.type = ann("number"), .name = "rate"});
            ch->variants.push_back(std::move(poisson));

            // Continuous: a right-skewed positive distribution parameterised by
            // shape (k) and scale (theta).
            ChoiceVariant gamma;
            gamma.name = "Gamma";
            gamma.fields.push_back(Parameter{.type = ann("number"), .name = "shape"});
            gamma.fields.push_back(Parameter{.type = ann("number"), .name = "scale"});
            ch->variants.push_back(std::move(gamma));

            // Continuous: a variable whose natural logarithm is normally
            // distributed with the given mean and standard deviation.
            ChoiceVariant log_normal;
            log_normal.name = "LogNormal";
            log_normal.fields.push_back(Parameter{.type = ann("number"), .name = "mean"});
            log_normal.fields.push_back(
                Parameter{.type = ann("number"), .name = "standard_deviation"});
            ch->variants.push_back(std::move(log_normal));

            st.choice_map["Random.Distribution"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // Random.uuid_typed() / Random.parse_uuid() produce this typed wrapper over
        // a validated canonical UUID string (8-4-4-4-12 hex), so a UUID is
        // distinguishable from any other string and can be validated on the way in.
        // The single `value` field holds the canonical text; Random.uuid_to_string
        // reads it back out.  Mirrors Socket.IpAddress (a typed wrapper over an
        // address that is otherwise a string).
        add_record(st, "Random.Uuid", field("string", "value"));

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

        // ── Decimal.Error ───────────────────────────────
        // Opt-in typed error surfaced via result<decimal, Decimal.Error> on the
        // from_string_typed / divide_typed slices of Decimal (leaving the
        // string-error from_string / divide untouched).  Lets a program branch on
        // "user typed nonsense" vs "divided by zero" vs "exceeds precision" without
        // substring-matching the message.  Variant names must match
        // decimal_error_variant() in
        // core/runtime/stdlib/math/decimal_module.cpp exactly (PascalCase).
        // Mirrors FileSystem.IoError / Http.Error / DateTime.ParseError.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Error");
            ch->variants.push_back(ChoiceVariant{.name = "InvalidFormat", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "DivisionByZero", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Overflow", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "PrecisionExceeded", .fields = {}});

            st.choice_map["Decimal.Error"] = ch.get();
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

        // ── Encoder.Error ───────────────────────────────
        // Opt-in typed error surfaced via result<string, Encoder.Error> on the
        // decode_base64_typed / decode_url_typed / decode_text_typed slice of
        // Encoder (leaving the string-error decoders untouched).  Lets a learner
        // validating user-supplied encoded input branch on *why* a decode failed:
        // a bad base64 alphabet/padding (InvalidBase64), a malformed percent-
        // escape (InvalidPercentEncoding), bytes that are not valid UTF-8
        // (InvalidUtf8), or bytes outside the ASCII range (InvalidAscii) — instead
        // of substring-matching an opaque message.  Variant names must match
        // make_encoder_error_choice() in
        // core/runtime/stdlib/system/encoder_module.cpp exactly (PascalCase).
        // Mirrors the DateTime.ParseError / FileSystem.IoError prototypes.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Error");
            ch->variants.push_back(ChoiceVariant{.name = "InvalidBase64", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "InvalidPercentEncoding", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "InvalidUtf8", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "InvalidAscii", .fields = {}});

            st.choice_map["Encoder.Error"] = ch.get();
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

        // ── Http.Auth ───────────────────────────────────
        // Request credentials as a closed, exhaustive set: Basic(username, password)
        // carries HTTP Basic credentials, Bearer(token) a bearer/OAuth token.  Both
        // are payload-bearing variants (like Log.Output.File), so a typo in the scheme
        // is a compile error instead of a hand-built "Authrization" header.  Rendered
        // into the Authorization header value by Http.authorization_header (base64 via
        // the shared base64 codec for Basic), mirroring Http.Method / Log.Output.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Auth");

            // Both variants carry string payloads, so build them by moving Parameters
            // into the fields vector (a Parameter holds a move-only default_value).
            ChoiceVariant basic_variant;
            basic_variant.name = "Basic";
            basic_variant.fields.push_back(Parameter{.type = ann("string"), .name = "username"});
            basic_variant.fields.push_back(Parameter{.type = ann("string"), .name = "password"});

            ChoiceVariant bearer_variant;
            bearer_variant.name = "Bearer";
            bearer_variant.fields.push_back(Parameter{.type = ann("string"), .name = "token"});

            ch->variants.push_back(std::move(basic_variant));
            ch->variants.push_back(std::move(bearer_variant));

            st.choice_map["Http.Auth"] = ch.get();
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

        // ── Terminal.CursorStyle ────────────────────────
        // Typed cursor-shape selector consumed by Terminal.set_cursor_style,
        // which emits the matching DECSCUSR escape ("\x1b[<n> q").  A typed choice
        // keeps the six shapes discoverable and match-exhaustive over a bare
        // integer.  Variant names/order must match the DECSCUSR mapping in
        // set_cursor_style in core/runtime/stdlib/io/terminal_module.cpp:
        // BlinkingBlock=1, SteadyBlock=2, BlinkingUnderline=3, SteadyUnderline=4,
        // BlinkingBar=5, SteadyBar=6.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "CursorStyle");
            ch->variants.push_back(ChoiceVariant{.name = "BlinkingBlock", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "SteadyBlock", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BlinkingUnderline", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "SteadyUnderline", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BlinkingBar", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "SteadyBar", .fields = {}});

            st.choice_map["Terminal.CursorStyle"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.MouseButton ─────────────────────
        // Which pointer button a GraphicalUi.MouseEvent refers to.  A closed,
        // exhaustively-matchable choice replacing the stringly-typed "left" /
        // "middle" / "right" the on_mouse dictionary carries in its `button` key.
        // Variant names/order must match button_from_string() in
        // core/runtime/stdlib/io/graphicalui_events.cpp and the JS button map in
        // external/gui-framework/gui-subscriptions.js (index 0/1/2 → Left/Middle/
        // Right).  A non-left/middle/right value falls back to Left, so the choice
        // stays total over anything the browser can emit.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "MouseButton");
            ch->variants.push_back(ChoiceVariant{.name = "Left", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Middle", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Right", .fields = {}});

            st.choice_map["GraphicalUi.MouseButton"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.Severity ────────────────────────
        // Alert / toast severity accepted by GraphicalUi.alert_of / toast_of and
        // bridged to a string by GraphicalUi.severity_to_string.  A closed choice
        // the type checker can enforce, replacing the open severity string (and
        // the GraphicalUi.INFO/WARNING/ERROR/SUCCESS constants, which stay).
        // Variant names must match severity_to_lower() in
        // core/runtime/stdlib/io/graphicalui_helpers.hpp (Info → "info", etc.).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Severity");
            ch->variants.push_back(ChoiceVariant{.name = "Info", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Warning", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Error", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Success", .fields = {}});

            st.choice_map["GraphicalUi.Severity"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.ButtonVariant ───────────────────
        // Button-hierarchy style accepted by GraphicalUi.button_of and bridged to
        // a string by GraphicalUi.button_variant_to_string.  A closed choice
        // paralleling the GraphicalUi.PRIMARY/SECONDARY/GHOST/DANGER constants
        // (which stay).  Variant names must match button_variant_to_lower() in
        // core/runtime/stdlib/io/graphicalui_helpers.hpp (Primary → "primary").
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "ButtonVariant");
            ch->variants.push_back(ChoiceVariant{.name = "Primary", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Secondary", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Ghost", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Danger", .fields = {}});

            st.choice_map["GraphicalUi.ButtonVariant"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.DeviceClass ─────────────────────
        // Coarse device-size bucket returned by GraphicalUi.classify_device_typed,
        // replacing the "phone" / "tablet" / "desktop" / "big_desktop" strings the
        // classify_device dictionary carries in its `class` key.  Variant
        // names/order must match register_classify_device_typed() in
        // core/runtime/stdlib/io/graphicalui_widgets_layout.cpp (Phone < 640 <
        // Tablet < 1024 < Desktop < 1920 ≤ BigDesktop).  Mirrors
        // FileSystem.FileKind — a classifier that returns a closed choice.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "DeviceClass");
            ch->variants.push_back(ChoiceVariant{.name = "Phone", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Tablet", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Desktop", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "BigDesktop", .fields = {}});

            st.choice_map["GraphicalUi.DeviceClass"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.Orientation ─────────────────────
        // Screen orientation returned in GraphicalUi.DeviceInfo.orientation.
        // Landscape when width ≥ height, else Portrait — must match
        // register_classify_device_typed().
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Orientation");
            ch->variants.push_back(ChoiceVariant{.name = "Portrait", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Landscape", .fields = {}});

            st.choice_map["GraphicalUi.Orientation"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.MouseEventType ──────────────────
        // Which pointer-event kind a GraphicalUi.on_mouse / on_mouse_of
        // subscription listens for.  A closed, exhaustively-matchable choice
        // replacing the open "click" / "move" / "down" / "up" / "scroll" event
        // string on_mouse takes — a typo like "mouseup" silently never fires,
        // whereas the choice is enforced at the type level.  Variant names/order
        // must match mouse_event_type_to_string() in
        // core/runtime/stdlib/io/graphicalui_helpers.hpp and the JS evtMap in
        // external/gui-framework/gui-subscriptions.js (Click/Move/Down/Up/Scroll).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "MouseEventType");
            ch->variants.push_back(ChoiceVariant{.name = "Click", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Move", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Down", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Up", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Scroll", .fields = {}});

            st.choice_map["GraphicalUi.MouseEventType"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.DragPhase ───────────────────────
        // Which phase of a drag gesture a GraphicalUi.DragEvent refers to.  A
        // closed, exhaustively-matchable choice replacing the stringly-typed drag
        // event_type filter on_drag takes.  Variant names/order must match
        // drag_phase_from_string() in
        // core/runtime/stdlib/io/graphicalui_helpers.hpp and the DOM drag events
        // (Start/Move/End/Enter/Leave/Drop → dragstart/drag/dragend/dragenter/
        // dragleave/drop).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "DragPhase");
            ch->variants.push_back(ChoiceVariant{.name = "Start", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Move", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "End", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Enter", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Leave", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Drop", .fields = {}});

            st.choice_map["GraphicalUi.DragPhase"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.VisibilityState ─────────────────
        // Whether the document is currently Visible or Hidden — the closed,
        // exhaustively-matchable replacement for the bare boolean
        // GraphicalUi.on_visibility_change delivers (removing the "which way does
        // the flag point?" trap).  Delivered to a GraphicalUi.on_visibility_change_typed
        // callback and bridged to a string by GraphicalUi.visibility_state_to_string.
        // Variant names/order must match visibility_state_from_visible() in
        // core/runtime/stdlib/io/graphicalui_helpers.hpp (Page Visibility API:
        // !document.hidden → Visible, else Hidden).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "VisibilityState");
            ch->variants.push_back(ChoiceVariant{.name = "Visible", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Hidden", .fields = {}});

            st.choice_map["GraphicalUi.VisibilityState"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.ThemeMode ───────────────────────
        // Theme-mode override accepted by GraphicalUi.set_theme_mode_of and bridged
        // to a string by GraphicalUi.theme_mode_to_string.  A closed choice the type
        // checker enforces, replacing the open "light"/"dark"/"auto" string the
        // GraphicalUi.set_theme_mode command takes (which stays).  Variant names
        // must match theme_mode_to_lower() in
        // core/runtime/stdlib/io/graphicalui_helpers.hpp (Light → "light", etc.).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "ThemeMode");
            ch->variants.push_back(ChoiceVariant{.name = "Light", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Dark", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Auto", .fields = {}});

            st.choice_map["GraphicalUi.ThemeMode"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.ScrollBehavior ──────────────────
        // Scroll-animation behaviour accepted by GraphicalUi.scroll_to_of, lowered
        // to the behavior string GraphicalUi.scroll_to takes (which stays).  A
        // closed choice mirroring the web scrollIntoView({behavior}) values.
        // Variant names must match scroll_behavior_to_lower() in
        // core/runtime/stdlib/io/graphicalui_helpers.hpp (Smooth → "smooth", etc.).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "ScrollBehavior");
            ch->variants.push_back(ChoiceVariant{.name = "Smooth", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Instant", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Auto", .fields = {}});

            st.choice_map["GraphicalUi.ScrollBehavior"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── GraphicalUi.SortDirection ───────────────────
        // Table sort direction bridged to a string by
        // GraphicalUi.sort_direction_to_string and accepted directly as the
        // GraphicalUi.table `sort_direction` option (lowered to "asc"/"desc").  A
        // typed value the model can store instead of a raw direction string.
        // Variant names must match sort_direction_to_lower() in
        // core/runtime/stdlib/io/graphicalui_helpers.hpp (Ascending → "asc").
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "SortDirection");
            ch->variants.push_back(ChoiceVariant{.name = "Ascending", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Descending", .fields = {}});

            st.choice_map["GraphicalUi.SortDirection"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // Typed result of GraphicalUi.classify_device_typed(width, height): the
        // record replacement for the {class, orientation, width, height}
        // dictionary that classify_device returns.  `class` / `orientation` are
        // fully-qualified so the annotations resolve to the choices; width /
        // height are `integer` (discrete pixel counts).
        add_record(st, "GraphicalUi.DeviceInfo", field("GraphicalUi.DeviceClass", "class"),
                   field("GraphicalUi.Orientation", "orientation"), field("integer", "width"),
                   field("integer", "height"));

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

        // ── Math.Angle ──────────────────────────────────
        // Optional unit-safe angle: a payload-carrying choice that makes the
        // radians-vs-degrees distinction explicit at the call site, so mixing the
        // two becomes a visible choice rather than a silent bug.  Both payloads
        // are a `number` measurement.  Consumed by Math.to_radians /
        // Math.to_degrees / Math.sin_of; the existing number-radians trig APIs
        // stay primary, so this is a convenience, not a replacement.  Variant
        // names must match angle_to_radians() in
        // core/runtime/stdlib/math/math_module.cpp exactly (Radians / Degrees).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Angle");

            ChoiceVariant radians_variant;
            radians_variant.name = "Radians";
            radians_variant.fields.push_back(Parameter{.type = ann("number"), .name = "value"});

            ChoiceVariant degrees_variant;
            degrees_variant.name = "Degrees";
            degrees_variant.fields.push_back(Parameter{.type = ann("number"), .name = "value"});

            ch->variants.push_back(std::move(radians_variant));
            ch->variants.push_back(std::move(degrees_variant));

            st.choice_map["Math.Angle"] = ch.get();
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

        // ── Http.Error ──────────────────────────────────
        // Opt-in typed transport error surfaced via result<Http.Response,
        // Http.Error> on the get_typed slice of Http (leaving the string-error
        // get/post/… untouched).  Variant names must match http_error_variant()
        // in core/runtime/stdlib/io/http_module_request.cpp exactly (PascalCase).
        // A closed, match-able set of transport-level failure categories, so a
        // program can retry only on Timeout, fall back only on ConnectionFailed,
        // or distinguish an SSRF-Blocked URL from a Malformed one — instead of
        // brittle substring matching on an opaque error string.  Mirrors the
        // FileSystem.IoError prototype, generalising the typed-error pattern to
        // the highest-traffic stdlib failure surface.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Error");
            ch->variants.push_back(ChoiceVariant{.name = "InvalidUrl", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "ConnectionFailed", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Timeout", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "TlsError", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "TooManyRedirects", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Blocked", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Malformed", .fields = {}});

            st.choice_map["Http.Error"] = ch.get();
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

        // Hash.<algo>_typed produces this algorithm-tagged digest record, pairing
        // the hex output with the Hash.Algorithm that produced it so a SHA-256 and
        // an MD5 digest are no longer the same (bare-string) type and cannot be
        // compared across algorithms by accident.  The `algorithm` field is the
        // existing Hash.Algorithm choice; `hex` is the lowercase hex digest.
        add_record(st, "Hash.Digest", field("Hash.Algorithm", "algorithm"), field("string", "hex"));

        // ── Compression.Format ──────────────────────────
        // Selects the compression algorithm for the generic Compression.compress /
        // Compression.decompress entry points, mirroring the Hash.Algorithm +
        // Hash.digest dual of "several named algorithm functions plus one
        // choice-dispatched generic function".  Unlike Hash.Algorithm, this has
        // no string dual-form — Compression.Format is the sole runtime-dispatch
        // path, while the per-algorithm functions (deflate/inflate, gzip/gunzip,
        // encode_rle/decode_rle) stay primary.  Variant names must match
        // require_format_variant() in core/runtime/stdlib/system/compression_module.cpp
        // exactly (PascalCase).
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Format");
            ch->variants.push_back(ChoiceVariant{.name = "Deflate", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Gzip", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Zlib", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Rle", .fields = {}});

            st.choice_map["Compression.Format"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── Compression.Error ───────────────────────────
        // Opt-in typed error surfaced via result<string, Compression.Error> on
        // the decompress_typed / inflate_typed / gunzip_typed slice of
        // Compression (leaving the string-error decompress / inflate / gunzip
        // untouched).  Lets a beginner decompressing an untrusted or truncated
        // blob tell "corrupt data" (Corrupt) from "wrong container" (Gzip magic
        // or non-deflate method → UnsupportedFormat) from "stream ended early"
        // (Truncated) from "output too large" (TooLarge) — instead of substring-
        // matching an opaque message.  Variant names must match
        // make_compression_error_choice() in
        // core/runtime/stdlib/system/compression_module.cpp exactly (PascalCase),
        // and map 1:1 onto compression::DecodeError.  Mirrors the
        // FileSystem.IoError / RegularExpression.Error prototypes.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Error");
            ch->variants.push_back(ChoiceVariant{.name = "Corrupt", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Truncated", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "UnsupportedFormat", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "TooLarge", .fields = {}});

            st.choice_map["Compression.Error"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── RegularExpression.Error ─────────────────────
        // Opt-in typed error surfaced via result<string, RegularExpression.Error>
        // on RegularExpression.compile_typed (the string payload on success is the
        // validated pattern, reusable in matches/find/replace), leaving the
        // existing string-error / boolean functions untouched.  Turns the module's
        // existing internal distinction into a teachable, exhaustive choice: a
        // typo (`InvalidSyntax(message)`, carrying the engine's diagnostic), a
        // pattern rejected as catastrophically slow (`Unsafe`, the ReDoS guard),
        // and a pattern past the size limit (`TooLarge`).  Variant names must match
        // regex_error_variant() in
        // core/runtime/stdlib/text/regularexpression_module.cpp exactly.  Mirrors
        // the FileSystem.IoError / Http.Error / Socket.Error prototypes.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Error");

            auto invalid_syntax = ChoiceVariant{};
            invalid_syntax.name = "InvalidSyntax";
            invalid_syntax.fields.push_back(Parameter{.type = ann("string"), .name = "message"});

            ch->variants.push_back(std::move(invalid_syntax));
            ch->variants.push_back(ChoiceVariant{.name = "Unsafe", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "TooLarge", .fields = {}});

            st.choice_map["RegularExpression.Error"] = ch.get();
            st.choices.push_back(std::move(ch));
        }

        // ── RegularExpression.Flags ─────────────────────
        // Typed flag set for the flag-accepting variants (matches_with /
        // find_with / find_all_with / replace_with / replace_all_with /
        // split_with), each taking a flags: array<RegularExpression.Flags>.  A
        // typed choice keeps the options discoverable and match-exhaustive over a
        // bare string.  Variant names must match parse_regex_flags() in
        // core/runtime/stdlib/text/regularexpression_module.cpp exactly:
        // CaseInsensitive → std::regex::icase, MultiLine → std::regex::multiline,
        // and DotAll (no std::regex equivalent) → a `.`-to-`[\s\S]` pattern
        // rewrite so `.` also matches a newline.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Flags");
            ch->variants.push_back(ChoiceVariant{.name = "CaseInsensitive", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "MultiLine", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "DotAll", .fields = {}});

            st.choice_map["RegularExpression.Flags"] = ch.get();
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

        // ── Socket.Error ────────────────────────────────
        // Opt-in typed transport error surfaced via result<T, Socket.Error> on
        // the *_typed slice of Socket (connect_typed / listen_typed / send_typed
        // / receive_typed), leaving the string-error connect/listen/send/receive
        // untouched.  Variant names must match socket_error_variant() in
        // core/runtime/stdlib/io/socket_module.cpp exactly (PascalCase).  A
        // closed, match-able set of transport-level failure categories, so a
        // program can retry only on Timeout, fall back only on ConnectionRefused,
        // or report a HostUnreachable target — instead of brittle substring
        // matching on an opaque error string.  Mirrors the FileSystem.IoError /
        // Http.Error prototypes.
        {
            auto ch = std::make_unique<ChoiceDeclaration>(SourceLocation{}, "Error");
            ch->variants.push_back(ChoiceVariant{.name = "ConnectionRefused", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Timeout", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "HostUnreachable", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "AddressInUse", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "ConnectionReset", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "NotConnected", .fields = {}});
            ch->variants.push_back(ChoiceVariant{.name = "Other", .fields = {}});

            st.choice_map["Socket.Error"] = ch.get();
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
