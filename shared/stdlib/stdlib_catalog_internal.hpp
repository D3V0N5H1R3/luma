#ifndef LUMA_STDLIB_CATALOG_INTERNAL_HPP
#define LUMA_STDLIB_CATALOG_INTERNAL_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers for stdlib catalog registration
// ─────────────────────────────────────────────────────────────────────────────
// ModuleBuilder, ParamShorthands, and per-group registration functions used
// by the split catalog source files.  Not part of the public API.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "stdlib/stdlib_catalog.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::stdlib::detail {

// Move-append a braced list of specs into `specs`.
//
// A braced-init-list binds to the rvalue array reference and deduces N, so the
// elements are non-const and can be moved out — unlike std::initializer_list,
// whose elements are const and force a copy of every FunctionSpec (two heap
// std::strings + a param_types vector + ReturnTypeDescs each).  The braced
// temporaries are constructed in place and then moved into the vector, so each
// spec is built exactly once and never copied.
template <std::size_t N>
void append_specs(std::vector<FunctionSpec>& specs, FunctionSpec (&&items)[N]) {
    specs.reserve(specs.size() + N);

    for (auto& item : items) {
        specs.push_back(std::move(item));
    }
}

// Short alias for the return-type descriptor used throughout the per-module
// registration functions (e.g. R::integer_type(), R::result_number()).
using R = ReturnTypeDesc;

// ─── Named-type descriptors ──────────────────────────────────────────
// Single source of truth for every named stdlib type (handle types such as
// `widget`/`socket` and record types such as `Response`/`TimeParts`).  Each
// helper wraps ReturnTypeDesc::named("...") so the identifier string appears
// exactly once.  Registration sites reference these instead of repeating a
// raw string literal, which turns a mistyped type name into a compile error
// (a bare R::named("wiget") would compile and silently mis-type the catalog).
//
// The ParamShorthands below draw their named fields from these helpers too,
// so parameter and return positions share the same canonical definitions.
namespace named {

// Handle types (identified by a lowercase keyword).
[[nodiscard]] inline ReturnTypeDesc widget() {
    return ReturnTypeDesc::named("widget");
}

[[nodiscard]] inline ReturnTypeDesc set() {
    return ReturnTypeDesc::named("set");
}

[[nodiscard]] inline ReturnTypeDesc queue() {
    return ReturnTypeDesc::named("queue");
}

[[nodiscard]] inline ReturnTypeDesc stack() {
    return ReturnTypeDesc::named("stack");
}

[[nodiscard]] inline ReturnTypeDesc linked_list() {
    return ReturnTypeDesc::named("linked_list");
}

[[nodiscard]] inline ReturnTypeDesc hash_set() {
    return ReturnTypeDesc::named("hash_set");
}

[[nodiscard]] inline ReturnTypeDesc binary_tree() {
    return ReturnTypeDesc::named("binary_tree");
}

[[nodiscard]] inline ReturnTypeDesc graph() {
    return ReturnTypeDesc::named("graph");
}

[[nodiscard]] inline ReturnTypeDesc socket() {
    return ReturnTypeDesc::named("socket");
}

[[nodiscard]] inline ReturnTypeDesc decimal() {
    return ReturnTypeDesc::named("decimal");
}

[[nodiscard]] inline ReturnTypeDesc xml() {
    return ReturnTypeDesc::named("xml");
}

[[nodiscard]] inline ReturnTypeDesc key_value_store() {
    return ReturnTypeDesc::named("key_value_store");
}

[[nodiscard]] inline ReturnTypeDesc log_level() {
    return ReturnTypeDesc::named("Log.Level");
}

[[nodiscard]] inline ReturnTypeDesc weekday() {
    return ReturnTypeDesc::named("DateTime.Weekday");
}

[[nodiscard]] inline ReturnTypeDesc month() {
    return ReturnTypeDesc::named("DateTime.Month");
}

[[nodiscard]] inline ReturnTypeDesc ordering() {
    return ReturnTypeDesc::named("Ordering");
}

// Top-level choice (bare name, no namespace) — mirrors ordering() above.
[[nodiscard]] inline ReturnTypeDesc sign() {
    return ReturnTypeDesc::named("Sign");
}

[[nodiscard]] inline ReturnTypeDesc json_value() {
    return ReturnTypeDesc::named("Json.Value");
}

// Xml.to_node returns this recursive choice directly, so — like json_value() —
// it uses the fully-qualified name (choices resolve qualified).
[[nodiscard]] inline ReturnTypeDesc xml_node() {
    return ReturnTypeDesc::named("Xml.Node");
}

// Choice type — uses the fully-qualified name so the type checker resolves it as
// Http.StatusClass (a bare name would fall through to the record default).
[[nodiscard]] inline ReturnTypeDesc status_class() {
    return ReturnTypeDesc::named("Http.StatusClass");
}

// Record / result types (identified by a PascalCase name).
[[nodiscard]] inline ReturnTypeDesc response() {
    return ReturnTypeDesc::named("Response");
}

[[nodiscard]] inline ReturnTypeDesc request() {
    return ReturnTypeDesc::named("Request");
}

[[nodiscard]] inline ReturnTypeDesc match() {
    return ReturnTypeDesc::named("Match");
}

[[nodiscard]] inline ReturnTypeDesc dialect() {
    return ReturnTypeDesc::named("Dialect");
}

[[nodiscard]] inline ReturnTypeDesc key_value() {
    return ReturnTypeDesc::named("KeyValue");
}

// Graph.edges emits an array of these edge records (bare name, like key_value()).
[[nodiscard]] inline ReturnTypeDesc edge() {
    return ReturnTypeDesc::named("Edge");
}

// Graph.shortest_path_detailed() returns this route+cost record (bare name).
[[nodiscard]] inline ReturnTypeDesc path() {
    return ReturnTypeDesc::named("Path");
}

[[nodiscard]] inline ReturnTypeDesc time_parts() {
    return ReturnTypeDesc::named("TimeParts");
}

[[nodiscard]] inline ReturnTypeDesc duration() {
    return ReturnTypeDesc::named("Duration");
}

// DateTime.interval() / interval_* take and return these range records (bare name).
[[nodiscard]] inline ReturnTypeDesc interval() {
    return ReturnTypeDesc::named("Interval");
}

// DateTime.zoned() / zoned_* take and return these offset-aware timestamp records
// (bare short name "Zoned", like interval()).
[[nodiscard]] inline ReturnTypeDesc zoned() {
    return ReturnTypeDesc::named("Zoned");
}

[[nodiscard]] inline ReturnTypeDesc file_info() {
    return ReturnTypeDesc::named("FileInfo");
}

// Choice type — uses the fully-qualified name so the type checker resolves it as
// FileSystem.FileKind (a bare name would fall through to the record default).
[[nodiscard]] inline ReturnTypeDesc file_kind() {
    return ReturnTypeDesc::named("FileSystem.FileKind");
}

// Typed I/O error choice (fully-qualified, like file_kind).  Used as the error
// parameter of result<T, FileSystem.IoError> on the read_file_typed slice.
[[nodiscard]] inline ReturnTypeDesc file_io_error() {
    return ReturnTypeDesc::named("FileSystem.IoError");
}

[[nodiscard]] inline ReturnTypeDesc path_parts() {
    return ReturnTypeDesc::named("PathParts");
}

[[nodiscard]] inline ReturnTypeDesc summary() {
    return ReturnTypeDesc::named("Summary");
}

// Math.fraction() / fraction_* take and return these exact-rational records
// (bare name, resolved as a record — mirrors summary() / interval()).
[[nodiscard]] inline ReturnTypeDesc fraction() {
    return ReturnTypeDesc::named("Fraction");
}

// Math.complex() / complex_* take and return these complex-number records.
[[nodiscard]] inline ReturnTypeDesc complex() {
    return ReturnTypeDesc::named("Complex");
}

// Math.vector2() / vec2_* and Math.vector3() / vec3_* take and return these
// geometry records (bare short names, like complex()).
[[nodiscard]] inline ReturnTypeDesc vector2() {
    return ReturnTypeDesc::named("Vector2");
}

[[nodiscard]] inline ReturnTypeDesc vector3() {
    return ReturnTypeDesc::named("Vector3");
}

// Math.matrix2() / mat2_* and Math.matrix3() / mat3_* take and return these
// typed transform-matrix records (bare short names, like vector2()).
[[nodiscard]] inline ReturnTypeDesc matrix2() {
    return ReturnTypeDesc::named("Matrix2");
}

[[nodiscard]] inline ReturnTypeDesc matrix3() {
    return ReturnTypeDesc::named("Matrix3");
}

// Math.five_number_summary() returns this box-plot record (bare short name).
[[nodiscard]] inline ReturnTypeDesc five_number_summary() {
    return ReturnTypeDesc::named("FiveNumberSummary");
}

// Math.linear_fit() returns this least-squares regression record.
[[nodiscard]] inline ReturnTypeDesc line_fit() {
    return ReturnTypeDesc::named("LineFit");
}

// Json.parse_detailed() surfaces this located parse-failure record as its
// result error type.
[[nodiscard]] inline ReturnTypeDesc json_parse_error() {
    return ReturnTypeDesc::named("ParseError");
}

// Csv.deserialize_detailed() / Xml.deserialize_detailed() surface the same
// located parse-failure record shape (message, line, column) as their result
// error type — bare "ParseError", mirroring json_parse_error().
[[nodiscard]] inline ReturnTypeDesc csv_parse_error() {
    return ReturnTypeDesc::named("ParseError");
}

[[nodiscard]] inline ReturnTypeDesc xml_parse_error() {
    return ReturnTypeDesc::named("ParseError");
}

// Color.* construct and consume these RGBA colour records.  The module is named
// Color, so the record's qualified name is Color.Color (bare short name "Color").
[[nodiscard]] inline ReturnTypeDesc color() {
    return ReturnTypeDesc::named("Color");
}

// Color.to_hsl / from_hsl / rotate_hue pivot through this hue/saturation/lightness
// record (bare short name "Hsl", like color()).
[[nodiscard]] inline ReturnTypeDesc hsl() {
    return ReturnTypeDesc::named("Hsl");
}

// Color.to_hsv / from_hsv pivot through this hue/saturation/value record (bare
// short name "Hsv", like hsl()).
[[nodiscard]] inline ReturnTypeDesc hsv() {
    return ReturnTypeDesc::named("Hsv");
}

// Encoder.encode_text / decode_text take this text-encoding selector choice.
[[nodiscard]] inline ReturnTypeDesc encoding() {
    return ReturnTypeDesc::named("Encoder.Encoding");
}

[[nodiscard]] inline ReturnTypeDesc process_result() {
    return ReturnTypeDesc::named("ProcessResult");
}

[[nodiscard]] inline ReturnTypeDesc command_output() {
    return ReturnTypeDesc::named("CommandOutput");
}

// Process.command() builds this shell-free command record consumed by
// Process.run_command (bare short name "Command", like command_output()).
[[nodiscard]] inline ReturnTypeDesc command() {
    return ReturnTypeDesc::named("Command");
}

[[nodiscard]] inline ReturnTypeDesc input_event() {
    return ReturnTypeDesc::named("InputEvent");
}

[[nodiscard]] inline ReturnTypeDesc mouse_event() {
    return ReturnTypeDesc::named("MouseEvent");
}

// Terminal.parse_key returns this choice directly, so — like json_value() and
// file_kind() — it uses the fully-qualified name (choices resolve qualified;
// records such as input_event()/mouse_event() resolve by bare name).
[[nodiscard]] inline ReturnTypeDesc key() {
    return ReturnTypeDesc::named("Terminal.Key");
}

[[nodiscard]] inline ReturnTypeDesc cursor_position() {
    return ReturnTypeDesc::named("CursorPosition");
}

[[nodiscard]] inline ReturnTypeDesc size() {
    return ReturnTypeDesc::named("Size");
}

[[nodiscard]] inline ReturnTypeDesc udp_packet() {
    return ReturnTypeDesc::named("UdpPacket");
}

[[nodiscard]] inline ReturnTypeDesc address() {
    return ReturnTypeDesc::named("Address");
}

// Socket.parse_ip returns this parsed IP-literal choice directly, so — like
// json_value() / file_kind() — it uses the fully-qualified name (choices resolve
// qualified), and needs a matching branch in stdlib_type_signatures.cpp.
[[nodiscard]] inline ReturnTypeDesc ip_address() {
    return ReturnTypeDesc::named("Socket.IpAddress");
}

[[nodiscard]] inline ReturnTypeDesc url_parts() {
    return ReturnTypeDesc::named("UrlParts");
}

// Http.parse_cookie / cookie_header parse and format this flat cookie record
// (bare short name "Cookie", like url_parts()).
[[nodiscard]] inline ReturnTypeDesc cookie() {
    return ReturnTypeDesc::named("Cookie");
}

} // namespace named

// ─── Builder helpers ───────────────────────────────────────────────
// These reduce per-entry boilerplate from ~10 fields (with repeated
// `false` for is_constant and the full qualified name) to a concise
// 2-3 field call.
//
// The builder carries the module prefix and capability so that each
// entry does not need to repeat them.
//
// Usage:
//   const ModuleBuilder math{"Math"};
//   math.fn("absolute", 1, "(value: integer | number)", R::result_number(), {p.number}),
//   math.constant("pi", R::number_type()),

struct ModuleBuilder {
    std::string prefix;
    Capability cap{Capability::None};

    // `cap_override` lets an individual function opt out of the module-wide
    // capability — e.g. a mostly in-memory module (Csv, Xml) where only the
    // file-I/O members should carry Capability::FileSystem.  Left unset, the
    // function inherits the module's `cap`.
    [[nodiscard]] FunctionSpec fn(std::string_view name, int arity, std::string params,
                                  ReturnTypeDesc ret, std::vector<ReturnTypeDesc> param_types = {},
                                  std::optional<Capability> cap_override = std::nullopt) const {
        return {luma::make_qualified(prefix, name),
                arity,
                std::move(params),
                false,
                std::move(ret),
                cap_override.value_or(cap),
                std::move(param_types)};
    }

    [[nodiscard]] FunctionSpec variadic_fn(std::string_view name, int min_arity, std::string params,
                                           ReturnTypeDesc ret,
                                           std::vector<ReturnTypeDesc> param_types = {}) const {
        return {luma::make_qualified(prefix, name),
                min_arity,
                std::move(params),
                false,
                std::move(ret),
                cap,
                std::move(param_types),
                true};
    }

    [[nodiscard]] FunctionSpec constant(std::string_view name, ReturnTypeDesc ret) const {
        return {luma::make_qualified(prefix, name), 0, "", true, std::move(ret), cap};
    }
};

// Structured parameter-type shorthands shared by all register_* helpers.
// Field names are descriptive to improve readability at registration sites.
struct ParamShorthands {
    // Primitive types
    ReturnTypeDesc integer, number, string, boolean, any, func;
    // Collection types
    ReturnTypeDesc array_any, array_number, array_string, dict_any;
    // Generic wrapper types
    ReturnTypeDesc result_any, optional_any, channel_any, task_any, reference_any;
    // Named types
    ReturnTypeDesc socket, matrix, log_level;
    ReturnTypeDesc set, xml, kv_store, queue, stack, linked_list, hash_set, binary_tree, graph;
    // UI types
    ReturnTypeDesc widget;
    // Exact-decimal type
    ReturnTypeDesc decimal;
};

// ─── Per-module registration functions ──────────────────────────────────────
// All registration functions share the same signature: they receive the specs
// vector, a ModuleBuilder (carrying the module prefix and capability), and
// the shared parameter-type shorthands.

using ModuleRegisterFn = void (*)(std::vector<FunctionSpec>&, const ModuleBuilder&,
                                  const ParamShorthands&);

void register_math_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p);

void register_converter_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                  const ParamShorthands& p);

void register_random_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                               const ParamShorthands& p);

void register_string_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                               const ParamShorthands& p);

void register_regular_expression_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                           const ParamShorthands& p);

void register_array_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p);

void register_dictionary_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                   const ParamShorthands& p);

void register_set_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                            const ParamShorthands& p);

void register_queue_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p);

void register_stack_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p);

void register_linked_list_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                    const ParamShorthands& p);

void register_hash_set_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                 const ParamShorthands& p);

void register_binary_tree_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                    const ParamShorthands& p);

void register_graph_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p);

void register_console_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                const ParamShorthands& p);

void register_file_system_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                    const ParamShorthands& p);

void register_process_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                const ParamShorthands& p);

void register_socket_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                               const ParamShorthands& p);

void register_http_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p);

void register_key_value_store_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                        const ParamShorthands& p);

void register_json_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p);

void register_csv_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                            const ParamShorthands& p);

void register_xml_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                            const ParamShorthands& p);

void register_encoder_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                const ParamShorthands& p);

void register_hash_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p);

void register_compression_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                    const ParamShorthands& p);

void register_linear_algebra_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                       const ParamShorthands& p);

void register_calculus_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                 const ParamShorthands& p);

void register_decimal_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                const ParamShorthands& p);

void register_task_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p);

void register_channel_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                const ParamShorthands& p);

void register_date_time_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                  const ParamShorthands& p);

void register_log_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                            const ParamShorthands& p);

void register_terminal_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                 const ParamShorthands& p);

void register_graphical_ui_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                     const ParamShorthands& p);

void register_result_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                               const ParamShorthands& p);

void register_optional_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                 const ParamShorthands& p);

void register_reference_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                  const ParamShorthands& p);

void register_resource_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                 const ParamShorthands& p);

void register_order_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p);

void register_color_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p);

} // namespace luma::stdlib::detail

#endif // LUMA_STDLIB_CATALOG_INTERNAL_HPP
