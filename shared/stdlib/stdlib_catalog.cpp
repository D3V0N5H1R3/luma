#include "stdlib/stdlib_catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "stdlib/stdlib_catalog_internal.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::stdlib {

namespace {

using detail::ModuleBuilder;
using detail::ModuleRegisterFn;
using detail::ParamShorthands;

// Helper: build the catalog by delegating to per-module registration
// functions, then building the lookup map.
[[nodiscard]] CatalogMap build_catalog() {
    using R = ReturnTypeDesc;
    namespace named = detail::named;

    // Structured parameter-type shorthands shared by all register_* helpers.
    // Static so the shorthand descriptors are constructed exactly once.
    static const ParamShorthands p = {
        .integer = R::integer_type(),
        .number = R::number_type(),
        .string = R::string_type(),
        .boolean = R::boolean_type(),
        .any = R::any_type(),
        .func = R::func_type(),
        .array_any = R::array_any(),
        .array_number = R::array_number(),
        .array_string = R::array_string(),
        .dict_any = R::dict_any(),
        .result_any = R::result_any(),
        .optional_any = R::optional_any(),
        .channel_any = R::channel_any(),
        .task_any = R::task_any(),
        .reference_any = R::reference_any(),
        .socket = named::socket(),
        .matrix = R::array_array_number(),
        .log_level = named::log_level(),
        .set = named::set(),
        .xml = named::xml(),
        .kv_store = named::key_value_store(),
        .queue = named::queue(),
        .stack = named::stack(),
        .linked_list = named::linked_list(),
        .hash_set = named::hash_set(),
        .binary_tree = named::binary_tree(),
        .graph = named::graph(),
        .widget = named::widget(),
        .decimal = named::decimal(),
    };

    // ── Unified registration table ──
    // Every module is registered through a single table.  Each entry carries
    // the module name, required capability, and registration function.  The
    // ModuleBuilder propagates the capability to each FunctionSpec, so no
    // post-hoc capability_map scan is needed.

    struct ModuleRegistration {
        const char* module_name;
        Capability cap;
        ModuleRegisterFn register_fn;
    };

    static constexpr ModuleRegistration k_registrations[] = {
        {.module_name = "Math",
         .cap = Capability::None,
         .register_fn = detail::register_math_functions},
        {.module_name = "Converter",
         .cap = Capability::None,
         .register_fn = detail::register_converter_functions},
        {.module_name = "Random",
         .cap = Capability::None,
         .register_fn = detail::register_random_functions},
        {.module_name = "String",
         .cap = Capability::None,
         .register_fn = detail::register_string_functions},
        {.module_name = "RegularExpression",
         .cap = Capability::None,
         .register_fn = detail::register_regular_expression_functions},
        {.module_name = "Array",
         .cap = Capability::None,
         .register_fn = detail::register_array_functions},
        {.module_name = "Dictionary",
         .cap = Capability::None,
         .register_fn = detail::register_dictionary_functions},
        {.module_name = "Set",
         .cap = Capability::None,
         .register_fn = detail::register_set_functions},
        {.module_name = "Queue",
         .cap = Capability::None,
         .register_fn = detail::register_queue_functions},
        {.module_name = "Stack",
         .cap = Capability::None,
         .register_fn = detail::register_stack_functions},
        {.module_name = "LinkedList",
         .cap = Capability::None,
         .register_fn = detail::register_linked_list_functions},
        {.module_name = "HashSet",
         .cap = Capability::None,
         .register_fn = detail::register_hash_set_functions},
        {.module_name = "BinaryTree",
         .cap = Capability::None,
         .register_fn = detail::register_binary_tree_functions},
        {.module_name = "Graph",
         .cap = Capability::None,
         .register_fn = detail::register_graph_functions},
        {.module_name = "Console",
         .cap = Capability::Console,
         .register_fn = detail::register_console_functions},
        {.module_name = "FileSystem",
         .cap = Capability::FileSystem,
         .register_fn = detail::register_file_system_functions},
        {.module_name = "Process",
         .cap = Capability::Process,
         .register_fn = detail::register_process_functions},
        {.module_name = "Socket",
         .cap = Capability::Network,
         .register_fn = detail::register_socket_functions},
        {.module_name = "Http",
         .cap = Capability::Network,
         .register_fn = detail::register_http_functions},
        {.module_name = "KeyValueStore",
         .cap = Capability::FileSystem,
         .register_fn = detail::register_key_value_store_functions},
        {.module_name = "Json",
         .cap = Capability::None,
         .register_fn = detail::register_json_functions},
        {.module_name = "Csv",
         .cap = Capability::None,
         .register_fn = detail::register_csv_functions},
        {.module_name = "Xml",
         .cap = Capability::None,
         .register_fn = detail::register_xml_functions},
        {.module_name = "Encoder",
         .cap = Capability::None,
         .register_fn = detail::register_encoder_functions},
        {.module_name = "Hash",
         .cap = Capability::None,
         .register_fn = detail::register_hash_functions},
        {.module_name = "Compression",
         .cap = Capability::None,
         .register_fn = detail::register_compression_functions},
        {.module_name = "LinearAlgebra",
         .cap = Capability::None,
         .register_fn = detail::register_linear_algebra_functions},
        {.module_name = "Calculus",
         .cap = Capability::None,
         .register_fn = detail::register_calculus_functions},
        {.module_name = "Decimal",
         .cap = Capability::None,
         .register_fn = detail::register_decimal_functions},
        {.module_name = "Task",
         .cap = Capability::None,
         .register_fn = detail::register_task_functions},
        {.module_name = "Channel",
         .cap = Capability::None,
         .register_fn = detail::register_channel_functions},
        {.module_name = "DateTime",
         .cap = Capability::None,
         .register_fn = detail::register_date_time_functions},
        {.module_name = "Log",
         .cap = Capability::None,
         .register_fn = detail::register_log_functions},
        {.module_name = "Terminal",
         .cap = Capability::None,
         .register_fn = detail::register_terminal_functions},
        {.module_name = "GraphicalUi",
         .cap = Capability::None,
         .register_fn = detail::register_graphical_ui_functions},
        {.module_name = "Result",
         .cap = Capability::None,
         .register_fn = detail::register_result_functions},
        {.module_name = "Optional",
         .cap = Capability::None,
         .register_fn = detail::register_optional_functions},
        {.module_name = "Reference",
         .cap = Capability::None,
         .register_fn = detail::register_reference_functions},
        {.module_name = "Resource",
         .cap = Capability::None,
         .register_fn = detail::register_resource_functions},
        {.module_name = "Order",
         .cap = Capability::None,
         .register_fn = detail::register_order_functions},
        {.module_name = "Color",
         .cap = Capability::None,
         .register_fn = detail::register_color_functions},
    };

    // Approximate total number of stdlib function and constant specs across all
    // modules (the real count is ~996).  Used only to pre-size the vector and
    // avoid reallocations during registration; it does not need to be exact but
    // must stay at or above the real total so the single up-front reserve covers
    // the whole build — otherwise the per-append reserve in append_specs would
    // force exact-size (non-geometric) reallocations once the estimate is
    // exceeded.
    static constexpr std::size_t k_approx_catalog_size = 1100;

    std::vector<FunctionSpec> specs;
    specs.reserve(k_approx_catalog_size);

    for (const auto& [name, cap, fn] : k_registrations) {
        const ModuleBuilder m{.prefix = name, .cap = cap};
        fn(specs, m, p);
    }

    // Reject negative arities.  This guards a static-data programming error, so
    // fail fast in every build (not just debug asserts): the catalog is built
    // once on first use, so the throw surfaces at a call site rather than during
    // static initialisation.
    for (const auto& spec : specs) {
        if (spec.arity < 0) {
            throw std::logic_error("stdlib catalog: negative arity for '" + spec.qualified_name +
                                   "' — use is_variadic_fn = true instead");
        }
    }

    CatalogMap result;
    result.reserve(specs.size());

    for (auto& spec : specs) {
        // try_emplace does not move `spec` on a key collision, so its name stays
        // valid for the diagnostic below.
        if (!result.try_emplace(spec.qualified_name, std::move(spec)).second) {
            throw std::logic_error("stdlib catalog: duplicate registration for '" +
                                   spec.qualified_name + "'");
        }
    }

    return result;
}

// Build all derived sets in a single pass over the catalog.
// The implicitly-generated special members only touch standard containers; the
// sole escape path the analyzer traces is MSVC STL bad_alloc, which is fatal and
// cannot be handled here.
struct DerivedSets { // NOLINT(bugprone-exception-escape)
    luma::StringSet constants;
    luma::StringSet blocked_modules;
    luma::StringSet result_returning;
};

[[nodiscard]] DerivedSets build_derived_sets(const CatalogMap& cat) {
    DerivedSets sets;

    for (const auto& [name, spec] : cat) {
        if (spec.is_constant) {
            sets.constants.insert(name);
        }

        if (!spec.is_safe()) {
            if (const auto split = split_module(name)) {
                sets.blocked_modules.insert(std::string{split->first});
            }
        }

        if (!spec.is_constant && spec.return_type.kind == ReturnTypeDesc::Result) {
            sets.result_returning.insert(name);
        }
    }

    return sets;
}

[[nodiscard]] const DerivedSets& derived_sets() {
    static const auto instance = build_derived_sets(catalog());
    return instance;
}

} // anonymous namespace

const CatalogMap& catalog() {
    static const auto instance = build_catalog();
    return instance;
}

const luma::StringSet& constants() {
    return derived_sets().constants;
}

const luma::StringSet& sandbox_blocked_modules() {
    return derived_sets().blocked_modules;
}

const luma::StringSet& result_returning_functions() {
    return derived_sets().result_returning;
}

} // namespace luma::stdlib
